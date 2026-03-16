/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../ieee1905_task.h"
#include "db.h"
#include "on_action.h"

#include <bcl/beerocks_event_loop_impl.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>

namespace {

struct FakeIEEE1905QuerySender : son::IEEE1905QuerySender {
    bool send_topology_query(const sMacAddr &dest_mac, ieee1905_1::CmduMessageTx &) override
    {
        topology_queries.push_back(dest_mac);
        return true;
    }

    bool send_higher_layer_query(const sMacAddr &dest_mac, ieee1905_1::CmduMessageTx &) override
    {
        higher_layer_queries.push_back(dest_mac);
        return true;
    }

    std::deque<sMacAddr> topology_queries;
    std::deque<sMacAddr> higher_layer_queries;
};

class TestableIEEE1905Task : public son::ieee1905_task {
public:
    using son::ieee1905_task::ensure_al_in_dm;
    using son::ieee1905_task::handle_event;
    using son::ieee1905_task::ieee1905_task;
    using son::ieee1905_task::update_al_in_dm;
    using son::ieee1905_task::work;
};

class IEEE1905TaskTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_log_conf.files_enabled  = "false";
        m_log_conf.stdout_enabled = "true";
        m_log_conf.global_levels  = "all";
        m_log_conf.syslog_levels  = "off";
        m_logger = std::make_unique<beerocks::logging>("ieee1905_task_test", m_log_conf);
        m_logger->set_log_level_state(beerocks::LOG_LEVEL_ALL, true);

        m_amxrt                  = std::make_shared<beerocks::nbapi::Amxrt>();
        char app_name[]          = "ieee1905_task_test";
        std::vector<char *> argv = {app_name, nullptr};

        ASSERT_EQ(0, m_amxrt->Initialize(argv.size() - 1, argv.data(), nullptr));

        m_event_loop = std::make_shared<beerocks::EventLoopImpl>();

        // Gets rid of "Couldn't remove event handlers ..." noise
        ASSERT_TRUE(m_event_loop->register_handlers(amxp_signal_fd(), {}));

        m_ambiorix = std::make_shared<beerocks::nbapi::AmbiorixImpl>(
            m_event_loop, std::vector<beerocks::nbapi::sActionsCallback>{},
            prplmesh::controller::actions::get_events_list(),
            std::vector<beerocks::nbapi::sFunctions>{});

        ASSERT_TRUE(m_ambiorix->load_datamodel(IEEE1905_ODL_PATH));

        m_local_al_mac = tlvf::mac_from_string("11:22:33:44:55:66");
        m_database =
            std::make_unique<son::db>(m_master_conf, *m_logger, m_local_al_mac, m_ambiorix);

        m_cmdu_tx = std::make_unique<ieee1905_1::CmduMessageTx>(m_tx_buffer, sizeof(m_tx_buffer));

        auto query_sender = std::make_unique<FakeIEEE1905QuerySender>();
        m_query_sender    = query_sender.get();
        ASSERT_NE(m_query_sender, nullptr);

        m_task = std::make_unique<TestableIEEE1905Task>(
            *m_database, *m_cmdu_tx, std::move(query_sender),
            []() { return son::ieee1905_task::steady_clock::now(); });
        ASSERT_NE(m_task, nullptr);
    }

    void TearDown() override
    {
        m_task.reset();
        m_query_sender = nullptr;
        m_cmdu_tx.reset();
        m_database.reset();
        m_logger.reset();
        m_ambiorix.reset();
        m_event_loop.reset();
        m_amxrt.reset();
    }

    std::string read_network_status()
    {
        std::string status;
        EXPECT_TRUE(
            m_ambiorix->read_param(std::string(IEEE1905_ROOT_DM) + ".Network", "Status", &status));
        return status;
    }

    bool topology_query_sent_to(const sMacAddr &al_mac) const
    {
        return std::any_of(m_query_sender->topology_queries.begin(),
                           m_query_sender->topology_queries.end(),
                           [&](const sMacAddr &mac) { return mac == al_mac; });
    }

    void clear_sent_queries()
    {
        m_query_sender->topology_queries.clear();
        m_query_sender->higher_layer_queries.clear();
    }

    std::string read_al_param(const sMacAddr &al_mac, const std::string &param)
    {
        const auto al_it = m_database->ieee1905_network->al.find(al_mac);
        EXPECT_NE(al_it, m_database->ieee1905_network->al.end());

        std::string value;
        EXPECT_TRUE(m_ambiorix->read_param(al_it->second.dm_path.path, param, &value));
        return value;
    }

    std::string read_interface_param(const sMacAddr &al_mac, const sMacAddr &if_mac,
                                     const std::string &param)
    {
        const auto al_it = m_database->ieee1905_network->al.find(al_mac);
        EXPECT_NE(al_it, m_database->ieee1905_network->al.end());

        const auto iface_it = al_it->second.interfaces.find(if_mac);
        EXPECT_NE(iface_it, al_it->second.interfaces.end());

        std::string value;
        EXPECT_TRUE(m_ambiorix->read_param(iface_it->second.dm_path.path, param, &value));
        return value;
    }

    std::string read_ieee1905_neighbor_param(const sMacAddr &al_mac, const sMacAddr &if_mac,
                                             const sMacAddr &neighbor_al_mac,
                                             const std::string &param)
    {
        const auto al_it = m_database->ieee1905_network->al.find(al_mac);
        EXPECT_NE(al_it, m_database->ieee1905_network->al.end());

        const auto iface_it = al_it->second.interfaces.find(if_mac);
        EXPECT_NE(iface_it, al_it->second.interfaces.end());

        const auto neighbor_it = iface_it->second.ieee1905_neighbors.find(neighbor_al_mac);
        EXPECT_NE(neighbor_it, iface_it->second.ieee1905_neighbors.end());

        std::string value;
        EXPECT_TRUE(m_ambiorix->read_param(neighbor_it->second.dm_path.path, param, &value));
        return value;
    }

    beerocks::config_file::SConfigLog m_log_conf{};
    son::db::sDbMasterConfig m_master_conf;
    std::shared_ptr<beerocks::nbapi::Amxrt> m_amxrt;
    std::shared_ptr<beerocks::EventLoopImpl> m_event_loop;
    std::shared_ptr<beerocks::nbapi::AmbiorixImpl> m_ambiorix;
    std::unique_ptr<beerocks::logging> m_logger;
    std::unique_ptr<son::db> m_database;
    std::unique_ptr<ieee1905_1::CmduMessageTx> m_cmdu_tx;
    FakeIEEE1905QuerySender *m_query_sender = nullptr;
    std::unique_ptr<TestableIEEE1905Task> m_task;
    sMacAddr m_local_al_mac = beerocks::net::network_utils::ZERO_MAC;
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH] = {};
};

