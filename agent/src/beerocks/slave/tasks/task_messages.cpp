/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "task_messages.h"
#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <beerocks/tlvf/beerocks_message.h>

namespace beerocks {

namespace {
// Return the centre only when the event primary is the other half of the requested HT40 block.
uint16_t swapped_cac_center_frequency(const sSwitchChannelRequest &request, uint8_t primary)
{
    if (request.freq_type != eFreqType::FREQ_5G || request.bandwidth != BANDWIDTH_40) {
        return 0;
    }
    auto center = son::wireless_utils::get_vht_central_frequency(request.channel, request.bandwidth,
                                                                 request.freq_type);
    auto requested_freq = son::wireless_utils::channel_to_freq(request.channel, request.freq_type);
    auto primary_freq   = son::wireless_utils::channel_to_freq(primary, request.freq_type);
    if (center && requested_freq != primary_freq && requested_freq + primary_freq == 2 * center) {
        return center;
    }
    return 0;
}
} // namespace

// switch channel request //

sSwitchChannelRequest::sSwitchChannelRequest()
    : channel(0), freq_type(eFreqType::FREQ_UNKNOWN), bandwidth(eWiFiBandwidth::BANDWIDTH_UNKNOWN)
{
}

sSwitchChannelRequest::sSwitchChannelRequest(const sSwitchChannelRequest &rhs)
    : ifname(rhs.ifname), channel(rhs.channel), freq_type(rhs.freq_type), bandwidth(rhs.bandwidth),
      wait_for_cac(rhs.wait_for_cac)
{
}

bool sSwitchChannelRequest::matches_cac_started(const sCacStartedNotification &notification) const
{
    if (notification.ifname != ifname) {
        return false;
    }
    const auto &params = notification.cac_started_params;
    if (params.channel == channel) {
        return true;
    }
    auto center = swapped_cac_center_frequency(*this, params.channel);
    // WHM decodes hostapd's width=0 (20/40 MHz) as 20 MHz. The secondary offset
    // distinguishes a real HT40 CAC from a 20 MHz CAC on only half the block.
    if (!center || (params.bandwidth != BANDWIDTH_20 && params.bandwidth != BANDWIDTH_40)) {
        return false;
    }
    int offset = params.secondary_channel == 1
                     ? 1
                     : params.secondary_channel == static_cast<uint8_t>(-1) ? -1 : 0;
    return offset &&
           son::wireless_utils::channel_to_freq(params.channel, freq_type) + offset * 10 == center;
}

bool sSwitchChannelRequest::matches_cac_completed(
    const sCacCompletedNotification &notification) const
{
    if (notification.ifname != ifname) {
        return false;
    }
    const auto &params = notification.cac_completed_params;
    if (params.channel == channel) {
        return true;
    }
    auto center = swapped_cac_center_frequency(*this, params.channel);
    return center && params.bandwidth == BANDWIDTH_40 && params.center_frequency1 == center &&
           params.center_frequency2 == 0 &&
           params.frequency == son::wireless_utils::channel_to_freq(params.channel, freq_type);
}

std::ostream &operator<<(std::ostream &os, const sSwitchChannelRequest &switch_channel_request)
{
    os << "switch channel request - " << '\n'
       << "ifname:    " << switch_channel_request.ifname << '\n'
       << "channel:   " << +switch_channel_request.channel << '\n'
       << "freq type: "
       << beerocks::utils::convert_frequency_type_to_string(switch_channel_request.freq_type)
       << '\n'
       << "bandwidth: "
       << beerocks::utils::convert_bandwidth_to_string(switch_channel_request.bandwidth) << '\n';

    return os;
}

// switch channel notification time //
std::ostream &operator<<(std::ostream &os,
                         const sSwitchChannelDurationTime &switch_channel_duration_time)
{
    os << "switch channel duration - " << '\n'
       << "ifname:       " << switch_channel_duration_time.ifname << '\n'
       << "duration sec: " << switch_channel_duration_time.duration_sec.count() << '\n';

    return os;
}

// switch channel report //

sSwitchChannelReport::sSwitchChannelReport()
    : cac_happened(false), status(eSwitchChannelReportStatus::NOT_COMPUTED)
{
}

sSwitchChannelReport::sSwitchChannelReport(
    bool cac_happened_, std::shared_ptr<const sSwitchChannelRequest> original_request_,
    std::shared_ptr<sSwitchChannelNotification> switch_channel_notification_,
    std::shared_ptr<sCacStartedNotification> cac_started_notification_,
    std::shared_ptr<sCacCompletedNotification> cac_completed_notification_,
    eSwitchChannelReportStatus status_)
    : cac_happened(cac_happened_), original_request(original_request_),
      switch_channel_notification(switch_channel_notification_),
      cac_started_notification(cac_started_notification_),
      cac_completed_notification(cac_completed_notification_), status(status_)
{
}

eSwitchChannelReportStatus sSwitchChannelReport::compute_switch_channel_report_status() const
{
    // the function calculates switch channel report status

    // calculation:
    // if the status is already set, return whatever is set.
    // when cac happened, the success is based on the cac-completed success
    // when cac didn't happen - we look at the m_ok of the notification
    // if we are missing expected data - it is not computed

    if (status != eSwitchChannelReportStatus::NOT_COMPUTED) {
        LOG(DEBUG) << "switch-channel-report-status was set by the caller, returning it: "
                   << static_cast<int>(status);
        return status;
    }

    if (!switch_channel_notification && !cac_started_notification && !cac_completed_notification) {
        LOG(ERROR) << "no switch-channel-notification at all. can't calculate success";
        return eSwitchChannelReportStatus::NOT_COMPUTED;
    }

    if (cac_happened) {

        if (!cac_completed_notification) {
            LOG(ERROR) << "no cac-completed-notification. can't calculate sucess";
            return eSwitchChannelReportStatus::NOT_COMPUTED;
        }

        if (cac_completed_notification->cac_completed_params.success != 1) {
            LOG(ERROR) << "switch-channel-notification reported cac failure";
            return eSwitchChannelReportStatus::CAC_FAILED;
        }

        // report ok
        LOG(DEBUG) << "switch-channel-report (with cac) computed as: SUCCESS";
        return eSwitchChannelReportStatus::SUCCESS;

    } else {
        if (!switch_channel_notification) {
            LOG(ERROR) << "no switch-channel-notification in the report.";
            return eSwitchChannelReportStatus::NO_SWITCH_CHANNEL_NOTIFICATION;
        }

        if (!switch_channel_notification->switched) {
            LOG(ERROR) << "switch-channel-notification reported csa failure";
            return eSwitchChannelReportStatus::CSA_FAILED;
        }

        // report ok
        LOG(DEBUG) << "switch-channel-report (without cac) computed as: SUCCESS";
        return eSwitchChannelReportStatus::SUCCESS;
    }
}

std::ostream &operator<<(std::ostream &os, const sSwitchChannelReport &switch_channel_report)
{
    os << "switch-channel-report - \nTODO: print the report\n";
    return os;
}

} // namespace beerocks
