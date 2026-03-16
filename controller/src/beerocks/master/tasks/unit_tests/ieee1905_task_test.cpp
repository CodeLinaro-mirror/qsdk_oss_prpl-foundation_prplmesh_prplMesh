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
#include <tlvf/ieee_1905_1/tlv1905NeighborDevice.h>
#include <tlvf/ieee_1905_1/tlvDeviceInformation.h>
#include <tlvf/ieee_1905_1/tlvNon1905neighborDeviceList.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <vector>

namespace {

template <typename Tuple> auto unwrap(Tuple &&result)
{
    return std::get<0>(result) ? &std::get<1>(result) : nullptr;
}
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
    struct sTopologyResponsePacket {
        std::vector<sMacAddr> interfaces;
        std::unordered_map<sMacAddr, std::vector<sMacAddr>> ieee1905_neighbors;
        std::unordered_map<sMacAddr, std::vector<sMacAddr>> non_1905_neighbors;
    };

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
        m_now = son::ieee1905_task::steady_clock::now();

        m_task = std::make_unique<TestableIEEE1905Task>(
            *m_database, *m_cmdu_tx, std::move(query_sender), [this]() { return m_now; });
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

    bool build_topology_response_cmdu(const sMacAddr &al_mac, ieee1905_1::CmduMessageRx &cmdu_rx,
                                      sTopologyResponsePacket packet = {})
    {
        if (!m_cmdu_tx->create(0, ieee1905_1::eMessageType::TOPOLOGY_RESPONSE_MESSAGE)) {
            return false;
        }

        auto device_information = m_cmdu_tx->addClass<ieee1905_1::tlvDeviceInformation>();
        if (!device_information) {
            return false;
        }
        device_information->mac() = al_mac;

        if (packet.interfaces.empty()) {
            packet.interfaces.push_back(al_mac);
        }

        for (const auto &if_mac : packet.interfaces) {
            auto iface_info = device_information->create_local_interface_list();
            if (!iface_info) {
                return false;
            }

            iface_info->mac()        = if_mac;
            iface_info->media_type() = ieee1905_1::UNKNOWN_MEDIA;

            if (!device_information->add_local_interface_list(iface_info)) {
                return false;
            }
        }

        for (const auto &entry : packet.non_1905_neighbors) {
            const auto &if_mac    = entry.first;
            const auto &neighbors = entry.second;

            if (neighbors.empty()) {
                continue;
            }

            auto tlv_neighbor = m_cmdu_tx->addClass<ieee1905_1::tlvNon1905neighborDeviceList>();
            if (!tlv_neighbor) {
                return false;
            }

            tlv_neighbor->mac_local_iface() = if_mac;
            if (!tlv_neighbor->alloc_mac_non_1905_device(neighbors.size())) {
                return false;
            }

            for (size_t i = 0; i < neighbors.size(); ++i) {
                auto neighbor = unwrap(tlv_neighbor->mac_non_1905_device(i));
                if (!neighbor) {
                    return false;
                }
                *neighbor = neighbors[i];
            }
        }

        for (const auto &entry : packet.ieee1905_neighbors) {
            const auto &if_mac    = entry.first;
            const auto &neighbors = entry.second;

            if (neighbors.empty()) {
                continue;
            }

            auto tlv_neighbor = m_cmdu_tx->addClass<ieee1905_1::tlv1905NeighborDevice>();
            if (!tlv_neighbor) {
                return false;
            }

            tlv_neighbor->mac_local_iface() = if_mac;
            if (!tlv_neighbor->alloc_mac_al_1905_device(neighbors.size())) {
                return false;
            }

            for (size_t i = 0; i < neighbors.size(); ++i) {
                auto neighbor = unwrap(tlv_neighbor->mac_al_1905_device(i));
                if (!neighbor) {
                    return false;
                }

                neighbor->mac           = neighbors[i];
                neighbor->bridges_exist = ieee1905_1::tlv1905NeighborDevice::NO_BRIDGES_EXIST;
            }
        }

        if (!m_cmdu_tx->finalize()) {
            return false;
        }

        std::copy_n(m_tx_buffer, m_cmdu_tx->getMessageLength(), m_rx_buffer);
        return cmdu_rx.parse();
    }

