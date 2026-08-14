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

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace {

std::string quoted(const std::string &key) { return std::string("\x22") + key + "\x22"; }

using Reason = wfa_map::cPreferenceOperatingClasses::eReasonCode;
using Pref   = wfa_map::cPreferenceOperatingClasses::ePreference;

} // namespace

namespace son {
namespace tests {

// cppcheck-suppress syntaxError
TEST(afc_easymesh_tlv, channel_preference_afc_reason_codes_match_spec)
{
    EXPECT_EQ(static_cast<int>(Reason::CONTROLLER_DFS_CHANNEL_CLEAR_INDICATION), 11);
    EXPECT_EQ(static_cast<int>(Reason::OPERATION_DISALLOWED_BY_REGULATORY_RESTRICTION), 12);
    EXPECT_EQ(static_cast<int>(Reason::CHANGE_DUE_TO_AVAILABLE_SPECTRUM_INQUIRY_AFC), 13);
}

TEST(afc_easymesh_tlv, available_spectrum_inquiry_tlv_and_message_types)
{
    EXPECT_EQ(static_cast<int>(wfa_map::eTlvTypeMap::TLV_AVAILABLE_SPECTRUM_INQUIRY_REQUEST), 232);
    EXPECT_EQ(static_cast<int>(wfa_map::eTlvTypeMap::TLV_AVAILABLE_SPECTRUM_INQUIRY_RESPONSE), 233);
    EXPECT_EQ(static_cast<int>(ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE),
              32841);
}

TEST(afc_easymesh_tlv, internal_afc_update_action_opcodes)
{
    EXPECT_EQ(static_cast<int>(beerocks_message::ACTION_APMANAGER_AFC_UPDATE_NOTIFICATION), 83);
    EXPECT_EQ(static_cast<int>(beerocks_message::ACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION), 89);
    EXPECT_LT(beerocks_message::ACTION_APMANAGER_AFC_UPDATE_NOTIFICATION,
              beerocks_message::ACTION_APMANAGER_ENUM_END);
    EXPECT_LT(beerocks_message::ACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION,
              beerocks_message::ACTION_BACKHAUL_ENUM_END);
}

TEST(afc_easymesh_tlv, round_trip_available_spectrum_inquiry_request_response)
{
    const size_t buf_size = 4096;
    uint8_t tx_buffer[4096];
    std::memset(tx_buffer, 0, buf_size);
    ieee1905_1::CmduMessageTx cmdu_tx(tx_buffer, buf_size);

    ASSERT_NE(cmdu_tx.create(7, ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE),
              nullptr);

    const std::string request_json = std::string("{") + quoted("availableSpectrumInquiryRequests") +
                                     ":[{" + quoted("requestId") + ":1}]}";
    const std::string response_json =
        std::string("{") + quoted("availableChannelInfo") + ":[{" + quoted("globalOperatingClass") +
        ":131," + quoted("channelCfi") + ":[5]," + quoted("maxEirp") + ":[36]}]}";

    auto request_tlv = cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
    ASSERT_NE(request_tlv, nullptr);
    ASSERT_TRUE(request_tlv->alloc_available_spectrum_inquiry_request_obj(request_json.size()));
    std::copy_n(request_json.data(), request_json.size(),
                request_tlv->available_spectrum_inquiry_request_obj(0));

    auto response_tlv = cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryResponse>();
    ASSERT_NE(response_tlv, nullptr);
    ASSERT_TRUE(response_tlv->alloc_available_spectrum_inquiry_response_obj(response_json.size()));
    std::copy_n(response_json.data(), response_json.size(),
                response_tlv->available_spectrum_inquiry_response_obj(0));

    ASSERT_TRUE(cmdu_tx.finalize());

    uint8_t rx_buffer[4096];
    std::memset(rx_buffer, 0, buf_size);
    std::memcpy(rx_buffer, tx_buffer, cmdu_tx.getMessageLength());

    ieee1905_1::CmduMessageRx cmdu_rx(rx_buffer, buf_size);
    ASSERT_TRUE(cmdu_rx.parse());
    EXPECT_EQ(cmdu_rx.getMessageType(),
              ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE);

    auto parsed_request = cmdu_rx.getClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
    ASSERT_NE(parsed_request, nullptr);
    EXPECT_EQ(parsed_request->type(), wfa_map::eTlvTypeMap::TLV_AVAILABLE_SPECTRUM_INQUIRY_REQUEST);
    ASSERT_EQ(parsed_request->available_spectrum_inquiry_request_obj_length(), request_json.size());
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(
                              parsed_request->available_spectrum_inquiry_request_obj(0)),
                          request_json.size()),
              request_json);

    auto parsed_response = cmdu_rx.getClass<wfa_map::tlvAvailableSpectrumInquiryResponse>();
    ASSERT_NE(parsed_response, nullptr);
    EXPECT_EQ(parsed_response->type(),
              wfa_map::eTlvTypeMap::TLV_AVAILABLE_SPECTRUM_INQUIRY_RESPONSE);
    ASSERT_EQ(parsed_response->available_spectrum_inquiry_response_obj_length(),
              response_json.size());
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(
                              parsed_response->available_spectrum_inquiry_response_obj(0)),
                          response_json.size()),
              response_json);
}

