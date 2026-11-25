/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "coordinated_cac_task.h"
#include "../backhaul_manager/backhaul_manager.h"
#include "task_pool_interface.h"
#include <bcl/beerocks_defines.h>
#include <bcl/beerocks_utils.h>
#include <bcl/beerocks_wifi_channel.h>

namespace beerocks {
namespace coordinated_cac {

///////////////
// cac task ///
///////////////
CoordinatedCacTask::CoordinatedCacTask(TaskPoolInterface &task_pool,
                                       BackhaulManager &backhaul_manager,
                                       ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::CAC_TASK), m_backhaul_manager(backhaul_manager), m_cmdu_tx(cmdu_tx),
      m_fsm(task_pool, cmdu_tx,
            [this](const std::string &ifname, ieee1905_1::CmduMessageTx &message) {
                auto agent_fd = m_backhaul_manager.get_agent_fd();
                auto db       = AgentDB::get();
                auto radio    = db->radio(ifname);
                if (agent_fd == net::FileDescriptor::invalid_descriptor || !radio) {
                    LOG(ERROR) << "Cannot send CAC command for " << ifname;
                    return false;
                }
                message_com::get_beerocks_header(message)->actionhdr()->radio_mac() =
                    radio->front.iface_mac;
                return m_backhaul_manager.send_cmdu(agent_fd, message);
            })
{
}

std::vector<eTaskEvent> CoordinatedCacTask::get_task_event_list() const
{
    return {eTaskEvent::SWITCH_CHANNEL_DURATION_TIME, eTaskEvent::SWITCH_CHANNEL_REPORT,
            eTaskEvent::CAC_STARTED_NOTIFICATION, eTaskEvent::CAC_COMPLETED_NOTIFICATION};
}

void CoordinatedCacTask::work()
{
    // work is called periodically
    // this is the chance to handle periodic "event"
    m_fsm.fire(fsm_event::PERIODIC);
}

void CoordinatedCacTask::handle_event(uint8_t event_enum_value, const void *event_obj) {}

void CoordinatedCacTask::handle_event(eTaskEvent event, std::shared_ptr<void> event_obj)
{
    std::ostringstream oss;
    oss << event; // Explicitly use the operator<< for eTaskEvent
    LOG(DEBUG) << "got event to handle: " << oss.str();

    switch (event) {

    case eTaskEvent::SWITCH_CHANNEL_DURATION_TIME: {
        auto switch_channel_duration_time =
            std::static_pointer_cast<const sSwitchChannelDurationTime>(event_obj);
        if (!switch_channel_duration_time) {
            LOG(ERROR) << "casting event_obj resulted in nullptr";
            return;
        }
        m_fsm.fire(fsm_event::SWITCH_CHANNEL_DURATION_TIME,
                   reinterpret_cast<const void *>(&switch_channel_duration_time));
    } break;

    case eTaskEvent::SWITCH_CHANNEL_REPORT: {
        auto switch_channel_report =
            std::static_pointer_cast<const sSwitchChannelReport>(event_obj);
        if (!switch_channel_report) {
            LOG(ERROR) << "casting event_obj resulted in nullptr";
            return;
        }
        m_fsm.fire(fsm_event::SWITCH_CHANNEL_REPORT,
                   reinterpret_cast<const void *>(&switch_channel_report));
    } break;

    case eTaskEvent::CAC_STARTED_NOTIFICATION: {
        auto cac_started_notification =
            std::static_pointer_cast<const sCacStartedNotification>(event_obj);
        if (!cac_started_notification) {
            LOG(ERROR) << "casting event_obj resulted in nullptr";
            return;
        }
        m_fsm.fire(fsm_event::CAC_STARTED_NOTIFICATION,
                   reinterpret_cast<const void *>(&cac_started_notification));
    } break;

    case eTaskEvent::CAC_COMPLETED_NOTIFICATION: {
        auto cac_completed_notification =
            std::static_pointer_cast<const sCacCompletedNotification>(event_obj);
        if (!cac_completed_notification) {
            LOG(ERROR) << "casting event_obj resulted in nullptr";
            return;
        }
        m_fsm.fire(fsm_event::CAC_COMPLETED_NOTIFICATION,
                   reinterpret_cast<const void *>(&cac_completed_notification));
    } break;

    default: {
        LOG(WARNING) << "got unexpected event: " << static_cast<uint8_t>(event);
    } break;
    }
}

bool CoordinatedCacTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                                     std::shared_ptr<beerocks_header> beerocks_header)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::CAC_REQUEST_MESSAGE: {
        const auto mid       = cmdu_rx.getMessageId();
        auto cac_request_tlv = cmdu_rx.getClass<wfa_map::tlvProfile2CacRequest>();
        if (!cac_request_tlv) {
            LOG(ERROR) << "CAC REQUEST CMDU mid=" << std::hex << mid
                       << " does not have profile 2 cac request TLV";
            return true;
        }
        // send ACK to the controller
        bool ack_sent = m_backhaul_manager.send_ack_to_controller(m_cmdu_tx, mid);
        LOG(DEBUG) << "CAC REQUEST: Ack was sent to controller? " << ack_sent;

        // let the fsm handle the request
        m_fsm.fire(fsm_event::CAC_REQUEST, reinterpret_cast<const void *>(&cac_request_tlv));
    } break;

