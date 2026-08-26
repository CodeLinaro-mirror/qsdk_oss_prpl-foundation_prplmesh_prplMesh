/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../client_association_task.h"
#include "db.h"

#include <bcl/beerocks_utils.h>
#include <tlvf/wfa_map/tlvClientAssociationEvent.h>
#include <tlvf/wfa_map/tlvClientCapabilityReport.h>
#include <tlvf/wfa_map/tlvClientInfo.h>

#include <ambiorix_mock.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

namespace {

constexpr auto g_join_time   = "2026-11-12T10:00:00.000000000Z";
constexpr auto g_later_time  = "2026-11-12T10:00:05.000000000Z";
constexpr auto g_rejoin_time = "2026-11-12T10:00:10.000000000Z";

/*
 * An Association Request frame body with HT, VHT and HE Capabilities elements. The Supported
 * HE-MCS And NSS Set field is fa ff fa ff (80 MHz only).
 */
const std::vector<uint8_t> g_assoc_req_frame_body = {
    0x11, 0x11, 0x0a, 0x00, 0x00, 0x08, 0x62, 0x64, 0x6b, 0x5f, 0x31, 0x32, 0x33, 0x34, 0x01, 0x08,
    0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c, 0x21, 0x02, 0xf7, 0x11, 0x24, 0x0a, 0x24, 0x04,
    0x34, 0x04, 0x64, 0x0b, 0x95, 0x04, 0xa5, 0x01, 0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
    0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x80, 0x00, 0x46, 0x05,
    0x71, 0x08, 0x01, 0x00, 0x00, 0x3b, 0x15, 0x80, 0x70, 0x73, 0x74, 0x75, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x51, 0x53, 0x54, 0x2d, 0x1a, 0x6f, 0x00,
    0x1b, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x0a, 0x00, 0x00, 0x08, 0x80, 0x00, 0x00,
    0x00, 0x40, 0x00, 0x20, 0xbf, 0x0c, 0x32, 0x78, 0x91, 0x0f, 0xfa, 0xff, 0x00, 0x00, 0xfa, 0xff,
    0x00, 0x00, 0xff, 0x1c, 0x23, 0x03, 0x08, 0x00, 0x00, 0x00, 0x80, 0x64, 0x30, 0x00, 0x00, 0x0d,
    0x00, 0x9f, 0x00, 0x0c, 0x00, 0x00, 0xfa, 0xff, 0xfa, 0xff, 0x39, 0x1c, 0xc7, 0x71, 0x1c, 0x07,
    0xdd, 0x0b, 0x00, 0x00, 0xf0, 0x22, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x03, 0xdd, 0x05, 0x00,
    0x90, 0x4c, 0x04, 0x17, 0xdd, 0x0a, 0x00, 0x10, 0x18, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00, 0xdd, 0x09, 0x50, 0x6f, 0x9a, 0x16, 0x02,
    0x00, 0x03, 0x01, 0x01};

template <typename T>
const T *find_event_arg(const std::vector<std::pair<std::string, T>> &args, const std::string &name)
{
    for (const auto &arg : args) {
        if (arg.first == name) {
            return &arg.second;
        }
    }
    return nullptr;
}

class ClientAssociationTaskTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        beerocks::config_file::sConfigMaster beerocks_master_conf;
        m_logger = std::make_unique<beerocks::logging>("client_association_task_test",
                                                       beerocks_master_conf.sLog);
        m_logger->set_log_level_state(beerocks::LOG_LEVEL_ERROR, true);

