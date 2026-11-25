/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _SWITCH_CHANNEL_FSM_H_
#define _SWITCH_CHANNEL_FSM_H_

#include "task_messages.h"
#include "task_pool_interface.h"
#include <bcl/beerocks_state_machine.h>

namespace beerocks {
namespace switch_channel {

enum class fsm_state { IDLE, WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION, WAIT_FOR_CAC_COMPLETED };
enum class fsm_event {
    SWITCH_CHANNEL_REQUEST,
    CSA,
    CAC_STARTED,
    CAC_COMPLETED,
    CANCELLED,
    PERIODIC
};

std::ostream &operator<<(std::ostream &out, const fsm_state &value);
std::ostream &operator<<(std::ostream &out, const fsm_event &value);

class SwitchChannelFsm : public beerocks_fsm<fsm_state, fsm_event> {
public:
    using Clock       = std::function<std::chrono::steady_clock::time_point()>;
    using SendRequest = std::function<bool(const sSwitchChannelRequest &)>;

    SwitchChannelFsm(const std::string &ifname, TaskPoolInterface &task_pool,
                     SendRequest send_request, Clock now = std::chrono::steady_clock::now);

private:
    void config_fsm();
    void reset();
    void report(eSwitchChannelReportStatus status = eSwitchChannelReportStatus::NOT_COMPUTED);
    bool handle_csa(TTransition &transition, const void *args);
    bool handle_cac_completed(TTransition &transition, const void *args);
    bool handle_cancelled(const void *args);
    bool report_switch_channel_in_progress(TTransition &transition, const void *args);

    const std::string m_ifname;
    TaskPoolInterface &m_task_pool;
    SendRequest m_send_request;
    Clock m_now;
    std::shared_ptr<const sSwitchChannelRequest> m_switch_channel_request;
    std::shared_ptr<sSwitchChannelNotification> m_switch_channel_notification;
    std::shared_ptr<sCacStartedNotification> m_cac_started_notification;
    std::shared_ptr<sCacCompletedNotification> m_cac_completed_notification;
    // Includes driver reconfiguration before CAC-START (over five seconds on OSPv2).
    static constexpr std::chrono::seconds NOTIFICATION_TIMEOUT{15};
    std::chrono::steady_clock::time_point m_deadline;
};

} // namespace switch_channel
} // namespace beerocks

#endif
