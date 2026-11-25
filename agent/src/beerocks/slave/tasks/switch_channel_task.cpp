/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "switch_channel_task.h"
#include "../backhaul_manager/backhaul_manager.h"
#include "switch_channel_fsm.h"
#include <bcl/beerocks_utils.h>

namespace beerocks {
namespace switch_channel {

bool load_switch_channel_request_to_cmdu_tx(ieee1905_1::CmduMessageTx &cmdu_tx,
                                            const sSwitchChannelRequest &switch_channel_request)
{
    auto request = message_com::create_vs_message<
        beerocks_message::cACTION_BACKHAUL_HOSTAP_CHANNEL_SWITCH_ACS_START>(cmdu_tx);
    if (!request) {
        LOG(ERROR) << "Failed to build message";
        return false;
    }

    request->cs_params().channel   = switch_channel_request.channel;
    request->cs_params().bandwidth = switch_channel_request.bandwidth;

    auto channels_table =
        (switch_channel_request.freq_type == eFreqType::FREQ_5G)
            ? son::wireless_utils::channels_table_5g
            : (switch_channel_request.freq_type == eFreqType::FREQ_6G)
                  ? son::wireless_utils::channels_table_6g
                  : std::map<uint8_t,
                             std::map<beerocks::eWiFiBandwidth, son::wireless_utils::sChannel>>();
    if (channels_table.empty()) {
        LOG(ERROR) << "Invalid freq type: "
                   << beerocks::utils::convert_frequency_type_to_string(
                          switch_channel_request.freq_type)
                   << ". Must be either 5GHz or 6GHz";
        return false;
    }
    uint8_t center_channel = 0;
    auto center_channel_it = channels_table.find(switch_channel_request.channel);
    if (center_channel_it == channels_table.end()) {
        LOG(ERROR) << "Failed find channel " << switch_channel_request.channel << " in the "
                   << beerocks::utils::convert_frequency_type_to_string(
                          switch_channel_request.freq_type)
                   << " table, center channel is set to zero";
    } else {
        auto bandwidth_it = center_channel_it->second.find(switch_channel_request.bandwidth);
        if (bandwidth_it == center_channel_it->second.end()) {
            LOG(ERROR) << "Failed find bandwidth for " << switch_channel_request.channel
                       << " in the "
                       << beerocks::utils::convert_frequency_type_to_string(
                              switch_channel_request.freq_type)
                       << "table, center channel is set to zero";
        } else {
            center_channel = bandwidth_it->second.center_channel;
        }
    }

    request->cs_params().vht_center_frequency =
        son::wireless_utils::channel_to_freq(center_channel, switch_channel_request.freq_type);

    return true;
}

/////////////////////////
// switch channel task //
/////////////////////////
SwitchChannelTask::SwitchChannelTask(TaskPoolInterface &task_pool,
                                     BackhaulManager &backhaul_manager,
                                     ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::SWITCH_CHANNEL), m_task_pool(task_pool), m_backhaul_manager(backhaul_manager),
      m_cmdu_tx(cmdu_tx)
{
}

SwitchChannelTask::~SwitchChannelTask() {}

std::vector<eTaskEvent> SwitchChannelTask::get_task_event_list() const
{
    return {eTaskEvent::SWITCH_CHANNEL_REQUEST, eTaskEvent::SWITCH_CHANNEL_REQUEST_CANCELLED,
            eTaskEvent::CAC_STARTED_NOTIFICATION, eTaskEvent::CAC_COMPLETED_NOTIFICATION,
            eTaskEvent::SWITCH_CHANNEL_NOTIFICATION_EVENT};
}

bool SwitchChannelTask::send_switch_channel_request(const sSwitchChannelRequest &request)
{
    if (!load_switch_channel_request_to_cmdu_tx(m_cmdu_tx, request)) {
        LOG(ERROR) << "Failed to build switch channel request";
        return false;
    }
    auto agent_fd = m_backhaul_manager.get_agent_fd();
    auto db       = AgentDB::get();
    auto radio    = db->radio(request.ifname);
    if (agent_fd == net::FileDescriptor::invalid_descriptor || !radio) {
        LOG(ERROR) << "Cannot send switch channel request for " << request.ifname;
        return false;
    }
    message_com::get_beerocks_header(m_cmdu_tx)->actionhdr()->radio_mac() = radio->front.iface_mac;
    return m_backhaul_manager.send_cmdu(agent_fd, m_cmdu_tx);
}

SwitchChannelFsm &SwitchChannelTask::get_fsm_by_ifname(const std::string &ifname)
{
    auto fsm_it = m_fsms.find(ifname);

    if (fsm_it == m_fsms.end()) {
        fsm_it = m_fsms
                     .insert(std::make_pair(ifname,
                                            std::unique_ptr<SwitchChannelFsm>(new SwitchChannelFsm(
                                                ifname, m_task_pool,
                                                [this](const sSwitchChannelRequest &request) {
                                                    return send_switch_channel_request(request);
                                                }))))
                     .first;

        LOG(DEBUG) << "Added " << ifname << " to switch channel task. There are now "
                   << m_fsms.size() << " managed interfaces";
    }

    return *(fsm_it->second);
}

void SwitchChannelTask::work()
{
    // work is called periodically
    // this is the chance to handle periodic "event"
    for (auto &fsm : m_fsms) {
        if (!fsm.second) {
            LOG(ERROR) << "null fsm for interface " << fsm.first << ". skipping";
            continue;
        }
        fsm.second->fire(fsm_event::PERIODIC);
    }
}

void SwitchChannelTask::handle_event(eTaskEvent event, std::shared_ptr<void> event_obj)
{
    if (!event_obj) {
        LOG(WARNING) << "Received event " << static_cast<uint8_t>(event)
                     << " without data. Unable to handle the event.";
        return;
    }
    std::ostringstream oss;
    oss << event; // Explicitly use the operator<< for eTaskEvent
    LOG(DEBUG) << "Received event to handle: " << oss.str();

    switch (event) {
    case eTaskEvent::SWITCH_CHANNEL_REQUEST_CANCELLED:
    case eTaskEvent::SWITCH_CHANNEL_REQUEST: {
        auto switch_channel_request =
            std::static_pointer_cast<const sSwitchChannelRequest>(event_obj);
        if (!switch_channel_request) {
            LOG(ERROR) << "casting event_obj resulted in nullptr";
            return;
        }
        get_fsm_by_ifname(switch_channel_request->ifname)
            .fire(event == eTaskEvent::SWITCH_CHANNEL_REQUEST_CANCELLED
                      ? fsm_event::CANCELLED
                      : fsm_event::SWITCH_CHANNEL_REQUEST,
                  reinterpret_cast<const void *>(&switch_channel_request));
    } break;

    case eTaskEvent::SWITCH_CHANNEL_NOTIFICATION_EVENT: {
        auto switch_channel_notification =
            std::static_pointer_cast<sSwitchChannelNotification>(event_obj);
        if (!switch_channel_notification) {
            LOG(ERROR) << "casting event_obj resulted in nullptr";
            return;
        }
        get_fsm_by_ifname(switch_channel_notification->ifname)
            .fire(fsm_event::CSA, reinterpret_cast<const void *>(&switch_channel_notification));
    } break;

    case eTaskEvent::CAC_STARTED_NOTIFICATION: {
        auto cac_started_notification =
            std::static_pointer_cast<sCacStartedNotification>(event_obj);
        if (!cac_started_notification) {
            LOG(ERROR) << "casting event_obj resulted in nullptr";
            return;
        }
        get_fsm_by_ifname(cac_started_notification->ifname)
            .fire(fsm_event::CAC_STARTED,
                  reinterpret_cast<const void *>(&cac_started_notification));
    } break;

    case eTaskEvent::CAC_COMPLETED_NOTIFICATION: {
        auto cac_completed_notification =
            std::static_pointer_cast<sCacCompletedNotification>(event_obj);
        if (!cac_completed_notification) {
            LOG(ERROR) << "casting event_obj resulted in nullptr";
            return;
        }
        get_fsm_by_ifname(cac_completed_notification->ifname)
            .fire(fsm_event::CAC_COMPLETED,
                  reinterpret_cast<const void *>(&cac_completed_notification));
    } break;

    default: {
        LOG(WARNING) << "Received unexpected event: " << static_cast<uint8_t>(event);
    } break;
    }
}

} // namespace switch_channel
} // namespace beerocks