    bool build_higher_layer_response_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx)
    {
        if (!m_cmdu_tx->create(0, ieee1905_1::eMessageType::HIGHER_LAYER_RESPONSE_MESSAGE)) {
            return false;
        }

        if (!m_cmdu_tx->finalize()) {
            return false;
        }

        std::copy_n(m_tx_buffer, m_cmdu_tx->getMessageLength(), m_rx_buffer);
        return cmdu_rx.parse();
    }

    bool topology_query_sent_to(const sMacAddr &al_mac) const
    {
        return std::any_of(m_query_sender->topology_queries.begin(),
                           m_query_sender->topology_queries.end(),
                           [&](const sMacAddr &mac) { return mac == al_mac; });
    }

    bool higher_layer_query_sent_to(const sMacAddr &al_mac) const
    {
        return std::any_of(m_query_sender->higher_layer_queries.begin(),
                           m_query_sender->higher_layer_queries.end(),
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

    std::string read_non_1905_neighbor_param(const sMacAddr &al_mac, const sMacAddr &if_mac,
                                             const sMacAddr &neighbor_if_mac,
                                             const std::string &param)
    {
        const auto al_it = m_database->ieee1905_network->al.find(al_mac);
        EXPECT_NE(al_it, m_database->ieee1905_network->al.end());

        const auto iface_it = al_it->second.interfaces.find(if_mac);
        EXPECT_NE(iface_it, al_it->second.interfaces.end());

        const auto neighbor_it = iface_it->second.non_1905_neighbors.find(neighbor_if_mac);
        EXPECT_NE(neighbor_it, iface_it->second.non_1905_neighbors.end());

        std::string value;
        EXPECT_TRUE(m_ambiorix->read_param(neighbor_it->second.path, param, &value));
        return value;
    }

    void advance_time(std::chrono::seconds delta)
    {
        m_now += delta;
        m_task->work();
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
    son::ieee1905_task::time_point m_now;
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH] = {};
    uint8_t m_rx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH] = {};
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

TEST_F(IEEE1905TaskTest, topology_timeout_restarts_local_discovery)
{
    EXPECT_TRUE(topology_query_sent_to(m_local_al_mac));
    clear_sent_queries();

    // no response from local AL
    advance_time(son::ieee1905_task::topology_response_timeout);

    // topology query retried
    EXPECT_TRUE(topology_query_sent_to(m_local_al_mac));
    EXPECT_FALSE(higher_layer_query_sent_to(m_local_al_mac));
    EXPECT_TRUE(m_database->ieee1905_network->al.empty());
    EXPECT_EQ("Incomplete", read_network_status());
}

TEST_F(IEEE1905TaskTest, topology_timeout_clears_remote_sidecar_without_removing_db_al)
{
    const auto remote_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:35");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {remote_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    ASSERT_EQ(1, m_database->ieee1905_network->al.count(remote_al_mac));

    clear_sent_queries();

    // no response from remote AL
    advance_time(son::ieee1905_task::topology_response_timeout);

    // sidecar is cleared, DB AL is kept until orphaned by topology updates
    EXPECT_FALSE(topology_query_sent_to(remote_al_mac));
    EXPECT_FALSE(higher_layer_query_sent_to(remote_al_mac));
    EXPECT_EQ(1, m_database->ieee1905_network->al.count(remote_al_mac));
    EXPECT_EQ("Available", read_network_status());
}

TEST_F(IEEE1905TaskTest, topology_response_removes_orphan_chain_local_mac1_mac2)
{
    const auto mac1_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:31");
    const auto mac2_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:32");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {mac1_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    sTopologyResponsePacket mac1_packet;
    mac1_packet.ieee1905_neighbors[mac1_al_mac] = {mac2_al_mac};
    ieee1905_1::CmduMessageRx mac1_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(mac1_al_mac, mac1_topology_rx, mac1_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac1_al_mac, mac1_topology_rx));

    ieee1905_1::CmduMessageRx mac2_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(mac2_al_mac, mac2_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac2_al_mac, mac2_topology_rx));

    ieee1905_1::CmduMessageRx mac1_higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(mac1_higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac1_al_mac, mac1_higher_layer_rx));

    ieee1905_1::CmduMessageRx mac2_higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(mac2_higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac2_al_mac, mac2_higher_layer_rx));

    ASSERT_EQ(1, m_database->ieee1905_network->al.count(mac1_al_mac));
    ASSERT_EQ(1, m_database->ieee1905_network->al.count(mac2_al_mac));
    const auto mac1_path = m_database->ieee1905_network->al.at(mac1_al_mac).dm_path.path;
    const auto mac2_path = m_database->ieee1905_network->al.at(mac2_al_mac).dm_path.path;

    ieee1905_1::CmduMessageRx local_topology_without_neighbors(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_without_neighbors));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_without_neighbors));

    EXPECT_EQ(0, m_database->ieee1905_network->al.count(mac1_al_mac));
    EXPECT_EQ(0, m_database->ieee1905_network->al.count(mac2_al_mac));

    std::string ieee1905_id;
    EXPECT_FALSE(m_ambiorix->read_param(mac1_path, "IEEE1905Id", &ieee1905_id));
    EXPECT_FALSE(m_ambiorix->read_param(mac2_path, "IEEE1905Id", &ieee1905_id));
}