        m_ambiorix = std::make_shared<NiceMock<beerocks::nbapi::AmbiorixMock>>();
        ON_CALL(*m_ambiorix, add_instance(_)).WillByDefault(Invoke([](const std::string &path) {
            return path + ".1";
        }));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const std::string &>(_)))
            .WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const sMacAddr &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const bool &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const uint8_t &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const uint16_t &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const uint32_t &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const uint64_t &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set_current_time(_, _)).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, add_optional_subobject(_, _)).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, remove_optional_subobject(_, _)).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, get_datamodel_time_format()).WillByDefault(Return(g_join_time));
        // No emit is expected unless a test says so.
        EXPECT_CALL(*m_ambiorix, emit_event(_, _, _)).Times(0);

        son::db::sDbMasterConfig master_conf;
        m_database = std::make_unique<son::db>(
            master_conf, *m_logger, tlvf::mac_from_string("02:00:00:00:00:01"), m_ambiorix);

        ASSERT_NE(m_database->add_agent(m_agent_mac), nullptr);
        ASSERT_TRUE(m_database->add_radio(m_radio_mac, m_agent_mac));
        auto radio = m_database->get_radio(m_agent_mac, m_radio_mac);
        ASSERT_NE(radio, nullptr);
        ASSERT_TRUE(m_database->set_radio_wifi_channel(
            m_radio_mac, beerocks::WifiChannel(36, beerocks::FREQ_5G, beerocks::BANDWIDTH_80),
            "test"));
        ASSERT_NE(m_database->add_bss(*radio, m_bssid, "ssid", 0), nullptr);
        ASSERT_NE(m_database->add_station(m_agent_mac, m_sta_mac, m_bssid), nullptr);

        m_cmdu_tx = std::make_unique<ieee1905_1::CmduMessageTx>(m_tx_buffer, sizeof(m_tx_buffer));
        m_task = std::make_unique<son::client_association_task>(*m_database, *m_cmdu_tx, m_tasks);
    }

    /**
     * Replaces the task with one that waits @a timeout for the Client Capability Report.
     */
    void set_association_event_timeout(std::chrono::milliseconds timeout)
    {
        m_task = std::make_unique<son::client_association_task>(*m_database, *m_cmdu_tx, m_tasks,
                                                                timeout);
    }

    void expect_basic_arguments(const beerocks::nbapi::EventArgs &args,
                                const std::string &join_time)
    {
        ASSERT_NE(find_event_arg(args.strings, "MACAddress"), nullptr);
        EXPECT_EQ(*find_event_arg(args.strings, "MACAddress"), tlvf::mac_to_string(m_sta_mac));
        ASSERT_NE(find_event_arg(args.strings, "BSSID"), nullptr);
        EXPECT_EQ(*find_event_arg(args.strings, "BSSID"), tlvf::mac_to_string(m_bssid));
        ASSERT_NE(find_event_arg(args.uint32s, "StatusCode"), nullptr);
        ASSERT_NE(find_event_arg(args.datetimes, "TimeStamp"), nullptr);
        EXPECT_EQ(*find_event_arg(args.datetimes, "TimeStamp"), join_time);
        ASSERT_NE(find_event_arg(args.uint32s, "OpClass"), nullptr);
        ASSERT_NE(find_event_arg(args.uint32s, "Channel"), nullptr);

        EXPECT_EQ(find_event_arg(args.strings, "ClientCapabilities"), nullptr);
        EXPECT_TRUE(args.booleans.empty());
        EXPECT_TRUE(args.uint16s.empty());
    }

    bool to_rx(ieee1905_1::CmduMessageRx &cmdu_rx)
    {
        if (!m_cmdu_tx->finalize()) {
            return false;
        }
        std::copy_n(m_tx_buffer, m_cmdu_tx->getMessageLength(), m_rx_buffer);
        return cmdu_rx.parse();
    }

    bool join(const std::string &time)
    {
        ON_CALL(*m_ambiorix, get_datamodel_time_format()).WillByDefault(Return(time));
        if (!m_cmdu_tx->create(0, ieee1905_1::eMessageType::TOPOLOGY_NOTIFICATION_MESSAGE)) {
            return false;
        }
        auto tlv = m_cmdu_tx->addClass<wfa_map::tlvClientAssociationEvent>();
        if (!tlv) {
            return false;
        }
        tlv->client_mac()        = m_sta_mac;
        tlv->bssid()             = m_bssid;
        tlv->association_event() = wfa_map::tlvClientAssociationEvent::CLIENT_HAS_JOINED_THE_BSS;

        ieee1905_1::CmduMessageRx cmdu_rx(m_rx_buffer, sizeof(m_rx_buffer));
        return to_rx(cmdu_rx) && m_task->handle_ieee1905_1_msg(m_agent_mac, cmdu_rx);
    }

    bool report(bool success)
    {
        if (!m_cmdu_tx->create(0, ieee1905_1::eMessageType::CLIENT_CAPABILITY_REPORT_MESSAGE)) {
            return false;
        }
        auto info_tlv = m_cmdu_tx->addClass<wfa_map::tlvClientInfo>();
        if (!info_tlv) {
            return false;
        }
        info_tlv->bssid()      = m_bssid;
        info_tlv->client_mac() = m_sta_mac;

        auto report_tlv = m_cmdu_tx->addClass<wfa_map::tlvClientCapabilityReport>();
        if (!report_tlv) {
            return false;
        }
        report_tlv->result_code() = success ? wfa_map::tlvClientCapabilityReport::SUCCESS
                                            : wfa_map::tlvClientCapabilityReport::FAILURE;
        if (success && !report_tlv->set_association_frame(g_assoc_req_frame_body.data(),
                                                          g_assoc_req_frame_body.size())) {
            return false;
        }

        ieee1905_1::CmduMessageRx cmdu_rx(m_rx_buffer, sizeof(m_rx_buffer));
        return to_rx(cmdu_rx) && m_task->handle_ieee1905_1_msg(m_agent_mac, cmdu_rx);
    }

    void expect_associated(beerocks::nbapi::EventArgs &args)
    {
        EXPECT_CALL(*m_ambiorix, emit_event(std::string(DATAELEMENTS_ROOT_DM ".AssociationEvent"),
                                            "Associated!", _))
            .WillOnce(DoAll(SaveArg<2>(&args), Return(true)));
    }

    const sMacAddr m_agent_mac = tlvf::mac_from_string("02:00:00:00:00:02");
    const sMacAddr m_radio_mac = tlvf::mac_from_string("02:00:00:00:00:03");
    const sMacAddr m_bssid     = tlvf::mac_from_string("02:00:00:00:00:04");
    const sMacAddr m_sta_mac   = tlvf::mac_from_string("02:00:00:00:00:05");

    std::unique_ptr<beerocks::logging> m_logger;
    std::shared_ptr<NiceMock<beerocks::nbapi::AmbiorixMock>> m_ambiorix;
    std::unique_ptr<son::db> m_database;
    son::task_pool m_tasks;
    std::unique_ptr<ieee1905_1::CmduMessageTx> m_cmdu_tx;
    std::unique_ptr<son::client_association_task> m_task;
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH] = {};
    uint8_t m_rx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH] = {};
};