    case ieee1905_1::eMessageType::CAC_TERMINATION_MESSAGE: {
        const auto mid           = cmdu_rx.getMessageId();
        auto cac_termination_tlv = cmdu_rx.getClass<wfa_map::tlvProfile2CacTermination>();
        if (!cac_termination_tlv) {
            LOG(ERROR) << "CAC TERMINATION CMDU mid=" << std::hex << mid
                       << " does not have profile 2 cac termination TLV";
            return true;
        }
        // send ACK to the controller
        bool ack_sent = m_backhaul_manager.send_ack_to_controller(m_cmdu_tx, mid);
        LOG(DEBUG) << "CAC TERMINATION: Ack was sent to controller? " << ack_sent;

        // let the fsm handle the termination
        m_fsm.fire(fsm_event::CAC_TERMINATION_REQUEST,
                   reinterpret_cast<const void *>(&cac_termination_tlv));
    } break;

    case ieee1905_1::eMessageType::VENDOR_SPECIFIC_MESSAGE: {
        // Internally, the 'handle_vendor_specific' might not really handle
        // the CMDU, thus we need to return the real return value and not 'true'.
        return handle_vendor_specific(cmdu_rx, src_mac, fd, beerocks_header);
    }
    default: {
        return false;
    }
    }
    return true;
}

bool CoordinatedCacTask::handle_vendor_specific(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                const sMacAddr &src_mac, int sd,
                                                std::shared_ptr<beerocks_header> beerocks_header)
{
    if (!beerocks_header) {
        LOG(ERROR) << "beerocks_header is nullptr";
        return false;
    }

    // Since currently we handle only action_ops of action type "ACTION_BACKHAUL", use a single
    // switch-case on "ACTION_BACKHAUL" only.
    // Once the son_slave will be unified, need to replace the expected action to
    // "ACTION_AP_MANAGER". PPM-352.
    if (beerocks_header->action() != beerocks_message::ACTION_BACKHAUL) {
        return false;
    }

    switch (beerocks_header->action_op()) {
    case beerocks_message::ACTION_BACKHAUL_HOSTAP_CANCEL_ACTIVE_CAC_RESPONSE: {
        auto cac_response =
            beerocks_header
                ->addClass<beerocks_message::cACTION_BACKHAUL_HOSTAP_CANCEL_ACTIVE_CAC_RESPONSE>();
        if (!cac_response) {
            LOG(ERROR) << "addClass cACTION_BACKHAUL_HOSTAP_CANCEL_ACTIVE_CAC_RESPONSE failed";
            return false;
        }
        m_fsm.fire(fsm_event::CAC_TERMINATION_RESPONSE,
                   reinterpret_cast<const void *>(&cac_response->success()));
        break;
    }
    default: {
        // Message was not handled, therefore return false.
        return false;
    }
    }
    return true;
}

} // namespace coordinated_cac
} // namespace beerocks
