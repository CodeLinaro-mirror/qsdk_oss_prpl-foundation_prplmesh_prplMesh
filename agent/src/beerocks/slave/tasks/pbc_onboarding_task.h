/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _PBC_ONBOARDING_TASK_H_
#define _PBC_ONBOARDING_TASK_H_

#include "task.h"
#include <bcl/network/sockets_impl.h>
#include <beerocks/tlvf/beerocks_message_common.h>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/wfa_map/sPbcSessionId.h>
#include <tlvf/wfa_map/tlvMultiApPushButtonStatus.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace beerocks {

/**
 * @brief Push-button onboarding task on the Agent.
 *
 * Reads the Push-Button Config TLVs of the AP-Autoconfiguration WSC messages, and keeps the
 * Listen Duration of each fronthaul BSS that got one. The WPS push-button parameters of the TLVs
 * go to the ap_manager with the BSS settings of the message (ApAutoConfigurationTask).
 * Handles the Multi-AP Push-Button Trigger message of the Controller: starts and cancels WPS PBC
 * on each listed BSSID, through the ap_manager of the radio that owns the BSSID. Reports the
 * session status of each armed BSSID to the Controller in Multi-AP Push-Button Status messages.
 */
class PBCOnboardingTask : public Task {
public:
    /**
     * @brief Sends the messages of the task: to the Controller, and to the ap_manager of a radio.
     *
     * Decouples the task from the Agent thread, which owns the connections.
     */
    class MessageSender {
    public:
        virtual ~MessageSender() = default;

        /**
         * @brief Sends a CMDU to the Controller.
         *
         * @param cmdu_tx CMDU message to send.
         * @return true on success and false otherwise.
         */
        virtual bool send_cmdu_to_controller(ieee1905_1::CmduMessageTx &cmdu_tx) = 0;

        /**
         * @brief Gets the file descriptor of the connection to the ap_manager of a radio.
         *
         * @param radio_iface Interface name of the radio.
         * @return The file descriptor, or net::FileDescriptor::invalid_descriptor if the
         * ap_manager is not connected.
         */
        virtual int get_ap_manager_fd(const std::string &radio_iface) = 0;

        /**
         * @brief Sends a CMDU through a connection.
         *
         * @param fd File descriptor of the connection.
         * @param cmdu_tx CMDU message to send.
         * @return true on success and false otherwise.
         */
        virtual bool send_cmdu(int fd, ieee1905_1::CmduMessageTx &cmdu_tx) = 0;
    };

    /** @brief Callable that returns the current time; injectable for testing. */
    using now_f = std::function<std::chrono::steady_clock::time_point()>;

    /**
     * @brief Construct the task.
     *
     * @param sender Sends the messages of the task.
     * @param cmdu_tx Shared CMDU transmit buffer.
     * @param now Clock source; defaults to std::chrono::steady_clock::now.
     */
    PBCOnboardingTask(std::unique_ptr<MessageSender> sender, ieee1905_1::CmduMessageTx &cmdu_tx,
                      now_f now = std::chrono::steady_clock::now);
    ~PBCOnboardingTask() {}

    /**
     * @brief Handles the Multi-AP Push-Button Trigger message.
     *
     * @return true for the Multi-AP Push-Button Trigger message, false for all other messages.
     */
    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;

    /**
     * @brief WPS push-button configuration of one BSS, read from one Push-Button Config TLV.
     */
    struct sPushButtonConfig {
        /** BSS_Index of the TLV: selects the M2 with the same BSS_Index. Never 0. */
        uint8_t bss_index = 0;
        /** UUID-E as text, for example "12345678-9abc-def0-1234-56789abcdef0". */
        std::string uuid_e;
        /** WPS Device Password ID. */
        uint16_t device_password_id = 0;
        /** WPS Device Password. Empty when the TLV has none. Never logged. */
        std::string device_password;
        /**
         * Listen Duration of the TLV, in seconds. Time limit of a started push-button session
         * of the BSS. Never 0.
         */
        uint16_t listen_duration = 0;
    };

