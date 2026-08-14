/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "spectrum_inquiry_task.h"
#include "channel_selection_task.h"

#include <bcl/beerocks_string_utils.h>
#include <bcl/network/network_utils.h>
#include <beerocks/tlvf/beerocks_message_backhaul.h>
#include <easylogging++.h>

#include <backhaul_manager/backhaul_manager.h>

#include <algorithm>

namespace beerocks {
namespace {

bool parse_possible_channels_list(const std::string &possible_channels_str,
                                  std::unordered_set<uint8_t> &channels)
{
    channels.clear();
    for (const auto &chan_str : beerocks::string_utils::str_split(possible_channels_str, ',')) {
        if (chan_str.empty()) {
            continue;
        }
        channels.insert(static_cast<uint8_t>(beerocks::string_utils::stoi(chan_str)));
    }
    return !channels.empty();
}

} // namespace

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
    (void)cmdu_rx;
    (void)iface_index;
    (void)dst_mac;
    (void)src_mac;
    (void)fd;

    if (!beerocks_header || beerocks_header->action() != beerocks_message::ACTION_BACKHAUL) {
        return false;
    }

    if (beerocks_header->action_op() != beerocks_message::ACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION) {
        return false;
    }

    auto notification =
        beerocks_header->addClass<beerocks_message::cACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION>();
    if (!notification) {
        LOG(ERROR) << "addClass ACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION failed";
        return false;
    }

    return handle_afc_update_notification(*notification, beerocks_header->actionhdr()->radio_mac());
}

bool SpectrumInquiryTask::handle_afc_update_notification(
    beerocks_message::cACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION &notification,
    const sMacAddr &radio_mac)
{
    LOG(INFO) << "AFC update notification received for radio " << radio_mac
              << ", preparing AVAILABLE_SPECTRUM_INQUIRY_MESSAGE";

    auto db = AgentDB::get();

    const auto inquiry_request  = notification.inquiry_request_str();
    const auto inquiry_response = notification.inquiry_response_str();

    if (inquiry_request.empty() || inquiry_response.empty()) {
        LOG(ERROR) << "AFC update notification missing inquiry request/response payload";
        return false;
    }

    if (db->afc_available_spectrum_request != inquiry_request) {
        db->afc_radio_platform_data.clear();
    }

    db->afc_available_spectrum_request  = inquiry_request;
    db->afc_available_spectrum_response = inquiry_response;

    if (notification.grant_successful()) {
        db->afc_spectrum_update_completed = true;
        LOG(INFO) << "AFC spectrum update completed with successful grant";
    } else {
        db->afc_spectrum_update_completed = false;
        LOG(WARNING) << "AFC update received but grant is not successful; "
                        "optional inquiry Channel Preference TLVs will not be applied";
    }

    auto &radio_platform_data                 = db->afc_radio_platform_data[radio_mac];
    radio_platform_data.data_valid            = true;
    radio_platform_data.regulatory_applicable = notification.regulatory_applicable() != 0;
    radio_platform_data.possible_channels.clear();

    const auto possible_channels_str = notification.possible_channels_str();
    if (!possible_channels_str.empty()) {
        parse_possible_channels_list(possible_channels_str, radio_platform_data.possible_channels);
    }

    size_t six_g_radio_count = 0;
    size_t reported_count    = 0;
    for (const auto radio : db->get_radios_list()) {
        if (radio->wifi_channel.get_freq_type() != beerocks::eFreqType::FREQ_6G) {
            continue;
        }
        six_g_radio_count++;
        const auto it = db->afc_radio_platform_data.find(radio->front.iface_mac);
        if (it != db->afc_radio_platform_data.end() && it->second.data_valid) {
            reported_count++;
        }
    }

    if (reported_count < six_g_radio_count) {
        LOG(DEBUG) << "AFC platform data received from " << reported_count << "/"
                   << six_g_radio_count << " 6 GHz radio(s); deferring inquiry message";
        return true;
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