// cppcheck-suppress syntaxError
TEST_F(ClientAssociationTaskTest, join_alone_emits_no_associated_event_before_the_timeout)
{
    EXPECT_TRUE(join(g_join_time));
    m_task->execute();
}

TEST_F(ClientAssociationTaskTest, report_before_the_timeout_emits_associated_with_the_capabilities)
{
    ASSERT_TRUE(join(g_join_time));

    // Record what the STA object gets; let the other writes through.
    EXPECT_CALL(*m_ambiorix, set(_, _, Matcher<const std::string &>(_)))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_ambiorix, set(_, _, Matcher<const bool &>(_))).WillRepeatedly(Return(true));
    EXPECT_CALL(*m_ambiorix, set(_, _, Matcher<const uint16_t &>(_))).WillRepeatedly(Return(true));
    std::string sta_client_capabilities;
    EXPECT_CALL(*m_ambiorix, set(_, "ClientCapabilities", Matcher<const std::string &>(_)))
        .WillRepeatedly(DoAll(SaveArg<2>(&sta_client_capabilities), Return(true)));
    bool sta_ht40 = false;
    EXPECT_CALL(*m_ambiorix,
                set(::testing::HasSubstr("HTCapabilities."), "HT40", Matcher<const bool &>(_)))
        .WillRepeatedly(DoAll(SaveArg<2>(&sta_ht40), Return(true)));
    uint16_t sta_vht_mcs = 0;
    EXPECT_CALL(*m_ambiorix, set(::testing::HasSubstr("VHTCapabilities."), "MCSNSSTxSet",
                                 Matcher<const uint16_t &>(_)))
        .WillRepeatedly(DoAll(SaveArg<2>(&sta_vht_mcs), Return(true)));

    beerocks::nbapi::EventArgs args;
    expect_associated(args);
    ON_CALL(*m_ambiorix, get_datamodel_time_format()).WillByDefault(Return(g_later_time));
    EXPECT_TRUE(report(true));

    ASSERT_NE(find_event_arg(args.strings, "MACAddress"), nullptr);
    EXPECT_EQ(*find_event_arg(args.strings, "MACAddress"), tlvf::mac_to_string(m_sta_mac));
    ASSERT_NE(find_event_arg(args.strings, "BSSID"), nullptr);
    EXPECT_EQ(*find_event_arg(args.strings, "BSSID"), tlvf::mac_to_string(m_bssid));
    // The time of the join, not of the report.
    ASSERT_NE(find_event_arg(args.datetimes, "TimeStamp"), nullptr);
    EXPECT_EQ(*find_event_arg(args.datetimes, "TimeStamp"), g_join_time);
    ASSERT_NE(find_event_arg(args.uint32s, "Channel"), nullptr);
    EXPECT_EQ(*find_event_arg(args.uint32s, "Channel"), 36U);

    // The same content as the STA object gets.
    ASSERT_NE(find_event_arg(args.strings, "ClientCapabilities"), nullptr);
    EXPECT_EQ(*find_event_arg(args.strings, "ClientCapabilities"), sta_client_capabilities);
    EXPECT_EQ(
        *find_event_arg(args.strings, "ClientCapabilities"),
        beerocks::utils::dump_buffer(g_assoc_req_frame_body.data(), g_assoc_req_frame_body.size()));
    // HT: 2 spatial streams, short GI 20 and 40 MHz, HT 40 MHz.
    ASSERT_NE(find_event_arg(args.booleans, "HTCapabilities.HT40"), nullptr);
    EXPECT_TRUE(*find_event_arg(args.booleans, "HTCapabilities.HT40"));
    EXPECT_EQ(*find_event_arg(args.booleans, "HTCapabilities.HT40"), sta_ht40);
    ASSERT_NE(find_event_arg(args.uint32s, "HTCapabilities.MaxNumberOfRxSpatialStreams"), nullptr);
    EXPECT_EQ(*find_event_arg(args.uint32s, "HTCapabilities.MaxNumberOfRxSpatialStreams"), 2U);
    // VHT: MCS 0-9 on 2 spatial streams.
    ASSERT_NE(find_event_arg(args.uint16s, "VHTCapabilities.MCSNSSTxSet"), nullptr);
    EXPECT_EQ(*find_event_arg(args.uint16s, "VHTCapabilities.MCSNSSTxSet"), 0xfffa);
    EXPECT_EQ(*find_event_arg(args.uint16s, "VHTCapabilities.MCSNSSTxSet"), sta_vht_mcs);
    ASSERT_NE(find_event_arg(args.booleans, "VHTCapabilities.SUBeamformer"), nullptr);
    EXPECT_TRUE(*find_event_arg(args.booleans, "VHTCapabilities.SUBeamformer"));
    // HE: 80 MHz.
    ASSERT_NE(find_event_arg(args.booleans, "WiFi6Capabilities.HE160"), nullptr);
    EXPECT_FALSE(*find_event_arg(args.booleans, "WiFi6Capabilities.HE160"));
    ASSERT_NE(find_event_arg(args.uint32s, "WiFi6Capabilities.MaxNumberOfTxSpatialStreams"),
              nullptr);

    // A second report for the same join emits nothing, and the timeout emits nothing.
    EXPECT_TRUE(report(true));
    m_task->execute();
}

