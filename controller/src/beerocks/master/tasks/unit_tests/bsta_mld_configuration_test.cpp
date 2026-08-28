/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../../son_actions.h"
#include "db.h"

#include <tlvf/wfa_map/tlvBackhaulStaMldConfiguration.h>

#include <ambiorix_mock.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

constexpr auto g_agent_dm_path = "Device.WiFi.DataElements.Network.Device.2";

class BstaMldConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        beerocks::config_file::sConfigMaster beerocks_master_conf;
        m_logger = std::make_unique<beerocks::logging>("bsta_mld_configuration_test",
                                                       beerocks_master_conf.sLog);
        m_logger->set_log_level_state(beerocks::LOG_LEVEL_ERROR, true);

        m_ambiorix = std::make_shared<NiceMock<beerocks::nbapi::AmbiorixMock>>();
        ON_CALL(*m_ambiorix, add_instance(_)).WillByDefault(Invoke([](const std::string &path) {
            return path + ".1";
        }));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const sMacAddr &>(_))).WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const std::string &>(_)))
            .WillByDefault(Return(true));
        ON_CALL(*m_ambiorix, set(_, _, Matcher<const bool &>(_))).WillByDefault(Return(true));

        son::db::sDbMasterConfig master_conf;
        m_database = std::make_unique<son::db>(
            master_conf, *m_logger, tlvf::mac_from_string("02:00:00:00:00:01"), m_ambiorix);

        m_agent = m_database->add_agent(m_agent_mac);
        ASSERT_NE(m_agent, nullptr);
        m_agent->dm_path = g_agent_dm_path;

        m_cmdu_tx = std::make_unique<ieee1905_1::CmduMessageTx>(m_tx_buffer, sizeof(m_tx_buffer));
    }

    bool send_report(bool bsta_mld_valid)
    {
        if (!m_cmdu_tx->create(0,
                               ieee1905_1::eMessageType::BSTA_MLD_CONFIGURATION_RESPONSE_MESSAGE)) {
            return false;
        }
        auto tlv = m_cmdu_tx->addClass<wfa_map::tlvBackhaulStaMldConfiguration>();
        if (!tlv) {
            return false;
        }
        tlv->addr_valid().bsta_mld_mac_addr_valid = bsta_mld_valid;
        tlv->addr_valid().ap_mld_mac_addr_valid   = bsta_mld_valid;
        if (bsta_mld_valid) {
            tlv->bsta_mld_mac_addr() = m_bsta_mld_mac;
            tlv->ap_mld_mac_addr()   = tlvf::mac_from_string("02:00:00:00:01:02");
            tlv->modes().str         = 1;
        }
        if (!m_cmdu_tx->finalize()) {
            return false;
        }
        std::copy_n(m_tx_buffer, m_cmdu_tx->getMessageLength(), m_rx_buffer);

        ieee1905_1::CmduMessageRx cmdu_rx(m_rx_buffer, sizeof(m_rx_buffer));
        if (!cmdu_rx.parse()) {
            return false;
        }
        return son::son_actions::handle_backhaul_sta_mld_configuration_tlv(*m_database, m_agent_mac,
                                                                           cmdu_rx);
    }

    std::unique_ptr<beerocks::logging> m_logger;
    std::shared_ptr<NiceMock<beerocks::nbapi::AmbiorixMock>> m_ambiorix;
    std::unique_ptr<son::db> m_database;
    std::shared_ptr<prplmesh::controller::db::Agent> m_agent;
    std::unique_ptr<ieee1905_1::CmduMessageTx> m_cmdu_tx;
    const sMacAddr m_agent_mac    = tlvf::mac_from_string("02:00:00:00:00:02");
    const sMacAddr m_bsta_mld_mac = tlvf::mac_from_string("02:00:00:00:01:01");
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH] = {};
    uint8_t m_rx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH] = {};
};

// cppcheck-suppress syntaxError
TEST_F(BstaMldConfigurationTest, report_without_bsta_mld_removes_object)
{
    const std::string agent_path = std::string(g_agent_dm_path) + ".";
    {
        InSequence sequence;
        // A valid report attaches the object.
        EXPECT_CALL(*m_ambiorix, has_optional_subobject(agent_path, "bSTAMLD"))
            .WillOnce(Return(false));
        EXPECT_CALL(*m_ambiorix, add_optional_subobject(agent_path, "bSTAMLD"))
            .WillOnce(Return(true));
        // A report with the bSTA MLD valid flag clear removes it.
        EXPECT_CALL(*m_ambiorix, has_optional_subobject(agent_path, "bSTAMLD"))
            .WillOnce(Return(true));
        EXPECT_CALL(*m_ambiorix, remove_optional_subobject(agent_path, "bSTAMLD"))
            .WillOnce(Return(true));
    }

    ASSERT_TRUE(send_report(true));
    EXPECT_EQ(m_bsta_mld_mac, m_agent->bsta_mld.mld_info.mld_mac);

    ASSERT_TRUE(send_report(false));
    EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, m_agent->bsta_mld.mld_info.mld_mac);
    EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, m_agent->bsta_mld.ap_mld_mac);
    EXPECT_TRUE(m_agent->bsta_mld.affiliated_bstas.empty());
}

} // namespace