TEST_F(IEEE1905TaskTest, constructor_starts_local_topology_query_and_keeps_network_incomplete)
{
    ASSERT_TRUE(topology_query_sent_to(m_local_al_mac));
    EXPECT_TRUE(m_query_sender->higher_layer_queries.empty());
    ASSERT_NE(m_database->ieee1905_network, nullptr);
    EXPECT_EQ("Incomplete", read_network_status());
}

TEST_F(IEEE1905TaskTest, network_enable_event_false_disables_ieee1905_network)
{
    ASSERT_NE(m_database->ieee1905_network, nullptr);

    bool enabled = false;
    m_task->handle_event(son::ieee1905_task::IEEE1905_NETWORK_ENABLE_CHANGED, &enabled);

    EXPECT_EQ(nullptr, m_database->ieee1905_network.get());
}

TEST_F(IEEE1905TaskTest, network_enable_event_true_restarts_local_discovery)
{
    bool enabled = false;
    m_task->handle_event(son::ieee1905_task::IEEE1905_NETWORK_ENABLE_CHANGED, &enabled);
    ASSERT_EQ(nullptr, m_database->ieee1905_network.get());

    clear_sent_queries();

    enabled = true;
    m_task->handle_event(son::ieee1905_task::IEEE1905_NETWORK_ENABLE_CHANGED, &enabled);

    ASSERT_NE(m_database->ieee1905_network, nullptr);
    ASSERT_TRUE(topology_query_sent_to(m_local_al_mac));
    EXPECT_TRUE(m_query_sender->higher_layer_queries.empty());
    EXPECT_EQ("Incomplete", read_network_status());
}

TEST_F(IEEE1905TaskTest, ensure_al_in_dm_materializes_al_version)
{
    auto &local_al            = m_database->ieee1905_network->al[m_local_al_mac];
    local_al.version_is_1905a = true;

    ASSERT_TRUE(m_task->ensure_al_in_dm(m_local_al_mac));
    ASSERT_TRUE(local_al.dm_path);
    EXPECT_EQ("1905.1a", read_al_param(m_local_al_mac, "Version"));
}

TEST_F(IEEE1905TaskTest, update_al_in_dm_materializes_interface_and_non_1905_neighbor)
{
    const auto iface_mac         = tlvf::mac_from_string("11:22:33:44:55:77");
    const auto non_1905_neighbor = tlvf::mac_from_string("aa:bb:cc:dd:ee:f1");

    auto &local_al                              = m_database->ieee1905_network->al[m_local_al_mac];
    local_al.version_is_1905a                   = true;
    auto &iface                                 = local_al.interfaces[iface_mac];
    iface.type                                  = ieee1905_1::UNKNOWN_MEDIA;
    iface.non_1905_neighbors[non_1905_neighbor] = {};

    ASSERT_TRUE(m_task->ensure_al_in_dm(m_local_al_mac));
    EXPECT_EQ(tlvf::mac_to_string(iface_mac),
              read_interface_param(m_local_al_mac, iface_mac, "InterfaceId"));
}

TEST_F(IEEE1905TaskTest, ensure_al_in_dm_updates_existing_ieee1905_device_refs)
{
    using sNeighbor = son::db::ieee1905_network_db::sAL::sNeighbor;

    const auto source_al_mac = m_local_al_mac;
    const auto source_if_mac = m_local_al_mac;
    const auto target_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:21");
    auto &als                = m_database->ieee1905_network->al;
    auto &source_al          = als[source_al_mac];
    auto &source_iface       = source_al.interfaces[source_if_mac];
    auto ref_handle = sNeighbor::sRefHandle{als, target_al_mac, {source_al_mac, source_if_mac}};
    source_iface.ieee1905_neighbors.emplace(target_al_mac,
                                            sNeighbor{{}, false, std::move(ref_handle)});

    ASSERT_TRUE(m_task->ensure_al_in_dm(source_al_mac));
    EXPECT_TRUE(read_ieee1905_neighbor_param(source_al_mac, source_if_mac, target_al_mac,
                                             "IEEE1905DeviceRef")
                    .empty());

    ASSERT_TRUE(m_task->ensure_al_in_dm(target_al_mac));
    EXPECT_EQ("Device." + als[target_al_mac].dm_path.path,
              read_ieee1905_neighbor_param(source_al_mac, source_if_mac, target_al_mac,
                                           "IEEE1905DeviceRef"));
}

} // namespace