TEST(afc_easymesh_tlv, channel_preference_reason_0xc_and_0xd_round_trip)
{
    const size_t buf_size = 2048;
    uint8_t tx_buffer[2048];
    std::memset(tx_buffer, 0, buf_size);
    ieee1905_1::CmduMessageTx cmdu_tx(tx_buffer, buf_size);

    ASSERT_NE(cmdu_tx.create(0, ieee1905_1::eMessageType::CHANNEL_PREFERENCE_REPORT_MESSAGE),
              nullptr);

    auto preference_tlv = cmdu_tx.addClass<wfa_map::tlvChannelPreference>();
    ASSERT_NE(preference_tlv, nullptr);
    preference_tlv->radio_uid() = tlvf::mac_from_string("11:22:33:44:55:66");

    auto op_class = preference_tlv->create_operating_classes_list();
    ASSERT_NE(op_class, nullptr);
    op_class->operating_class() = 131;
    ASSERT_TRUE(op_class->alloc_channel_list(2));
    *op_class->channel_list(0)    = 5;
    *op_class->channel_list(1)    = 21;
    op_class->flags().preference  = static_cast<uint8_t>(Pref::NON_OPERABLE);
    op_class->flags().reason_code = Reason::OPERATION_DISALLOWED_BY_REGULATORY_RESTRICTION;
    ASSERT_TRUE(preference_tlv->add_operating_classes_list(op_class));

    auto op_class_afc = preference_tlv->create_operating_classes_list();
    ASSERT_NE(op_class_afc, nullptr);
    op_class_afc->operating_class() = 133;
    ASSERT_TRUE(op_class_afc->alloc_channel_list(1));
    *op_class_afc->channel_list(0)    = 7;
    op_class_afc->flags().preference  = static_cast<uint8_t>(Pref::NON_OPERABLE);
    op_class_afc->flags().reason_code = Reason::CHANGE_DUE_TO_AVAILABLE_SPECTRUM_INQUIRY_AFC;
    ASSERT_TRUE(preference_tlv->add_operating_classes_list(op_class_afc));

    ASSERT_TRUE(cmdu_tx.finalize());

    uint8_t rx_buffer[2048];
    std::memset(rx_buffer, 0, buf_size);
    std::memcpy(rx_buffer, tx_buffer, cmdu_tx.getMessageLength());
    ieee1905_1::CmduMessageRx cmdu_rx(rx_buffer, buf_size);
    ASSERT_TRUE(cmdu_rx.parse());

    auto parsed = cmdu_rx.getClass<wfa_map::tlvChannelPreference>();
    ASSERT_NE(parsed, nullptr);
    ASSERT_EQ(parsed->operating_classes_list_length(), 2U);

    auto cls0_tuple = parsed->operating_classes_list(0);
    ASSERT_TRUE(std::get<0>(cls0_tuple));
    auto &cls0 = std::get<1>(cls0_tuple);
    EXPECT_EQ(cls0.operating_class(), 131);
    EXPECT_EQ(cls0.flags().preference, static_cast<uint8_t>(Pref::NON_OPERABLE));
    EXPECT_EQ(cls0.flags().reason_code, Reason::OPERATION_DISALLOWED_BY_REGULATORY_RESTRICTION);
    ASSERT_EQ(cls0.channel_list_length(), 2U);
    EXPECT_EQ(*cls0.channel_list(0), 5);
    EXPECT_EQ(*cls0.channel_list(1), 21);

    auto cls1_tuple = parsed->operating_classes_list(1);
    ASSERT_TRUE(std::get<0>(cls1_tuple));
    auto &cls1 = std::get<1>(cls1_tuple);
    EXPECT_EQ(cls1.operating_class(), 133);
    EXPECT_EQ(cls1.flags().reason_code, Reason::CHANGE_DUE_TO_AVAILABLE_SPECTRUM_INQUIRY_AFC);
    ASSERT_EQ(cls1.channel_list_length(), 1U);
    EXPECT_EQ(*cls1.channel_list(0), 7);
}

TEST(afc_easymesh_tlv, transmit_power_limit_tlv_round_trip)
{
    const size_t buf_size = 1024;
    uint8_t tx_buffer[1024];
    std::memset(tx_buffer, 0, buf_size);
    ieee1905_1::CmduMessageTx cmdu_tx(tx_buffer, buf_size);

    ASSERT_NE(cmdu_tx.create(3, ieee1905_1::eMessageType::CHANNEL_SELECTION_REQUEST_MESSAGE),
              nullptr);

    auto tx_limit = cmdu_tx.addClass<wfa_map::tlvTransmitPowerLimit>();
    ASSERT_NE(tx_limit, nullptr);
    tx_limit->radio_uid()                = tlvf::mac_from_string("aa:bb:cc:dd:ee:ff");
    tx_limit->transmit_power_limit_dbm() = 24;

    ASSERT_TRUE(cmdu_tx.finalize());

    uint8_t rx_buffer[1024];
    std::memset(rx_buffer, 0, buf_size);
    std::memcpy(rx_buffer, tx_buffer, cmdu_tx.getMessageLength());
    ieee1905_1::CmduMessageRx cmdu_rx(rx_buffer, buf_size);
    ASSERT_TRUE(cmdu_rx.parse());

    auto parsed = cmdu_rx.getClass<wfa_map::tlvTransmitPowerLimit>();
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->radio_uid(), tlvf::mac_from_string("aa:bb:cc:dd:ee:ff"));
    EXPECT_EQ(parsed->transmit_power_limit_dbm(), 24);
}

} // namespace tests
} // namespace son