TEST_F(IEEE1905TaskTest, topology_response_removes_orphan_cycle_disconnected_from_local_al)
{
    const auto mac1_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:33");
    const auto mac2_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:34");
    const auto mac3_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:35");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {mac1_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    sTopologyResponsePacket mac1_packet;
    mac1_packet.ieee1905_neighbors[mac1_al_mac] = {mac2_al_mac};
    ieee1905_1::CmduMessageRx mac1_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(mac1_al_mac, mac1_topology_rx, mac1_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac1_al_mac, mac1_topology_rx));

    sTopologyResponsePacket mac2_packet;
    mac2_packet.ieee1905_neighbors[mac2_al_mac] = {mac3_al_mac};
    ieee1905_1::CmduMessageRx mac2_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(mac2_al_mac, mac2_topology_rx, mac2_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac2_al_mac, mac2_topology_rx));

    sTopologyResponsePacket mac3_packet;
    mac3_packet.ieee1905_neighbors[mac3_al_mac] = {mac2_al_mac};
    ieee1905_1::CmduMessageRx mac3_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(mac3_al_mac, mac3_topology_rx, mac3_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac3_al_mac, mac3_topology_rx));

    ASSERT_EQ(1, m_database->ieee1905_network->al.count(mac1_al_mac));
    ASSERT_EQ(1, m_database->ieee1905_network->al.count(mac2_al_mac));
    ASSERT_EQ(1, m_database->ieee1905_network->al.count(mac3_al_mac));
    const auto mac1_path = m_database->ieee1905_network->al.at(mac1_al_mac).dm_path.path;
    const auto mac2_path = m_database->ieee1905_network->al.at(mac2_al_mac).dm_path.path;
    const auto mac3_path = m_database->ieee1905_network->al.at(mac3_al_mac).dm_path.path;

    ieee1905_1::CmduMessageRx local_topology_without_neighbors(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_without_neighbors));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_without_neighbors));

    EXPECT_EQ(0, m_database->ieee1905_network->al.count(mac1_al_mac));
    EXPECT_EQ(0, m_database->ieee1905_network->al.count(mac2_al_mac));
    EXPECT_EQ(0, m_database->ieee1905_network->al.count(mac3_al_mac));

    std::string ieee1905_id;
    EXPECT_FALSE(m_ambiorix->read_param(mac1_path, "IEEE1905Id", &ieee1905_id));
    EXPECT_FALSE(m_ambiorix->read_param(mac2_path, "IEEE1905Id", &ieee1905_id));
    EXPECT_FALSE(m_ambiorix->read_param(mac3_path, "IEEE1905Id", &ieee1905_id));
}

TEST_F(IEEE1905TaskTest, orphan_cleanup_does_not_complete_status_for_already_completed_remote_al)
{
    const auto mac1_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:41");
    const auto mac2_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:42");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {mac1_al_mac, mac2_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    ieee1905_1::CmduMessageRx mac1_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(mac1_al_mac, mac1_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac1_al_mac, mac1_topology_rx));

    ieee1905_1::CmduMessageRx mac1_higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(mac1_higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac1_al_mac, mac1_higher_layer_rx));

    ASSERT_EQ("Incomplete", read_network_status());

    sTopologyResponsePacket local_packet_without_mac1;
    local_packet_without_mac1.ieee1905_neighbors[m_local_al_mac] = {mac2_al_mac};
    ieee1905_1::CmduMessageRx local_topology_without_mac1(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_without_mac1,
                                             local_packet_without_mac1));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_without_mac1));

    EXPECT_EQ(0, m_database->ieee1905_network->al.count(mac1_al_mac));
    EXPECT_EQ(1, m_database->ieee1905_network->al.count(mac2_al_mac));
    EXPECT_EQ("Incomplete", read_network_status());
}

TEST_F(IEEE1905TaskTest, orphan_cleanup_completes_status_for_pending_remote_al)
{
    const auto mac1_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:51");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {mac1_al_mac};
    ieee1905_1::CmduMessageRx local_topology_with_mac1(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(
        build_topology_response_cmdu(m_local_al_mac, local_topology_with_mac1, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_with_mac1));

    ASSERT_EQ("Incomplete", read_network_status());
    ASSERT_EQ(1, m_database->ieee1905_network->al.count(mac1_al_mac));

    ieee1905_1::CmduMessageRx local_topology_without_neighbors(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_without_neighbors));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_without_neighbors));

    EXPECT_EQ(0, m_database->ieee1905_network->al.count(mac1_al_mac));
    EXPECT_EQ("Available", read_network_status());
}

