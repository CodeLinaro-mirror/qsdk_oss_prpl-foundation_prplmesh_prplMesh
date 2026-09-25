/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_onboarding_task.h"

#include "dpp_protocol_task.h"

#include "../controller.h"
#include "../db/db.h"

#include <algorithm>
#include <easylogging++.h>

namespace {

constexpr int k_poll_interval_ms = 1000;
constexpr int k_retry_delay_ms   = 5000;
constexpr int k_retry_delay_max_ms = 60000;
constexpr int k_retry_backoff_max_exp = 4;
constexpr int k_event_timeout_ms = 30000;

std::string take_reason(void *obj)
{
    if (!obj) {
        return {};
    }

    auto *reason_ptr   = static_cast<std::string *>(obj);
    std::string reason = *reason_ptr;
    delete reason_ptr;
    return reason;
}

} // namespace

namespace son {

dpp_onboarding_task::dpp_onboarding_task(db &database_,
                                         std::shared_ptr<dpp_protocol_task> protocol_task_)
    : task("dpp_onboarding_task"), database(database_), m_protocol_task(std::move(protocol_task_)),
      next_retry_time(std::chrono::steady_clock::now())
{
}

std::string dpp_onboarding_task::consume_reason(void *obj) { return take_reason(obj); }

void dpp_onboarding_task::enable_cce()
{
    auto *controller_ctx = database.get_controller_ctx();
    if (!controller_ctx) {
        TASK_LOG(ERROR) << "No controller context for DPP CCE Indication enable";
        return;
    }

    TASK_LOG(INFO) << "Sending DPP CCE Indication ENABLE";
    controller_ctx->send_dpp_cce_indication(true);
    cce_advertised = true;
}

void dpp_onboarding_task::disable_cce()
{
    auto *controller_ctx = database.get_controller_ctx();
    if (!controller_ctx) {
        TASK_LOG(ERROR) << "No controller context for DPP CCE Indication disable";
        return;
    }

    TASK_LOG(INFO) << "Sending DPP CCE Indication DISABLE";
    controller_ctx->send_dpp_cce_indication(false);
    cce_advertised = false;
}

void dpp_onboarding_task::reset_active_session()
{
    expect_conn_status = false;
    if (m_protocol_task) {
        m_protocol_task->reset_session();
    }
}

void dpp_onboarding_task::schedule_retry(std::chrono::steady_clock::time_point now)
{
    const int exp      = std::min(failure_count, k_retry_backoff_max_exp);
    const int delay_ms = std::min(k_retry_delay_ms * (1 << exp), k_retry_delay_max_ms);
    next_retry_time    = now + std::chrono::milliseconds(delay_ms);
    wait_for(delay_ms);
}

void dpp_onboarding_task::restart_for_bootstrap_trigger(const std::string &reason)
{
    if (!reason.empty()) {
        TASK_LOG(INFO) << reason;
    }

    reset_active_session();
    state = State::IDLE;
    // Force sync_bootstrap_uri() to observe UPDATED or CLEARED on next work().
    last_bootstrap_fingerprint = std::string(1, '\0');
    last_completed_fingerprint.clear();
    failure_count = 0;
    last_failure_reason.clear();
    clear_pending_events();
    next_retry_time = std::chrono::steady_clock::now();
    wait_for(0);
}

void dpp_onboarding_task::finish_success()
{
    TASK_LOG(INFO) << "DPP onboarding completed successfully";
    reset_active_session();
    last_completed_fingerprint = last_bootstrap_fingerprint;
    state                      = State::IDLE;
    failure_count              = 0;
    last_failure_reason.clear();
    clear_pending_events();
    // C-8 / M-5: withdraw CCE advertisement when onboarding completes.
    disable_cce();
    wait_for(k_poll_interval_ms);
}

void dpp_onboarding_task::finish_failure(const std::string &reason, bool retry)
{
    auto failure_reason = reason.empty() ? std::string("DPP onboarding failed") : reason;
    failure_count++;
    last_failure_reason = failure_reason;
    TASK_LOG(WARNING) << failure_reason;
    reset_active_session();
    state = State::IDLE;
    clear_pending_events();
    // C-8: withdraw CCE on abort; IDLE re-ENABLE if URI is still provisioned for retry.
    disable_cce();
    if (retry) {
        schedule_retry(std::chrono::steady_clock::now());
    } else {
        wait_for(k_poll_interval_ms);
    }
}

void dpp_onboarding_task::enter_wait_auth()
{
    state = State::WAIT_AUTH;
    expect_conn_status = false;
    clear_pending_events();
    wait_for_event(AUTH_SUCCESS);
    wait_for_event(AUTH_INIT_FAILED);
    wait_for_event(CONF_SENT);
    wait_for_event(CONF_RECEIVED);
    wait_for_event(CONF_FAILED);
    wait_for_event(FAIL);
    wait_for_event(BOOTSTRAP_TRIGGERED);
    set_events_timeout(k_event_timeout_ms);
    TASK_LOG(INFO) << "Waiting for DPP Authentication (Controller Configurator / Proxied Encap)";
}

bool dpp_onboarding_task::sync_bootstrap_uri()
{
    const std::string fingerprint = database.calculate_dpp_bootstrap_map_fingerprint();

    if (fingerprint == last_bootstrap_fingerprint) {
        return false;
    }

    if (fingerprint.empty()) {
        TASK_LOG(INFO) << "DPP bootstrap URI store cleared";
        reset_active_session();
        last_bootstrap_fingerprint.clear();
        last_completed_fingerprint.clear();
        failure_count = 0;
        last_failure_reason.clear();
        clear_pending_events();
        disable_cce();
        state = State::IDLE;
        return true;
    }

    TASK_LOG(INFO) << "DPP bootstrap URI store updated (fingerprint changed)";
    reset_active_session();
    last_bootstrap_fingerprint = fingerprint;
    last_completed_fingerprint.clear();
    failure_count = 0;
    last_failure_reason.clear();
    clear_pending_events();
    enable_cce();
    state = State::IDLE;
    return true;
}

void dpp_onboarding_task::work()
{
    auto now = std::chrono::steady_clock::now();

    // URI replace / clear must abort wait states (M-13).
    if (state != State::IDLE) {
        const std::string fingerprint = database.calculate_dpp_bootstrap_map_fingerprint();
        if (fingerprint != last_bootstrap_fingerprint) {
            sync_bootstrap_uri();
            wait_for(0);
            return;
        }
    }

    switch (state) {
    case State::IDLE: {
        sync_bootstrap_uri();

        const std::string fingerprint = database.calculate_dpp_bootstrap_map_fingerprint();
        if (fingerprint.empty()) {
            reset_active_session();
            last_bootstrap_fingerprint.clear();
            last_completed_fingerprint.clear();
            failure_count = 0;
            last_failure_reason.clear();
            wait_for(k_poll_interval_ms);
            return;
        }

        last_bootstrap_fingerprint = fingerprint;

        if (fingerprint == last_completed_fingerprint) {
            wait_for(k_poll_interval_ms);
            return;
        }

        if (now < next_retry_time) {
            wait_for(k_poll_interval_ms);
            return;
        }

        // Protocol task starts Auth on chirp match; onboarding only waits.
        if (!cce_advertised) {
            enable_cce();
        }
        enter_wait_auth();
        return;
    }
    case State::WAIT_AUTH:
    case State::WAIT_CONF:
    case State::WAIT_CONN_STATUS:
        wait_for(k_poll_interval_ms);
        return;
    }
}

void dpp_onboarding_task::handle_event(int event_type, void *obj)
{
    auto reason = consume_reason(obj);
    switch (event_type) {
    case AUTH_SUCCESS:
        TASK_LOG(INFO) << "DPP authentication success";
        failure_count = 0;
        last_failure_reason.clear();
        if (state == State::WAIT_AUTH) {
            state = State::WAIT_CONF;
            clear_pending_events();
            wait_for_event(CONF_SENT);
            wait_for_event(CONF_RECEIVED);
            wait_for_event(CONF_FAILED);
            wait_for_event(FAIL);
            wait_for_event(BOOTSTRAP_TRIGGERED);
            set_events_timeout(k_event_timeout_ms);
        }
        break;
    case CONF_SENT:
        if (reason.find("wait_conn_status=1") != std::string::npos) {
            TASK_LOG(INFO) << "DPP configuration sent; will wait for connection status result";
            expect_conn_status = true;
        } else {
            TASK_LOG(INFO) << "DPP configuration response sent";
        }
        break;
    case CONF_RECEIVED:
        if (expect_conn_status || reason.find("wait_conn_status=1") != std::string::npos) {
            TASK_LOG(INFO) << "DPP configuration result OK; waiting for connection status result";
            expect_conn_status = true;
            state = State::WAIT_CONN_STATUS;
            clear_pending_events();
            wait_for_event(CONN_STATUS_OK);
            wait_for_event(CONN_STATUS_FAILED);
            wait_for_event(FAIL);
            wait_for_event(BOOTSTRAP_TRIGGERED);
            set_events_timeout(k_event_timeout_ms);
            break;
        }
        finish_success();
        break;
    case CONN_STATUS_OK:
        TASK_LOG(INFO) << "DPP connection status result OK";
        finish_success();
        break;
    case CONN_STATUS_FAILED:
        finish_failure(reason.empty() ? "DPP connection status failed" : reason, true);
        break;
    case AUTH_INIT_FAILED:
    case CONF_FAILED:
    case FAIL:
        finish_failure(reason.empty() ? "DPP onboarding failed" : reason, true);
        break;
    case BOOTSTRAP_TRIGGERED:
        restart_for_bootstrap_trigger(reason.empty() ? "DPP bootstrapping trigger updated"
                                                     : reason);
        break;
    default:
        break;
    }
}

void dpp_onboarding_task::handle_events_timeout(std::multiset<int> pending_events)
{
    (void)pending_events;
    if (state == State::WAIT_CONN_STATUS) {
        finish_failure("DPP onboarding timed out waiting for connection status result", true);
        return;
    }

    finish_failure("DPP onboarding timed out waiting for events", true);
}

void dpp_onboarding_task::handle_task_end() { reset_active_session(); }

} // namespace son
