/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _PBC_ONBOARDING_TASK_H_
#define _PBC_ONBOARDING_TASK_H_

#include "../db/db.h"
#include "task.h"

#include <tlvf/CmduMessageTx.h>
#include <tlvf/wfa_map/sPbcSessionId.h>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace son {

/**
 * @brief Controller side of a mesh-wide push-button onboarding session.
 *
 * The task owns the session: it resolves the X_PRPLWARE-COM_MeshPBCTemplate instance, selects
 * the target BSSs, creates the PBC Session ID, sends one Multi-AP Push-Button Trigger message
 * to each target Agent, runs the ListenDuration timer, handles the Multi-AP Push-Button Status
 * messages, and sends State = STOP when the session ends. It writes the template Status object
 * and the X_PRPLWARE-COM_MeshPBC object of each BSS.
 *
 * Only one session is active at a time. A Multi-AP Push-Button Request message from a
 * connected Agent starts a session with the template that
 * Templates.Network.X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference names.
 *
 * The task keeps one active-session record at most, plus a record for each BSS that waits for
 * its final report after a State = STOP (at most 10 s). It keeps no session history.
 */
class PBCOnboardingTask : public task {
public:
    /** Who started a session. */
    enum class eOrigin {
        CONTROLLER, /**< A call on the Controller (for example InitiatePBC()). */
        BUTTON,     /**< A Multi-AP Push-Button Request message from an Agent. */
    };

    /** Result of a session, or the cause of a rejected request. */
    enum class eResult {
        SUCCESS,
        ERROR_TIMEOUT,
        ERROR_NOT_READY,
        ERROR_INVALID_INPUT,
        ERROR_OTHER,
    };

    /** Values given to the completion callback at the end of a session. */
    struct sCompletion {
        /** PBC Session ID in the data-model form. */
        std::string session_id;
        eResult status          = eResult::ERROR_OTHER;
        uint16_t failure_reason = 0;
        /** Enrollee MAC address, ZERO_MAC unless status is SUCCESS. */
        sMacAddr enrollee_mac{};
        /** BSSID of the onboarding, ZERO_MAC unless status is SUCCESS. */
        sMacAddr bssid{};
    };

    using completion_callback = std::function<void(const sCompletion &)>;

    struct sStartRequest {
        /**
         * Reference to the X_PRPLWARE-COM_MeshPBCTemplate instance, with the prefix
         * "X_PRPLWARE-COM_Templates." or "Device.WiFi.Templates.".
         */
        std::string template_reference;
        /** Arm only the BSSs of this Agent. ZERO_MAC: the BSSs of all Agents. */
        sMacAddr agent_al_mac{};
        /**
         * Arm only these BSSs. Empty: all BSSs of the resolved set (of the Agent, if given).
         * Each BSSID must be in the resolved set of the template, and on the Agent, if given.
         */
        std::vector<sMacAddr> bssids;
        eOrigin origin = eOrigin::CONTROLLER;
        /**
         * For eOrigin::BUTTON: AL MAC of the Agent that sent the Request and the MID of the
         * Request. Not used for eOrigin::CONTROLLER.
         */
        wfa_map::sPbcSessionId request_id{};
        /** Called once at the end of an accepted session. Can be empty. */
        completion_callback on_completion;
    };

    struct sStartResult {
        /** true if a session was created. */
        bool accepted = false;
        /** ERROR_INVALID_INPUT or ERROR_NOT_READY if accepted is false. */
        eResult error = eResult::ERROR_OTHER;
        /** PBC Session ID in the data-model form, if accepted is true. */
        std::string session_id;
    };

    /** @brief Callable that returns the current time; injectable for testing. */
    using now_f = std::function<std::chrono::steady_clock::time_point()>;

    /**
     * @brief Construct the task.
     *
     * @param database The Controller database.
     * @param cmdu_tx Shared CMDU transmit buffer.
     * @param now Clock source of the session timers; defaults to std::chrono::steady_clock::now.
     */
    PBCOnboardingTask(db &database, ieee1905_1::CmduMessageTx &cmdu_tx,
                      now_f now = std::chrono::steady_clock::now);
    ~PBCOnboardingTask() override = default;

