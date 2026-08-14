/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "spectrum_inquiry_task.h"
#include "channel_selection_task.h"
#include <bwl/afc_spectrum_helper.h>

#include <bcl/network/network_utils.h>
#include <beerocks/tlvf/beerocks_message_backhaul.h>
#include <easylogging++.h>

#include <backhaul_manager/backhaul_manager.h>

#include <algorithm>

namespace beerocks {

SpectrumInquiryTask::SpectrumInquiryTask(
    BackhaulManager &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx,
    const std::shared_ptr<ChannelSelectionTask> &channel_selection_task)
    : Task(eTaskType::SPECTRUM_INQUIRY), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx),
      m_channel_selection_task(channel_selection_task)
{
}

bool SpectrumInquiryTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                      const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                                      std::shared_ptr<beerocks_header> beerocks_header)
{
    if (!beerocks_header || beerocks_header->action() != beerocks_message::ACTION_BACKHAUL) {
        return false;
    }

    if (beerocks_header->action_op() != beerocks_message::ACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION) {
        return false;
    }

    return handle_afc_update_notification();
}

bool SpectrumInquiryTask::handle_afc_update_notification()
{
    LOG(INFO) << "AFC update notification received, preparing AVAILABLE_SPECTRUM_INQUIRY_MESSAGE";

    auto db = AgentDB::get();

    if (!bwl::whm::afc_spectrum_helper::read_available_spectrum_inquiry_data(
            db->afc_available_spectrum_request, db->afc_available_spectrum_response)) {
        LOG(ERROR) << "Failed to read AFC Available Spectrum Inquiry data from platform DM";
        return false;
    }

    if (bwl::whm::afc_spectrum_helper::is_afc_grant_successful()) {
        db->afc_spectrum_update_completed = true;
        LOG(INFO) << "AFC spectrum update completed with successful grant";
    } else {
        db->afc_spectrum_update_completed = false;
        LOG(WARNING) << "AFC update received but grant is not successful; "
                        "optional inquiry Channel Preference TLVs will not be applied";
    }

    if (!m_channel_selection_task) {
        LOG(ERROR) << "ChannelSelectionTask is not available";
        return false;
    }

    // EasyMesh §8.2.5 / §17.1.67: optional Channel Preference TLVs in the inquiry use
    // reason 0xD (change due to Available Spectrum Inquiry).
    for (const auto radio : db->get_radios_list()) {
        if (radio->wifi_channel.get_freq_type() != beerocks::eFreqType::FREQ_6G) {
            continue;
        }
        m_channel_selection_task->build_afc_channel_preference_report(radio->front.iface_mac);
    }

    return create_available_spectrum_inquiry_message();
}

bool SpectrumInquiryTask::create_available_spectrum_inquiry_message()
{
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE)) {
        LOG(ERROR) << "Failed to create AVAILABLE_SPECTRUM_INQUIRY_MESSAGE";
        return false;
    }

    if (!prepare_available_spectrum_inquiry_message()) {
        LOG(ERROR) << "AVAILABLE_SPECTRUM_INQUIRY_MESSAGE filling has failed";
        return false;
    }

    LOG(INFO) << "Sending AVAILABLE_SPECTRUM_INQUIRY_MESSAGE to controller";

    auto db = AgentDB::get();
    if (db->controller_info.bridge_mac == beerocks::net::network_utils::ZERO_MAC) {
        LOG(ERROR) << "Controller MAC unknown.";
        return false;
    }

    return m_btl_ctx.send_cmdu_to_broker(m_cmdu_tx, db->controller_info.bridge_mac, db->bridge.mac);
}

bool SpectrumInquiryTask::prepare_available_spectrum_inquiry_message()
{
    // TLVs required by EasyMesh §17.1.67 / §17.2.104 / §17.2.105 (exactly one request + response).
    auto db = AgentDB::get();

    auto request_tlv = m_cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
    if (!request_tlv) {
        LOG(ERROR) << "Failed to get tlvAvailableSpectrumInquiryRequest from CmduMessageTx";
        return false;
    }

    if (!add_available_spectrum_inquiry_request_tlv(request_tlv,
                                                    db->afc_available_spectrum_request)) {
        LOG(ERROR) << "Error filling AVAILABLE SPECTRUM INQUIRY REQUEST TLV";
        return false;
    }

    if (!add_available_spectrum_inquiry_response_tlv(m_cmdu_tx,
                                                     db->afc_available_spectrum_response)) {
        LOG(ERROR) << "Error filling AVAILABLE SPECTRUM INQUIRY RESPONSE TLV";
        return false;
    }

    if (!add_afc_channel_preference_tlvs()) {
        LOG(ERROR) << "Error filling optional Channel Preference TLVs";
        return false;
    }

    return true;
}

