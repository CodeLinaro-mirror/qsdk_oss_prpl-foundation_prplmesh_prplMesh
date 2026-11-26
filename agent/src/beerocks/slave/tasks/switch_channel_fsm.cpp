/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "switch_channel_fsm.h"

#include <algorithm>
#include <utility>

namespace beerocks {
namespace switch_channel {

constexpr std::chrono::seconds SwitchChannelFsm::NOTIFICATION_TIMEOUT;

SwitchChannelFsm::SwitchChannelFsm(const std::string &ifname, TaskPoolInterface &task_pool,
                                   SendRequest send_request, Clock now)
    : beerocks_fsm(fsm_state::IDLE), m_ifname(ifname), m_task_pool(task_pool),
      m_send_request(std::move(send_request)), m_now(std::move(now))
{
    config_fsm();
}

std::ostream &operator<<(std::ostream &out, const fsm_state &value)
{
    switch (value) {
    case fsm_state::IDLE:
        return out << "IDLE";
    case fsm_state::WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION:
        return out << "WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION";
    case fsm_state::WAIT_FOR_CAC_COMPLETED:
        return out << "WAIT_FOR_CAC_COMPLETED";
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const fsm_event &value)
{
    switch (value) {
    case fsm_event::SWITCH_CHANNEL_REQUEST:
        return out << "SWITCH_CHANNEL_REQUEST";
    case fsm_event::CSA:
        return out << "CSA";
    case fsm_event::CAC_STARTED:
        return out << "CAC_STARTED";
    case fsm_event::CAC_COMPLETED:
        return out << "CAC_COMPLETED";
    case fsm_event::CANCELLED:
        return out << "CANCELLED";
    case fsm_event::PERIODIC:
        return out << "PERIODIC";
    }
    return out;
}

void SwitchChannelFsm::config_fsm()
{
    config()
        .state(fsm_state::IDLE)
        .entry([&](const void *) {
            reset();
            return true;
        })
        .on(fsm_event::SWITCH_CHANNEL_REQUEST, fsm_state::WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION,
            [&](TTransition &, const void *args) {
                m_switch_channel_request =
                    *static_cast<const std::shared_ptr<const sSwitchChannelRequest> *>(args);
                if (!m_switch_channel_request || m_switch_channel_request->ifname != m_ifname) {
                    reset();
                    return false;
                }
                if (!m_send_request(*m_switch_channel_request)) {
                    report(eSwitchChannelReportStatus::GENERAL_FAIL);
                    reset();
                    return false;
                }
                m_deadline = m_now() + NOTIFICATION_TIMEOUT;
                return true;
            })
        .on(fsm_event::CSA, fsm_state::IDLE,
            [&](TTransition &, const void *args) {
                auto notification =
                    *static_cast<const std::shared_ptr<sSwitchChannelNotification> *>(args);
                if (notification && notification->ifname == m_ifname) {
                    m_task_pool.send_event(eTaskEvent::SWITCH_CHANNEL_REPORT,
                                           std::make_shared<sSwitchChannelReport>(
                                               false, nullptr, notification, nullptr, nullptr));
                }
                return false;
            })
        .on(fsm_event::PERIODIC, fsm_state::IDLE)
        .on(fsm_event::CANCELLED, fsm_state::IDLE)

        .state(fsm_state::WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION)
        .on(fsm_event::CANCELLED, fsm_state::IDLE,
            [&](TTransition &, const void *args) { return handle_cancelled(args); })
        // Preserve the request here: it identifies both the CAC and its eventual report.
        .on(fsm_event::CSA, fsm_state::IDLE,
            [&](TTransition &transition, const void *args) { return handle_csa(transition, args); })
        .on(fsm_event::CAC_STARTED, fsm_state::WAIT_FOR_CAC_COMPLETED,
            [&](TTransition &, const void *args) {
                auto notification =
                    *static_cast<const std::shared_ptr<sCacStartedNotification> *>(args);
                if (!notification ||
                    !m_switch_channel_request->matches_cac_started(*notification)) {
                    return false;
                }
                m_cac_started_notification = notification;
                // Keep at least five seconds of slack, including when the driver reports zero.
                auto duration =
                    std::chrono::seconds(notification->cac_started_params.cac_duration_sec);
                duration += std::max(std::chrono::seconds(5), duration / 5);
                auto started_at        = m_now();
                m_deadline             = started_at + duration;
                auto wait              = std::make_shared<sSwitchChannelDurationTime>();
                wait->ifname           = m_ifname;
                wait->duration_sec     = duration;
                wait->original_request = m_switch_channel_request;
                wait->started_at       = started_at;
                m_task_pool.send_event(eTaskEvent::SWITCH_CHANNEL_DURATION_TIME, wait);
                return true;
            })
        // Completion itself is sufficient evidence even if CAC-START was lost.
        .on(fsm_event::CAC_COMPLETED, fsm_state::IDLE,
            [&](TTransition &transition, const void *args) {
                return handle_cac_completed(transition, args);
            })
        .on(fsm_event::SWITCH_CHANNEL_REQUEST, fsm_state::WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION,
            [&](TTransition &transition, const void *args) {
                return report_switch_channel_in_progress(transition, args);
            })
        .on(fsm_event::PERIODIC, fsm_state::IDLE,
            [&](TTransition &, const void *) {
                if (m_now() <= m_deadline) {
                    return false;
                }
                LOG(ERROR) << m_ifname << ": timeout waiting for channel switch or CAC start";
                report(m_switch_channel_request->wait_for_cac
                           ? eSwitchChannelReportStatus::CAC_FAILED
                           : eSwitchChannelReportStatus::NO_SWITCH_CHANNEL_NOTIFICATION);
                return true;
            })

        .state(fsm_state::WAIT_FOR_CAC_COMPLETED)
        .on(fsm_event::CANCELLED, fsm_state::IDLE,
            [&](TTransition &, const void *args) { return handle_cancelled(args); })
        .on(fsm_event::CSA, fsm_state::IDLE,
            [&](TTransition &transition, const void *args) { return handle_csa(transition, args); })
        .on(fsm_event::CAC_COMPLETED, fsm_state::IDLE,
            [&](TTransition &transition, const void *args) {
                return handle_cac_completed(transition, args);
            })
        // Duplicate start events must not extend the completion deadline.
        .on(fsm_event::CAC_STARTED, fsm_state::WAIT_FOR_CAC_COMPLETED)
        .on(fsm_event::SWITCH_CHANNEL_REQUEST, fsm_state::WAIT_FOR_CAC_COMPLETED,
            [&](TTransition &transition, const void *args) {
                return report_switch_channel_in_progress(transition, args);
            })
        .on(fsm_event::PERIODIC, fsm_state::IDLE, [&](TTransition &, const void *) {
            if (m_now() <= m_deadline) {
                return false;
            }
            LOG(ERROR) << m_ifname << ": timeout waiting for CAC completion";
            report(eSwitchChannelReportStatus::CAC_FAILED);
            return true;
        });
    start();
}

bool SwitchChannelFsm::handle_csa(TTransition &, const void *args)
{
    auto notification = *static_cast<const std::shared_ptr<sSwitchChannelNotification> *>(args);
    if (!notification || notification->ifname != m_ifname ||
        notification->ap_channel_switch.channel != m_switch_channel_request->channel) {
        return false;
    }
    if (!notification->switched) {
        m_switch_channel_notification = std::move(notification);
        report(eSwitchChannelReportStatus::CSA_FAILED);
        return true;
    }
    if (notification->ap_channel_switch.bandwidth != m_switch_channel_request->bandwidth) {
        return false;
    }
    m_switch_channel_notification = std::move(notification);
    if (m_switch_channel_request->wait_for_cac || m_cac_started_notification) {
        // Channel/DM changes also occur when CAC starts, before the channel is usable.
        return false;
    }
    report();
    return true;
}

bool SwitchChannelFsm::handle_cac_completed(TTransition &, const void *args)
{
    // Match the request, including hostapd's primary/secondary swap for HT40 CACs.
    auto notification = *static_cast<const std::shared_ptr<sCacCompletedNotification> *>(args);
    if (!notification || !m_switch_channel_request->matches_cac_completed(*notification) ||
        (!m_switch_channel_request->wait_for_cac && !m_cac_started_notification)) {
        return false;
    }
    m_cac_completed_notification = std::move(notification);
    report();
    return true;
}

bool SwitchChannelFsm::handle_cancelled(const void *args)
{
    return *static_cast<const std::shared_ptr<const sSwitchChannelRequest> *>(args) ==
           m_switch_channel_request;
}

void SwitchChannelFsm::report(eSwitchChannelReportStatus status)
{
    m_task_pool.send_event(
        eTaskEvent::SWITCH_CHANNEL_REPORT,
        std::make_shared<sSwitchChannelReport>(
            m_switch_channel_request->wait_for_cac || bool(m_cac_started_notification),
            m_switch_channel_request, m_switch_channel_notification, m_cac_started_notification,
            m_cac_completed_notification, status));
}

void SwitchChannelFsm::reset()
{
    m_switch_channel_request.reset();
    m_switch_channel_notification.reset();
    m_cac_started_notification.reset();
    m_cac_completed_notification.reset();
}

bool SwitchChannelFsm::report_switch_channel_in_progress(TTransition &, const void *args)
{
    auto request = *static_cast<const std::shared_ptr<const sSwitchChannelRequest> *>(args);
    m_task_pool.send_event(eTaskEvent::SWITCH_CHANNEL_REPORT,
                           std::make_shared<sSwitchChannelReport>(
                               false, request, nullptr, nullptr, nullptr,
                               eSwitchChannelReportStatus::ANOTHER_SWITCH_IN_PROGRESS));
    return false;
}

} // namespace switch_channel
} // namespace beerocks
