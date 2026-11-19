/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_ONBOARDING_TASK_H_
#define _DPP_ONBOARDING_TASK_H_

#include "task.h"

#include <tlvf/common/sMacAddr.h>

#include <chrono>
#include <memory>
#include <string>

namespace son {

class db;
class dpp_protocol_task;

/**
 * @brief FEAT-68 Controller-as-Configurator onboarding orchestration.
 *
 * Owns URI sync against the bootstrap store, CCE ENABLE, wait states
 * for Authentication / Configuration / Connection Status, and success/failure
 * cleanup. URI replace resets the protocol session and re-enters Auth
 * wait; cleanup after success/clear allows a second onboarding.
 * Does not drive Agent-side DPP_CONFIGURATOR_ADD; Auth/Config run in
 * dpp_protocol_task over Proxied/Direct Encap.
 */
class dpp_onboarding_task : public task {
public:
    dpp_onboarding_task(db &database_, std::shared_ptr<dpp_protocol_task> protocol_task_);

    enum eEventType {
        AUTH_SUCCESS = 1,
        AUTH_INIT_FAILED,
        CONF_SENT,
        CONF_RECEIVED,
        CONF_FAILED,
        CONN_STATUS_OK,
        CONN_STATUS_FAILED,
        FAIL,
        BOOTSTRAP_TRIGGERED,
        // dpp_protocol_task sent a (first or restarted) DPP Authentication Request.
        AUTH_STARTED,
        // Validated Wi-Fi backhaul evidence; does not restart onboarding.
        WIFI_BACKHAUL_OBSERVED
    };

    /**
     * @brief Desired CCE state: enabled from the first work() for the Controller lifetime.
     *
     * Stays set after a failed or timed-out session and after onboarding (EasyMesh 5.3.1
     * recommends keeping the CCE advertised). URI clear does not disable it.
     * This is not on-air confirmation.
     */
    bool is_cce_desired() const { return cce_advertised; }

    /**
     * @brief Reconcile CCE policy, then send ENABLE to one agent if CCE was already advertised.
     * A fresh enable broadcasts to all eligible agents, so no targeted copy is sent.
     */
    void send_cce_to_agent(const sMacAddr &al_mac);

protected:
    void work() override;
    void handle_event(int event_type, void *obj) override;
    void handle_events_timeout(std::multiset<int> pending_events) override;
    void handle_task_end() override;

private:
    enum class State { IDLE, WAIT_AUTH, WAIT_CONF, WAIT_CONN_STATUS };

    bool sync_bootstrap_uri();
    // Enables the CCE once; it is never withdrawn.
    void reconcile_cce_policy();
    void enable_cce();
    void reset_active_session();
    void restart_for_bootstrap_trigger(const std::string &reason);
    void enter_wait_auth();
    void finish_success();
    void finish_failure(const std::string &reason, bool retry);
    void schedule_retry(std::chrono::steady_clock::time_point now);
    static std::string consume_reason(void *obj);

    db &database;
    std::shared_ptr<dpp_protocol_task> m_protocol_task;
    State state = State::IDLE;
    std::string last_bootstrap_fingerprint;
    // Only used to log the first observation; the CCE is desired regardless.
    bool wifi_backhaul_observed = false;
    // Desired state, not confirmation that any agent is advertising on air.
    bool cce_advertised     = false;
    bool expect_conn_status = false;
    int failure_count       = 0;
    std::chrono::steady_clock::time_point next_retry_time;
    std::string last_failure_reason;
};

} // namespace son

#endif // _DPP_ONBOARDING_TASK_H_
