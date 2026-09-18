/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <beerocks/tlvf/beerocks_message_action.h>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/common/sMacAddr.h>
#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/tlvftypes.h>
#include <tlvf/wfa_map/eTlvTypeMap.h>
#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryRequest.h>
#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryResponse.h>
#include <tlvf/wfa_map/tlvChannelPreference.h>
#include <tlvf/wfa_map/tlvTransmitPowerLimit.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

namespace {

int g_fails = 0;

bool check(bool cond, const char *msg)
{
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        ++g_fails;
    }
    return cond;
}

std::string quoted(const std::string &key) { return std::string("\x22") + key + "\x22"; }

typedef wfa_map::cPreferenceOperatingClasses::eReasonCode Reason;
typedef wfa_map::cPreferenceOperatingClasses::ePreference Pref;

void channel_preference_afc_reason_codes_match_spec()
{
    check(static_cast<int>(Reason::CONTROLLER_DFS_CHANNEL_CLEAR_INDICATION) == 11,
          "reason 0xB DFS clear");
    check(static_cast<int>(Reason::OPERATION_DISALLOWED_BY_REGULATORY_RESTRICTION) == 12,
          "reason 0xC regulatory");
    check(static_cast<int>(Reason::CHANGE_DUE_TO_AVAILABLE_SPECTRUM_INQUIRY_AFC) == 13,
          "reason 0xD AFC inquiry");
}

void available_spectrum_inquiry_tlv_and_message_types()
{
    check(static_cast<int>(wfa_map::eTlvTypeMap::TLV_AVAILABLE_SPECTRUM_INQUIRY_REQUEST) == 232,
          "TLV request 0xE8");
    check(static_cast<int>(wfa_map::eTlvTypeMap::TLV_AVAILABLE_SPECTRUM_INQUIRY_RESPONSE) == 233,
          "TLV response 0xE9");
    check(static_cast<int>(ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE) == 32841,
          "CMDU 0x8049");
}

void internal_afc_update_action_opcodes()
{
    check(static_cast<int>(beerocks_message::ACTION_APMANAGER_AFC_UPDATE_NOTIFICATION) == 83,
          "AP manager AFC opcode 0x53");
    check(static_cast<int>(beerocks_message::ACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION) == 89,
          "backhaul AFC opcode 0x59");
    check(beerocks_message::ACTION_APMANAGER_AFC_UPDATE_NOTIFICATION <
              beerocks_message::ACTION_APMANAGER_ENUM_END,
          "AP manager opcode in range");
    check(beerocks_message::ACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION <
              beerocks_message::ACTION_BACKHAUL_ENUM_END,
          "backhaul opcode in range");
}

