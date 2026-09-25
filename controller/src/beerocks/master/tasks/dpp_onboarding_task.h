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

#include <chrono>
#include <memory>
#include <string>

namespace son {

class db;
class dpp_protocol_task;

/**
 * @brief FEAT-68 Controller-as-Configurator onboarding orchestration.
 *
 * Owns URI sync against the bootstrap store, CCE ENABLE/DISABLE, wait states
 * for Authentication / Configuration / Connection Status, and success/failure
 * cleanup. Does not drive Agent-side DPP_CONFIGURATOR_ADD; Auth/Config run in
 * dpp_protocol_task over Proxied Encap.
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
        BOOTSTRAP_TRIGGERED
    };

private:
    enum class State { IDLE, WAIT_AUTH, WAIT_CONF, WAIT_CONN_STATUS };

    void work() override;
    void handle_event(int event_type, void *obj) override;
    void handle_events_timeout(std::multiset<int> pending_events) override;
    void handle_task_end() override;

    bool sync_bootstrap_uri();
    void enable_cce();
    void disable_cce();
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
    std::string last_completed_fingerprint;
    bool cce_advertised = false;
    bool expect_conn_status = false;
    int failure_count = 0;
    std::chrono::steady_clock::time_point next_retry_time;
    std::string last_failure_reason;
};

} // namespace son

#endif // _DPP_ONBOARDING_TASK_H_