TEST_F(ClientAssociationTaskTest, failed_report_emits_associated_with_the_basic_arguments)
{
    ASSERT_TRUE(join(g_join_time));

    beerocks::nbapi::EventArgs args;
    expect_associated(args);
    EXPECT_TRUE(report(false));

    expect_basic_arguments(args, g_join_time);
}

TEST_F(ClientAssociationTaskTest, no_report_emits_associated_with_the_basic_arguments_at_timeout)
{
    set_association_event_timeout(std::chrono::milliseconds(0));
    ASSERT_TRUE(join(g_join_time));

    beerocks::nbapi::EventArgs args;
    expect_associated(args);
    ON_CALL(*m_ambiorix, get_datamodel_time_format()).WillByDefault(Return(g_later_time));
    m_task->execute();

    expect_basic_arguments(args, g_join_time);

    // The timeout of a join emits once.
    m_task->execute();
}

TEST_F(ClientAssociationTaskTest, report_after_the_timeout_updates_the_sta_and_emits_nothing)
{
    set_association_event_timeout(std::chrono::milliseconds(0));
    ASSERT_TRUE(join(g_join_time));

    beerocks::nbapi::EventArgs args;
    expect_associated(args);
    m_task->execute();

    std::string sta_client_capabilities;
    EXPECT_CALL(*m_ambiorix, set(_, _, Matcher<const std::string &>(_)))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_ambiorix, set(_, "ClientCapabilities", Matcher<const std::string &>(_)))
        .WillRepeatedly(DoAll(SaveArg<2>(&sta_client_capabilities), Return(true)));
    EXPECT_TRUE(report(true));

    EXPECT_EQ(sta_client_capabilities, beerocks::utils::dump_buffer(g_assoc_req_frame_body.data(),
                                                                    g_assoc_req_frame_body.size()));
}