TEST_F(IEEE1905TaskTest, topology_logic_keeps_nn_under_both_n1_n2_until_n1_drops_it)
{
    const auto n1_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:61");
    const auto n2_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:62");
    const auto nn_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:66");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {n1_al_mac, n2_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    sTopologyResponsePacket n1_packet;
    n1_packet.ieee1905_neighbors[n1_al_mac] = {nn_al_mac};
    ieee1905_1::CmduMessageRx n1_topology_with_nn(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n1_al_mac, n1_topology_with_nn, n1_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_topology_with_nn));

    sTopologyResponsePacket n2_packet;
    n2_packet.ieee1905_neighbors[n2_al_mac] = {nn_al_mac};
    ieee1905_1::CmduMessageRx n2_topology_with_nn(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n2_al_mac, n2_topology_with_nn, n2_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n2_al_mac, n2_topology_with_nn));

    auto &als = m_database->ieee1905_network->al;
    ASSERT_EQ(1, als[n1_al_mac].interfaces[n1_al_mac].ieee1905_neighbors.count(nn_al_mac));
    ASSERT_EQ(1, als[n2_al_mac].interfaces[n2_al_mac].ieee1905_neighbors.count(nn_al_mac));
    ASSERT_EQ(2, als[nn_al_mac].references.size());

    ieee1905_1::CmduMessageRx n1_topology_without_nn(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n1_al_mac, n1_topology_without_nn));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_topology_without_nn));

    EXPECT_EQ(0, als[n1_al_mac].interfaces[n1_al_mac].ieee1905_neighbors.count(nn_al_mac));
    EXPECT_EQ(1, als[n2_al_mac].interfaces[n2_al_mac].ieee1905_neighbors.count(nn_al_mac));
    ASSERT_EQ(1, als[nn_al_mac].references.size());
}

TEST_F(IEEE1905TaskTest, topology_response_move_ieee1905_neighbor_between_interfaces)
{
    const auto n1_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:81");
    const auto nn_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:82");
    const auto if1_mac   = tlvf::mac_from_string("aa:bb:cc:dd:ee:91");
    const auto if2_mac   = tlvf::mac_from_string("aa:bb:cc:dd:ee:92");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {n1_al_mac};

    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    sTopologyResponsePacket n1_packet;
    n1_packet.interfaces                  = {if1_mac, if2_mac};
    n1_packet.ieee1905_neighbors[if1_mac] = {nn_al_mac};

    ieee1905_1::CmduMessageRx n1_topology_on_if1(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n1_al_mac, n1_topology_on_if1, n1_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_topology_on_if1));

    ieee1905_1::CmduMessageRx n1_higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(n1_higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_higher_layer_rx));

    auto &n1_db = m_database->ieee1905_network->al[n1_al_mac];
    ASSERT_EQ(1, n1_db.interfaces[if1_mac].ieee1905_neighbors.count(nn_al_mac));
    ASSERT_EQ(0, n1_db.interfaces[if2_mac].ieee1905_neighbors.count(nn_al_mac));
    const auto old_neighbor_path =
        n1_db.interfaces[if1_mac].ieee1905_neighbors.at(nn_al_mac).dm_path.path;

    n1_packet.ieee1905_neighbors.clear();
    n1_packet.ieee1905_neighbors[if2_mac] = {nn_al_mac};

    ieee1905_1::CmduMessageRx n1_topology_on_if2(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n1_al_mac, n1_topology_on_if2, n1_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_topology_on_if2));

    ASSERT_EQ(0, n1_db.interfaces[if1_mac].ieee1905_neighbors.count(nn_al_mac));
    ASSERT_EQ(1, n1_db.interfaces[if2_mac].ieee1905_neighbors.count(nn_al_mac));
    const auto new_neighbor_path =
        n1_db.interfaces[if2_mac].ieee1905_neighbors.at(nn_al_mac).dm_path.path;
    EXPECT_NE(old_neighbor_path, new_neighbor_path);

    std::string neighbor_device_id;
    EXPECT_FALSE(
        m_ambiorix->read_param(old_neighbor_path, "NeighborDeviceId", &neighbor_device_id));
    EXPECT_EQ(tlvf::mac_to_string(nn_al_mac),
              read_ieee1905_neighbor_param(n1_al_mac, if2_mac, nn_al_mac, "NeighborDeviceId"));
}