    /**
     * @brief Handle the Multi-AP Push-Button Request and Status messages.
     *
     * The Controller sends the 1905 Ack for both message types. Every other type is ignored.
     *
     * @return true if the message type is one of the two, otherwise false.
     */
    bool handle_ieee1905_1_msg(const sMacAddr &src_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx) override;

    /**
     * @brief Start a session.
     *
     * Returns at once. The session is rejected, with no Trigger sent, when:
     * - another session is active (ERROR_NOT_READY);
     * - Templates.Network.Enable is false (ERROR_NOT_READY);
     * - the template does not resolve, or a value of the template is not valid
     *   (ERROR_INVALID_INPUT);
     * - a listed BSSID is not in the resolved set of the template, or not on the given Agent
     *   (ERROR_INVALID_INPUT);
     * - no BSS is a target (ERROR_NOT_READY);
     * - for eOrigin::BUTTON, the PBC Session ID was used in the last 60 s (ERROR_NOT_READY).
     * A rejection writes Result and FailureReason of the template Status, unless that template
     * has the active session or the template does not resolve.
     * The BSSs of an Agent whose Trigger cannot be sent end at once with Error_Other. When no
     * Trigger was sent, the session ends with ERROR_OTHER at the next run of the task, after
     * start() returned; the completion callback is never called from start().
     *
     * @param request The parameters.
     * @return accepted with the PBC Session ID, or the error.
     */
    sStartResult start(const sStartRequest &request);

    /**
     * @brief Terminate the active session of a template (template level).
     *
     * The session ends with ERROR_OTHER and FailureReason 0.
     *
     * @param template_reference Reference to the template of the active session.
     * @return false if no session of that template is active.
     */
    bool terminate(const std::string &template_reference);

    /**
     * @brief Stop the armed BSSs of one Agent in the active session (Agent level).
     *
     * Sends State = STOP for those BSSs only; the session continues on the other Agents. When no
     * other BSS of the session is still Requested or InProgress, the session ends with
     * ERROR_OTHER and the last non-zero Failure Reason of the session (0 if none). After such a
     * stop, the session never ends with ERROR_NOT_READY.
     *
     * @param session_id PBC Session ID of the active session, in the data-model form.
     * @param agent_al_mac The Agent whose BSSs are stopped.
     * @return false if session_id is not the active session, or if no BSS of the Agent is armed
     * in it.
     */
    bool terminate(const std::string &session_id, const sMacAddr &agent_al_mac);

    /**
     * @brief Handle a Templates.Network change event with Enable false.
     *
     * Ends the active session, if any, with ERROR_OTHER and FailureReason 0. Every BSS that is
     * still Requested or InProgress gets State = STOP.
     */
    void handle_templates_network_disabled();

    /**
     * @brief Get the text form of a result: "Success", "Error_Timeout", ...
     */
    static const char *result_to_string(eResult result);

    /**
     * @brief Get the data-model form of a PBC Session ID: "<al mac>-<4 lowercase hex digits>".
     */
    static std::string session_id_to_string(const wfa_map::sPbcSessionId &session_id);

protected:
    void work() override;

private:
    /** State of a BSS, as in X_PRPLWARE-COM_MeshPBC.State. */
    enum class eBssState { IDLE, REQUESTED, IN_PROGRESS };

    /** Result of the latest window of a BSS, as in X_PRPLWARE-COM_MeshPBC.Result. */
    enum class eBssResult { NONE, SUCCESS, ERROR_NOT_READY, ERROR_TIMEOUT, ERROR_OTHER };

    static const char *bss_state_to_string(eBssState state);
    static const char *bss_result_to_string(eBssResult result);