void round_trip_available_spectrum_inquiry_request_response()
{
    const size_t buf_size = 4096;
    uint8_t tx_buffer[4096];
    std::memset(tx_buffer, 0, buf_size);
    ieee1905_1::CmduMessageTx cmdu_tx(tx_buffer, buf_size);

    if (!check(cmdu_tx.create(7, ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE),
               "create inquiry CMDU")) {
        return;
    }

    const std::string request_json = std::string("{") + quoted("availableSpectrumInquiryRequests") +
                                     ":[{" + quoted("requestId") + ":1}]}";
    const std::string response_json =
        std::string("{") + quoted("availableChannelInfo") + ":[{" + quoted("globalOperatingClass") +
        ":131," + quoted("channelCfi") + ":[5]," + quoted("maxEirp") + ":[36]}]}";

    auto request_tlv = cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
    if (!check(request_tlv != nullptr, "add request TLV")) {
        return;
    }
    if (!check(request_tlv->alloc_available_spectrum_inquiry_request_obj(request_json.size()),
               "alloc request JSON")) {
        return;
    }
    std::copy_n(request_json.data(), request_json.size(),
                request_tlv->available_spectrum_inquiry_request_obj(0));

    auto response_tlv = cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryResponse>();
    if (!check(response_tlv != nullptr, "add response TLV")) {
        return;
    }
    if (!check(response_tlv->alloc_available_spectrum_inquiry_response_obj(response_json.size()),
               "alloc response JSON")) {
        return;
    }
    std::copy_n(response_json.data(), response_json.size(),
                response_tlv->available_spectrum_inquiry_response_obj(0));

    if (!check(cmdu_tx.finalize(), "finalize inquiry CMDU")) {
        return;
    }

    uint8_t rx_buffer[4096];
    std::memset(rx_buffer, 0, buf_size);
    std::memcpy(rx_buffer, tx_buffer, cmdu_tx.getMessageLength());

    ieee1905_1::CmduMessageRx cmdu_rx(rx_buffer, buf_size);
    if (!check(cmdu_rx.parse(), "parse inquiry CMDU")) {
        return;
    }
    check(cmdu_rx.getMessageType() == ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE,
          "inquiry message type");

    auto parsed_request = cmdu_rx.getClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
    if (!check(parsed_request != nullptr, "get request TLV")) {
        return;
    }
    check(parsed_request->type() == wfa_map::eTlvTypeMap::TLV_AVAILABLE_SPECTRUM_INQUIRY_REQUEST,
          "request TLV type");
    if (!check(parsed_request->available_spectrum_inquiry_request_obj_length() ==
                   request_json.size(),
               "request JSON length")) {
        return;
    }
    check(std::string(reinterpret_cast<const char *>(
                          parsed_request->available_spectrum_inquiry_request_obj(0)),
                      request_json.size()) == request_json,
          "request JSON payload");

    auto parsed_response = cmdu_rx.getClass<wfa_map::tlvAvailableSpectrumInquiryResponse>();
    if (!check(parsed_response != nullptr, "get response TLV")) {
        return;
    }
    check(parsed_response->type() == wfa_map::eTlvTypeMap::TLV_AVAILABLE_SPECTRUM_INQUIRY_RESPONSE,
          "response TLV type");
    if (!check(parsed_response->available_spectrum_inquiry_response_obj_length() ==
                   response_json.size(),
               "response JSON length")) {
        return;
    }
    check(std::string(reinterpret_cast<const char *>(
                          parsed_response->available_spectrum_inquiry_response_obj(0)),
                      response_json.size()) == response_json,
          "response JSON payload");
}

void channel_preference_reason_0xc_and_0xd_round_trip()
{
    const size_t buf_size = 2048;
    uint8_t tx_buffer[2048];
    std::memset(tx_buffer, 0, buf_size);
    ieee1905_1::CmduMessageTx cmdu_tx(tx_buffer, buf_size);

    if (!check(cmdu_tx.create(0, ieee1905_1::eMessageType::CHANNEL_PREFERENCE_REPORT_MESSAGE),
               "create CPR")) {
        return;
    }

    auto preference_tlv = cmdu_tx.addClass<wfa_map::tlvChannelPreference>();
    if (!check(preference_tlv != nullptr, "add Channel Preference TLV")) {
        return;
    }
    preference_tlv->radio_uid() = tlvf::mac_from_string("11:22:33:44:55:66");

    auto op_class = preference_tlv->create_operating_classes_list();
    if (!check(op_class != nullptr, "create op class 131")) {
        return;
    }
    op_class->operating_class() = 131;
    if (!check(op_class->alloc_channel_list(2), "alloc 2 channels")) {
        return;
    }
    *op_class->channel_list(0)    = 5;
    *op_class->channel_list(1)    = 21;
    op_class->flags().preference  = static_cast<uint8_t>(Pref::NON_OPERABLE);
    op_class->flags().reason_code = Reason::OPERATION_DISALLOWED_BY_REGULATORY_RESTRICTION;
    if (!check(preference_tlv->add_operating_classes_list(op_class), "add op class 131")) {
        return;
    }

    auto op_class_afc = preference_tlv->create_operating_classes_list();
    if (!check(op_class_afc != nullptr, "create op class 133")) {
        return;
    }
    op_class_afc->operating_class() = 133;
    if (!check(op_class_afc->alloc_channel_list(1), "alloc 1 channel")) {
        return;
    }
    *op_class_afc->channel_list(0)    = 7;
    op_class_afc->flags().preference  = static_cast<uint8_t>(Pref::NON_OPERABLE);
    op_class_afc->flags().reason_code = Reason::CHANGE_DUE_TO_AVAILABLE_SPECTRUM_INQUIRY_AFC;
    if (!check(preference_tlv->add_operating_classes_list(op_class_afc), "add op class 133")) {
        return;
    }

    if (!check(cmdu_tx.finalize(), "finalize CPR")) {
        return;
    }

    uint8_t rx_buffer[2048];
    std::memset(rx_buffer, 0, buf_size);
    std::memcpy(rx_buffer, tx_buffer, cmdu_tx.getMessageLength());
    ieee1905_1::CmduMessageRx cmdu_rx(rx_buffer, buf_size);
    if (!check(cmdu_rx.parse(), "parse CPR")) {
        return;
    }

    auto parsed = cmdu_rx.getClass<wfa_map::tlvChannelPreference>();
    if (!check(parsed != nullptr, "get Channel Preference TLV")) {
        return;
    }
    if (!check(parsed->operating_classes_list_length() == 2, "2 operating classes")) {
        return;
    }

    auto cls0_tuple = parsed->operating_classes_list(0);
    if (!check(std::get<0>(cls0_tuple), "op class 0 present")) {
        return;
    }
    auto &cls0 = std::get<1>(cls0_tuple);
    check(cls0.operating_class() == 131, "cls0 op class 131");
    check(cls0.flags().preference == static_cast<uint8_t>(Pref::NON_OPERABLE), "cls0 non-operable");
    check(cls0.flags().reason_code == Reason::OPERATION_DISALLOWED_BY_REGULATORY_RESTRICTION,
          "cls0 reason 0xC");
    if (!check(cls0.channel_list_length() == 2, "cls0 2 channels")) {
        return;
    }
    check(*cls0.channel_list(0) == 5, "cls0 ch 5");
    check(*cls0.channel_list(1) == 21, "cls0 ch 21");

    auto cls1_tuple = parsed->operating_classes_list(1);
    if (!check(std::get<0>(cls1_tuple), "op class 1 present")) {
        return;
    }
    auto &cls1 = std::get<1>(cls1_tuple);
    check(cls1.operating_class() == 133, "cls1 op class 133");
    check(cls1.flags().reason_code == Reason::CHANGE_DUE_TO_AVAILABLE_SPECTRUM_INQUIRY_AFC,
          "cls1 reason 0xD");
    if (!check(cls1.channel_list_length() == 1, "cls1 1 channel")) {
        return;
    }
    check(*cls1.channel_list(0) == 7, "cls1 ch 7");
}