    /**
     * @brief Reads the Push-Button Config TLVs of an AP-Autoconfiguration WSC message.
     *
     * A TLV with a BSSID other than zero (the Controller selects a BSS by its BSS_Index), with
     * BSS_Index 0, with Listen Duration 0, or with a Device Password of more than 63 octets or
     * that is not printable text, is skipped with an error log.
     *
     * @param radio_iface Interface name of the radio named by the AP Radio Identifier TLV, for
     * the log.
     * @param cmdu_rx The AP-Autoconfiguration WSC message.
     * @return The configs of the valid TLVs, in the order of the message.
     */
    static std::vector<sPushButtonConfig>
    read_push_button_configs(const std::string &radio_iface, ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Keeps the Listen Durations of the fronthaul BSSs of a radio that got a Push-Button
     * Config TLV in the last AP-Autoconfiguration WSC message for the radio.
     *
     * The values replace those of a previous WSC message for the same radio.
     *
     * @param radio_iface Interface name of the radio.
     * @param listen_durations SSID and Listen Duration (in seconds) of each BSS.
     */
    void
    set_listen_durations(const std::string &radio_iface,
                         const std::vector<std::pair<std::string, uint16_t>> &listen_durations);

    /**
     * @brief Ends the sessions on the radio of a disconnected ap_manager, and reports them.
     * Also forgets the Listen Durations of the radio.
     *
     * @param radio_iface Interface name of the radio.
     */
    void handle_ap_manager_disconnected(const std::string &radio_iface);

    /**
     * @brief Ends each session without a final status in time, and reports it.
     */
    void work() override;

    /**
     * @brief Handles ACTION_APMANAGER_MESHPBC_STATUS_NOTIFICATION.
     *
     * @param bssid BSSID of the session.
     * @param status Session status.
     * @param reason Reason string from the ap_manager, unchanged.
     * @param enrollee_mac Enrollee MAC address, or ZERO_MAC.
     */
    void handle_status_notification(const sMacAddr &bssid, beerocks_message::eMeshPbcStatus status,
                                    const std::string &reason, const sMacAddr &enrollee_mac);

private:
    /**
     * @brief One Multi-AP Push-Button Status TLV.
     */
    struct sStatusReport {
        sMacAddr bssid;
        wfa_map::sPbcSessionId session_id;
        wfa_map::tlvMultiApPushButtonStatus::eResult result;
        /** WSC Configuration Error, sent only for Failure/Aborted and Not started. */
        uint16_t failure_reason = 0;
        sMacAddr enrollee_mac;
    };

    /**
     * @brief Handles one Multi-AP Push-Button Trigger message from the Controller.
     *
     * @param cmdu_rx The message.
     * @param src_mac AL MAC address of the sender.
     */
    void handle_trigger(ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &src_mac);

    /**
     * @brief Answers one BSSID of a Trigger with State = TRUE: duplicate check, then WPS start.
     *
     * @param bssid BSSID listed in the Trigger.
     * @param session_id PBC Session ID of the Trigger.
     * @param wps true if the Trigger has the WPS method. Without it, the BSSID gets Not started.
     * @param[out] reports The Not started report, if WPS PBC cannot be started.
     */
    void start_session(const sMacAddr &bssid, const wfa_map::sPbcSessionId &session_id, bool wps,
                       std::vector<sStatusReport> &reports);

    /**
     * @brief Cancels WPS PBC on one BSSID for a Trigger with State = FALSE.
     * A BSSID without a session of this task, or with a session of another PBC Session ID, is
     * not changed.
     *
     * @param bssid BSSID listed in the Trigger.
     * @param session_id PBC Session ID of the Trigger.
     */
    void stop_session(const sMacAddr &bssid, const wfa_map::sPbcSessionId &session_id);

    /**
     * @brief Sends the cancel request for one BSSID to the ap_manager of its radio.
     *
     * @return true if the request was sent, otherwise false.
     */
    bool send_cancel_request(const sMacAddr &bssid, const std::string &radio_iface);

    /**
     * @brief Sends one Multi-AP Push-Button Status message to the Controller, with one Status
     * TLV for each report.
     */
    void send_status_reports(const std::vector<sStatusReport> &reports);

    /**
     * @brief Ends the session of one BSSID with a Failure/Aborted or Not started report.
     * The result depends on whether the session started.
     *
     * @param bssid BSSID of the session.
     * @param reason Agent reason, logged only.
     * @param cancel true to send the cancel request for the BSSID first (fail closed after a time
     * limit, so that no WPS session stays armed without a report).
     * @param error true to log the end at ERROR level, false to log it at INFO level (an expected
     * end, for example at the Listen Duration).
     */
    void end_session_with_failure(const std::string &bssid, const std::string &reason, bool cancel,
                                  bool error);

    /**
     * @brief Forgets the kept Listen Durations of the BSSs of one radio.
     *
     * @param radio_iface Interface name of the radio.
     */
    void forget_listen_durations(const std::string &radio_iface);

    /**
     * @brief A push-button session armed by a Trigger, until its final status.
     */
    struct sSession {
        std::string radio_iface;           /**< Interface name of the radio that owns the BSSID */
        wfa_map::sPbcSessionId session_id; /**< PBC Session ID of the Trigger that armed it */
        bool started          = false;     /**< The Started status was received */
        bool cancel_requested = false;     /**< A Trigger with State = FALSE listed the BSSID */
        std::chrono::steady_clock::time_point deadline; /**< Time limit of the session */
        /** Listen Duration of the BSSID at the start request: time limit after Started. */
        std::chrono::seconds listen_duration{0};
    };

    /**
     * Listen Duration in seconds of each fronthaul BSS that got a Push-Button Config TLV in the
     * last WSC message for its radio, by radio interface name and SSID. The SSID is the key
     * because a BSS that the message creates has no BSSID yet. A Trigger with State = TRUE
     * finds the entry by the SSID of the BSSID in the AgentDB. A BSSID without an entry, or
     * whose SSID is used by more than one fronthaul BSS of the radio, gets Not started.
     */
    std::map<std::pair<std::string, std::string>, uint16_t> m_listen_durations;

    std::unique_ptr<MessageSender> m_sender;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    now_f m_now;

    /**
     * Sessions without a final status, by BSSID.
     */
    std::unordered_map<std::string, sSession> m_sessions;

    /**
     * PBC Session ID of the last Trigger with State = TRUE that got an answer, by BSSID. A later
     * Trigger with State = TRUE and the same PBC Session ID is a duplicate and gets no answer.
     */
    std::unordered_map<std::string, wfa_map::sPbcSessionId> m_last_session_ids;
};

} // namespace beerocks

#endif // _PBC_ONBOARDING_TASK_H_
