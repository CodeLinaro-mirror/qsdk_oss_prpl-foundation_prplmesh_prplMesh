/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../../son_actions.h"
#include "../dynamic_channel_selection_r2_task.h"
#include "db.h"

#include <tlvf/wfa_map/tlvProfile2CacStatusReport.h>

#include <ambiorix_mock.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

using ::testing::_;
using ::testing::Invoke;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::Return;

// The task sends an ACK for each Channel Preference Report. No agent is connected in the test.
bool son::son_actions::send_cmdu_to_agent(const sMacAddr &, ieee1905_1::CmduMessageTx &, db &,
                                          const std::string &)
{
    return true;
}

namespace {

constexpr auto g_agent_dm_path = "Device.WiFi.DataElements.Network.Device.2";

struct sCacStatusReport {
    std::vector<wfa_map::tlvProfile2CacStatusReport::sAvailableChannels> available;
    std::vector<wfa_map::tlvProfile2CacStatusReport::sDetectedPairs> non_occupancy;
    std::vector<wfa_map::tlvProfile2CacStatusReport::sActiveCacPairs> active;
};

class DynamicChannelSelectionR2TaskTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        beerocks::config_file::sConfigMaster beerocks_master_conf;
        m_logger = std::make_unique<beerocks::logging>("dynamic_channel_selection_r2_task_test",
                                                       beerocks_master_conf.sLog);
        m_logger->set_log_level_state(beerocks::LOG_LEVEL_ERROR, true);

        m_ambiorix = std::make_shared<NiceMock<beerocks::nbapi::AmbiorixMock>>();
        ON_CALL(*m_ambiorix, add_instance(_)).WillByDefault(Invoke([](const std::string &path) {
            return path + ".1";
        }));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const uint16_t &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const uint32_t &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set_current_time(_, _)).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, remove_all_instances(_)).WillByDefault(Return(true));

        // Record the channel of each CAC list entry, and count the CACStatus instances.
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const uint8_t &>(_)))
            .WillByDefault(Invoke(
                [this](const std::string &path, const std::string &param, const uint8_t &value) {
                    if (param == "Channel") {
                        // path is <CACStatus instance>.<list>.<index>
                        const auto list_path = path.substr(0, path.rfind('.'));
                        m_channels[list_path.substr(list_path.rfind('.') + 1)].push_back(value);
                    }
                    return true;
                }));
        const std::string cac_status_path = std::string(g_agent_dm_path) + ".CACStatus";
        ON_CALL(*m_ambiorix, add_instance(cac_status_path))
            .WillByDefault(Invoke([this, cac_status_path](const std::string &) {
                m_channels.clear();
                ++m_cac_status_instances;
                return cac_status_path + "." + std::to_string(++m_cac_status_index);
            }));
        ON_CALL(*m_ambiorix, remove_all_instances(cac_status_path))
            .WillByDefault(Invoke([this](const std::string &) {
                m_cac_status_instances = 0;
                return true;
            }));

        son::db::sDbMasterConfig master_conf;
        m_database = std::make_unique<son::db>(
            master_conf, *m_logger, tlvf::mac_from_string("02:00:00:00:00:01"), m_ambiorix);

        m_agent = m_database->add_agent(m_agent_mac);
        ASSERT_NE(m_agent, nullptr);
        m_agent->dm_path = g_agent_dm_path;
        m_agent->profile = wfa_map::tlvProfile2MultiApProfile::eMultiApProfile::MULTIAP_PROFILE_2;

        m_cmdu_tx = std::make_unique<ieee1905_1::CmduMessageTx>(m_tx_buffer, sizeof(m_tx_buffer));
        m_task_cmdu_tx =
            std::make_unique<ieee1905_1::CmduMessageTx>(m_task_tx_buffer, sizeof(m_task_tx_buffer));
        m_task = std::make_unique<son::dynamic_channel_selection_r2_task>(*m_database,
                                                                          *m_task_cmdu_tx, m_tasks);
    }

    bool send_channel_preference_report(const sCacStatusReport &report)
    {
        if (!m_cmdu_tx->create(0, ieee1905_1::eMessageType::CHANNEL_PREFERENCE_REPORT_MESSAGE)) {
            return false;
        }

        auto tlv = m_cmdu_tx->addClass<wfa_map::tlvProfile2CacStatusReport>();
        if (!tlv) {
            return false;
        }
        if (!report.available.empty() && !tlv->alloc_available_channels(report.available.size())) {
            return false;
        }
        for (size_t i = 0; i < report.available.size(); i++) {
            std::get<1>(tlv->available_channels(i)) = report.available[i];
        }
        if (!report.non_occupancy.empty() &&
            !tlv->alloc_detected_pairs(report.non_occupancy.size())) {
            return false;
        }
        for (size_t i = 0; i < report.non_occupancy.size(); i++) {
            std::get<1>(tlv->detected_pairs(i)) = report.non_occupancy[i];
        }
        if (!report.active.empty() && !tlv->alloc_active_cac_pairs(report.active.size())) {
            return false;
        }
        for (size_t i = 0; i < report.active.size(); i++) {
            std::get<1>(tlv->active_cac_pairs(i)) = report.active[i];
        }

        if (!m_cmdu_tx->finalize()) {
            return false;
        }
        std::copy_n(m_tx_buffer, m_cmdu_tx->getMessageLength(), m_rx_buffer);

        ieee1905_1::CmduMessageRx cmdu_rx(m_rx_buffer, sizeof(m_rx_buffer));
        if (!cmdu_rx.parse()) {
            return false;
        }
        return m_task->handle_ieee1905_1_msg(m_agent_mac, cmdu_rx);
    }

    std::unique_ptr<beerocks::logging> m_logger;
    std::shared_ptr<NiceMock<beerocks::nbapi::AmbiorixMock>> m_ambiorix;
    std::unique_ptr<son::db> m_database;
    std::shared_ptr<prplmesh::controller::db::Agent> m_agent;
    std::unique_ptr<ieee1905_1::CmduMessageTx> m_cmdu_tx;
    std::unique_ptr<ieee1905_1::CmduMessageTx> m_task_cmdu_tx;
    son::task_pool m_tasks;
    std::unique_ptr<son::dynamic_channel_selection_r2_task> m_task;
    const sMacAddr m_agent_mac = tlvf::mac_from_string("02:00:00:00:00:02");
    // Channels written to each list of the latest CACStatus instance, by list name.
    std::map<std::string, std::vector<uint8_t>> m_channels;
    size_t m_cac_status_instances                                      = 0;
    uint32_t m_cac_status_index                                        = 0;
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH]      = {};
    uint8_t m_task_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH] = {};
    uint8_t m_rx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH]      = {};
};