TEST_F(ClientAssociationTaskTest,
       disconnect_before_the_timeout_emits_basic_associated_before_disassociated)
{
    set_association_event_timeout(std::chrono::milliseconds(0));
    ASSERT_TRUE(join(g_join_time));

    beerocks::nbapi::EventArgs args;
    {
        InSequence sequence;
        expect_associated(args);
        EXPECT_CALL(*m_ambiorix,
                    emit_event(std::string(DATAELEMENTS_ROOT_DM ".DisassociationEvent"),
                               "Disassociated!", _))
            .WillOnce(Return(true));
    }
    EXPECT_TRUE(m_database->dm_emit_disassociation_event(m_sta_mac, 0, m_bssid));

    expect_basic_arguments(args, g_join_time);

    // The timeout and a late report of the join emit nothing.
    m_task->execute();
    EXPECT_TRUE(report(true));
}

TEST_F(ClientAssociationTaskTest, new_join_emits_basic_associated_of_the_previous_join)
{
    ASSERT_TRUE(join(g_join_time));

    beerocks::nbapi::EventArgs args;
    expect_associated(args);
    ASSERT_TRUE(join(g_rejoin_time));

    expect_basic_arguments(args, g_join_time);

    // The report of the new join emits its event.
    beerocks::nbapi::EventArgs rejoin_args;
    expect_associated(rejoin_args);
    EXPECT_TRUE(report(true));

    ASSERT_NE(find_event_arg(rejoin_args.datetimes, "TimeStamp"), nullptr);
    EXPECT_EQ(*find_event_arg(rejoin_args.datetimes, "TimeStamp"), g_rejoin_time);
    EXPECT_NE(find_event_arg(rejoin_args.strings, "ClientCapabilities"), nullptr);
}

TEST_F(ClientAssociationTaskTest, report_without_a_join_emits_nothing)
{
    EXPECT_TRUE(report(true));
}

} // namespace