bool SpectrumInquiryTask::add_available_spectrum_inquiry_request_tlv(
    const std::shared_ptr<wfa_map::tlvAvailableSpectrumInquiryRequest> &request_tlv,
    const std::string &request_data)
{
    if (request_data.empty()) {
        LOG(ERROR) << "Available Spectrum Inquiry request data is empty";
        return false;
    }

    if (!request_tlv->alloc_available_spectrum_inquiry_request_obj(request_data.size())) {
        LOG(ERROR) << "Failed to allocate Available Spectrum Inquiry request object, size="
                   << request_data.size()
                   << ", remaining_buffer=" << request_tlv->getBuffRemainingBytes();
        return false;
    }

    auto *request_obj = request_tlv->available_spectrum_inquiry_request_obj(0);
    if (!request_obj) {
        LOG(ERROR) << "Failed to get Available Spectrum Inquiry request object pointer";
        return false;
    }

    std::copy_n(request_data.data(), request_data.size(), request_obj);
    return true;
}

bool SpectrumInquiryTask::add_available_spectrum_inquiry_response_tlv(
    ieee1905_1::CmduMessageTx &cmdu_tx, const std::string &response_data)
{
    if (response_data.empty()) {
        LOG(ERROR) << "Available Spectrum Inquiry response data is empty";
        return false;
    }

    auto response_tlv = cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryResponse>();
    if (!response_tlv) {
        LOG(ERROR) << "Failed to get tlvAvailableSpectrumInquiryResponse from CmduMessageTx";
        return false;
    }

    if (!response_tlv->alloc_available_spectrum_inquiry_response_obj(response_data.size())) {
        LOG(ERROR) << "Failed to allocate Available Spectrum Inquiry response object, size="
                   << response_data.size()
                   << ", remaining_buffer=" << response_tlv->getBuffRemainingBytes();
        return false;
    }

    auto *response_obj = response_tlv->available_spectrum_inquiry_response_obj(0);
    if (!response_obj) {
        LOG(ERROR) << "Failed to get Available Spectrum Inquiry response object pointer";
        return false;
    }

    std::copy_n(response_data.data(), response_data.size(), response_obj);
    return true;
}

bool SpectrumInquiryTask::add_afc_channel_preference_tlvs()
{
    if (!m_channel_selection_task) {
        return false;
    }

    auto db        = AgentDB::get();
    bool added_tlv = false;
    for (const auto radio : db->get_radios_list()) {
        if (radio->wifi_channel.get_freq_type() != beerocks::eFreqType::FREQ_6G) {
            continue;
        }

        const auto &radio_mac = radio->front.iface_mac;
        if (db->afc_radio_states.count(radio_mac) == 0 ||
            db->afc_radio_states.at(radio_mac).changed_channels.empty()) {
            LOG(INFO) << "No optional Channel Preference TLV for 6 GHz radio " << radio_mac
                      << " (no AFC channel delta)";
            continue;
        }

        if (!m_channel_selection_task->add_channel_preference_tlv(m_cmdu_tx, radio_mac)) {
            LOG(ERROR) << "Failed to add Channel Preference TLV for radio " << radio_mac;
            return false;
        }
        added_tlv = true;
        LOG(INFO) << "Added optional Channel Preference TLV for 6 GHz radio " << radio_mac;
    }

    if (!added_tlv) {
        LOG(INFO) << "AVAILABLE_SPECTRUM_INQUIRY_MESSAGE has no optional Channel Preference TLV; "
                     "controller will still trigger CSR from AFC response / 6 GHz radios (§8.2.5)";
    }

    return true;
}

} // namespace beerocks