// cppcheck-suppress syntaxError
TEST_F(DynamicChannelSelectionR2TaskTest, cac_status_report_keeps_non_dfs_available_channels)
{
    sCacStatusReport report;
    report.available     = {{115, 36, 0}, {118, 52, 5}};
    report.non_occupancy = {{115, 40, 100}, {121, 100, 1800}};
    report.active        = {{118, 56, {0, 0, 60}}, {81, 6, {0, 0, 60}}};
    ASSERT_TRUE(send_channel_preference_report(report));

    EXPECT_EQ(1U, m_cac_status_instances);
    // Available: the non-DFS channel 36 is kept.
    EXPECT_EQ(std::vector<uint8_t>({36, 52}), m_channels["CACAvailableChannel"]);
    // Non-occupancy and active: the non-DFS pairs are dropped.
    EXPECT_EQ(std::vector<uint8_t>({100}), m_channels["CACNonOccupancyChannel"]);
    EXPECT_EQ(std::vector<uint8_t>({56}), m_channels["CACActiveChannel"]);
}

TEST_F(DynamicChannelSelectionR2TaskTest, cac_status_report_replaces_previous_report)
{
    sCacStatusReport report;
    report.available = {{115, 36, 0}};
    ASSERT_TRUE(send_channel_preference_report(report));
    EXPECT_EQ(1U, m_cac_status_instances);

    // A report with all lists empty.
    ASSERT_TRUE(send_channel_preference_report(sCacStatusReport{}));
    EXPECT_EQ(1U, m_cac_status_instances);

    ASSERT_TRUE(send_channel_preference_report(report));
    EXPECT_EQ(1U, m_cac_status_instances);
    EXPECT_EQ(3U, m_cac_status_index);
}

} // namespace