void transmit_power_limit_tlv_round_trip()
{
    const size_t buf_size = 1024;
    uint8_t tx_buffer[1024];
    std::memset(tx_buffer, 0, buf_size);
    ieee1905_1::CmduMessageTx cmdu_tx(tx_buffer, buf_size);

    if (!check(cmdu_tx.create(3, ieee1905_1::eMessageType::CHANNEL_SELECTION_REQUEST_MESSAGE),
               "create CSR")) {
        return;
    }

    auto tx_limit = cmdu_tx.addClass<wfa_map::tlvTransmitPowerLimit>();
    if (!check(tx_limit != nullptr, "add Transmit Power Limit TLV")) {
        return;
    }
    tx_limit->radio_uid()                = tlvf::mac_from_string("aa:bb:cc:dd:ee:ff");
    tx_limit->transmit_power_limit_dbm() = 24;

    if (!check(cmdu_tx.finalize(), "finalize CSR")) {
        return;
    }

    uint8_t rx_buffer[1024];
    std::memset(rx_buffer, 0, buf_size);
    std::memcpy(rx_buffer, tx_buffer, cmdu_tx.getMessageLength());
    ieee1905_1::CmduMessageRx cmdu_rx(rx_buffer, buf_size);
    if (!check(cmdu_rx.parse(), "parse CSR")) {
        return;
    }

    auto parsed = cmdu_rx.getClass<wfa_map::tlvTransmitPowerLimit>();
    if (!check(parsed != nullptr, "get Transmit Power Limit TLV")) {
        return;
    }
    check(parsed->radio_uid() == tlvf::mac_from_string("aa:bb:cc:dd:ee:ff"), "radio uid");
    check(parsed->transmit_power_limit_dbm() == 24, "tx power limit 24");
}

} // namespace

int main()
{
    channel_preference_afc_reason_codes_match_spec();
    available_spectrum_inquiry_tlv_and_message_types();
    internal_afc_update_action_opcodes();
    round_trip_available_spectrum_inquiry_request_response();
    channel_preference_reason_0xc_and_0xd_round_trip();
    transmit_power_limit_tlv_round_trip();
    return g_fails == 0 ? 0 : 1;
}