TEST_F(IEEE1905TaskTest, topology_response_moves_neighbor_between_non_1905_and_ieee1905_lists)
{
    const auto n1_al_mac       = tlvf::mac_from_string("aa:bb:cc:dd:ee:a1");
    const auto moving_mac      = tlvf::mac_from_string("aa:bb:cc:dd:ee:a2");
    const auto n1_iface_if_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:b1");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {n1_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    sTopologyResponsePacket non_1905_packet;
    non_1905_packet.interfaces                          = {n1_iface_if_mac};
    non_1905_packet.non_1905_neighbors[n1_iface_if_mac] = {moving_mac};
    ieee1905_1::CmduMessageRx n1_non_1905_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n1_al_mac, n1_non_1905_topology_rx, non_1905_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_non_1905_topology_rx));

    ieee1905_1::CmduMessageRx n1_higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(n1_higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_higher_layer_rx));

    auto &n1_db = m_database->ieee1905_network->al[n1_al_mac];
    ASSERT_EQ(1, n1_db.interfaces[n1_iface_if_mac].non_1905_neighbors.count(moving_mac));
    ASSERT_EQ(0, n1_db.interfaces[n1_iface_if_mac].ieee1905_neighbors.count(moving_mac));
    const auto old_non_1905_path =
        n1_db.interfaces[n1_iface_if_mac].non_1905_neighbors.at(moving_mac).path;

    sTopologyResponsePacket ieee1905_packet;
    ieee1905_packet.interfaces                          = {n1_iface_if_mac};
    ieee1905_packet.ieee1905_neighbors[n1_iface_if_mac] = {moving_mac};
    ieee1905_1::CmduMessageRx n1_ieee1905_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n1_al_mac, n1_ieee1905_topology_rx, ieee1905_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_ieee1905_topology_rx));

    ASSERT_EQ(0, n1_db.interfaces[n1_iface_if_mac].non_1905_neighbors.count(moving_mac));
    ASSERT_EQ(1, n1_db.interfaces[n1_iface_if_mac].ieee1905_neighbors.count(moving_mac));
    EXPECT_TRUE(topology_query_sent_to(moving_mac));
    EXPECT_TRUE(higher_layer_query_sent_to(moving_mac));

    std::string neighbor_interface_id;
    EXPECT_FALSE(
        m_ambiorix->read_param(old_non_1905_path, "NeighborInterfaceId", &neighbor_interface_id));
    EXPECT_EQ(
        tlvf::mac_to_string(moving_mac),
        read_ieee1905_neighbor_param(n1_al_mac, n1_iface_if_mac, moving_mac, "NeighborDeviceId"));
}

TEST_F(IEEE1905TaskTest, network_status_becomes_available_only_after_all_discovered_nodes_complete)
{
    const auto n1_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:71");
    const auto n2_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:72");
    const auto nn_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:77");

    clear_sent_queries();

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {n1_al_mac, n2_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));
    ASSERT_TRUE(topology_query_sent_to(n1_al_mac));
    ASSERT_TRUE(topology_query_sent_to(n2_al_mac));
    ASSERT_TRUE(higher_layer_query_sent_to(n1_al_mac));
    ASSERT_TRUE(higher_layer_query_sent_to(n2_al_mac));
    EXPECT_EQ("Incomplete", read_network_status());

    sTopologyResponsePacket n1_packet;
    n1_packet.ieee1905_neighbors[n1_al_mac] = {nn_al_mac};
    ieee1905_1::CmduMessageRx n1_topology_with_nn(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n1_al_mac, n1_topology_with_nn, n1_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n1_al_mac, n1_topology_with_nn));
    ASSERT_TRUE(topology_query_sent_to(nn_al_mac));
    ASSERT_TRUE(higher_layer_query_sent_to(nn_al_mac));
    EXPECT_EQ("Incomplete", read_network_status());

    ieee1905_1::CmduMessageRx n2_topology_without_nn(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(n2_al_mac, n2_topology_without_nn));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(n2_al_mac, n2_topology_without_nn));
    EXPECT_EQ("Incomplete", read_network_status());

    // N1/N2/NN higher-layer response timeouts may fire, but NN topology is still pending.
    advance_time(son::ieee1905_task::higher_layer_response_timeout);
    EXPECT_EQ("Incomplete", read_network_status());

    ieee1905_1::CmduMessageRx nn_topology_without_neighbors(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(nn_al_mac, nn_topology_without_neighbors));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(nn_al_mac, nn_topology_without_neighbors));
    EXPECT_EQ("Available", read_network_status());
}

} // namespace