    /** Why the Controller stopped a BSS. */
    enum class eStopCause {
        NONE,          /**< Not stopped by the Controller. */
        SESSION_TIMER, /**< The session timer expired. */
        OTHER,         /**< Success on another BSS, overlap, or terminate. */
    };

    /** A BSS that a session armed. */
    struct sArmedBss {
        sMacAddr al_mac{};
        eBssState state = eBssState::REQUESTED;
        /** true after the first final report (or the Controller ended the BSS). */
        bool ended = false;
        /** Result field of the final report, valid if ended is true and reported is true. */
        uint8_t reported_result = 0;
        bool reported           = false;
        eStopCause stop_cause   = eStopCause::NONE;
    };

    struct sSession {
        wfa_map::sPbcSessionId id{};
        std::string id_text;
        eOrigin origin = eOrigin::CONTROLLER;
        std::string template_path;
        bool wps                 = false;
        bool dpp                 = false;
        uint16_t listen_duration = 0;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point deadline;
        /** Armed BSSs, key BSSID. A stopped BSS moves to m_stopped_bsses. */
        std::unordered_map<sMacAddr, sArmedBss> bsses;
        /** Number of BSSs and Agents when the session started. */
        size_t armed_bss_count   = 0;
        size_t armed_agent_count = 0;
        /** Last non-zero Failure Reason of a final report of the session. */
        uint16_t last_failure_reason = 0;
        /**
         * true if the Controller stopped or ended a BSS of the session (terminate, a Trigger that
         * was not sent). The session then never ends with Error_Not_Ready.
         */
        bool controller_ended_bss = false;
        completion_callback on_completion;
    };

    /** A BSS that waits for its final report after a State = STOP. */
    struct sStoppedBss {
        wfa_map::sPbcSessionId session_id{};
        std::string session_id_text;
        sArmedBss bss;
        std::chrono::steady_clock::time_point deadline;
    };

    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    now_f m_now;

    std::unique_ptr<sSession> m_session;
    /** BSSs that wait for their final report after a State = STOP, key BSSID. */
    std::unordered_map<sMacAddr, sStoppedBss> m_stopped_bsses;

    /** Controller session counter (low 2 octets of a Controller PBC Session ID). */
    uint16_t m_session_counter = 0;
    /** PBC Session IDs used in the last 60 s, with the time of use. */
    std::deque<std::pair<wfa_map::sPbcSessionId, std::chrono::steady_clock::time_point>>
        m_recent_session_ids;

    void handle_request(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);
    void handle_status(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    sStartResult reject(const std::string &template_path, eResult error);
    bool next_controller_session_id(wfa_map::sPbcSessionId &session_id);
    bool recently_used(const wfa_map::sPbcSessionId &session_id);
    void remember_session_id(const wfa_map::sPbcSessionId &session_id);

    bool send_trigger(const sMacAddr &al_mac, const wfa_map::sPbcSessionId &session_id, bool wps,
                      bool dpp, bool start, const std::vector<sMacAddr> &bssids);

    void update_bss(const sMacAddr &bssid, const sMacAddr &al_mac, const std::string &session_id,
                    eBssState state, eBssResult result, uint16_t failure_reason,
                    const sMacAddr &enrollee_mac);
    /** @return true if the report changed the BSS. */
    bool apply_report(const sMacAddr &bssid, sArmedBss &bss, const std::string &session_id,
                      uint8_t result, uint16_t failure_reason, const sMacAddr &enrollee_mac);
    void evaluate_session(const sMacAddr &bssid, uint8_t result, uint16_t failure_reason,
                          const sMacAddr &enrollee_mac);

    void stop_bsses(const std::vector<sMacAddr> &bssids, eStopCause cause);
    bool end_if_nothing_armed();
    void end_session(eResult status, uint16_t failure_reason, const sMacAddr &enrollee_mac,
                     const sMacAddr &bssid);

    /** Store in the db whether a BSS of the Agent is Requested or InProgress. */
    void update_agent_armed(const sMacAddr &al_mac);
};

} // namespace son

#endif // _PBC_ONBOARDING_TASK_H_
