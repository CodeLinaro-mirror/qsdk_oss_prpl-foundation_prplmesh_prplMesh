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
#include <bcl/network/network_utils.h>
#include <tlvf/ieee_1905_1/tlv1905NeighborDevice.h>
#include <tlvf/ieee_1905_1/tlv1905ProfileVersion.h>
#include <tlvf/ieee_1905_1/tlvAlMacAddress.h>
#include <tlvf/ieee_1905_1/tlvControlUrl.h>
#include <tlvf/ieee_1905_1/tlvDeviceBridgingCapability.h>
#include <tlvf/ieee_1905_1/tlvDeviceIdentification.h>
#include <tlvf/ieee_1905_1/tlvDeviceInformation.h>
#include <tlvf/ieee_1905_1/tlvIpv4.h>
#include <tlvf/ieee_1905_1/tlvIpv6.h>
#include <tlvf/ieee_1905_1/tlvNon1905neighborDeviceList.h>
#include <tlvf/ieee_1905_1/tlvReceiverLinkMetric.h>
#include <tlvf/ieee_1905_1/tlvTransmitterLinkMetric.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>

#include <gtest/gtest.h>

#include <arpa/inet.h>

#include <algorithm>
#include <array>
#include <deque>
#include <unordered_map>
#include <vector>

using net_utils = beerocks::net::network_utils;

namespace {

template <typename Tuple> auto unwrap(Tuple &&result)
{
    return std::get<0>(result) ? &std::get<1>(result) : nullptr;
}

static uint32_t ipv4_to_u32(const beerocks::net::sIpv4Addr &ipv4)
{
    return (static_cast<uint32_t>(ipv4.oct[0]) << 24) | (static_cast<uint32_t>(ipv4.oct[1]) << 16) |
           (static_cast<uint32_t>(ipv4.oct[2]) << 8) | static_cast<uint32_t>(ipv4.oct[3]);
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

    bool send_link_metric_query(const sMacAddr &dest_mac, ieee1905_1::CmduMessageTx &) override
    {
        link_metric_queries.push_back(dest_mac);
        return true;
    }

    std::deque<sMacAddr> topology_queries;
    std::deque<sMacAddr> higher_layer_queries;
    std::deque<sMacAddr> link_metric_queries;
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
        struct sInterfaceInfo {
            sMacAddr mac;
            ieee1905_1::eMediaType media_type                 = ieee1905_1::UNKNOWN_MEDIA;
            bool has_media_info                               = false;
            ieee1905_1::s802_11SpecificInformation media_info = {};
        };

        std::vector<sMacAddr> interfaces;
        std::vector<sInterfaceInfo> interface_infos;
        std::unordered_map<sMacAddr, std::vector<sMacAddr>> ieee1905_neighbors;
        std::unordered_map<sMacAddr, std::vector<sMacAddr>> non_1905_neighbors;
        std::vector<std::vector<sMacAddr>> bridging_tuples;
    };

    struct sHigherLayerResponsePacket {
        struct sIPv4Address {
            sMacAddr mac;
            beerocks::net::sIpv4Addr address     = {};
            ieee1905_1::eIpv4AddressType type    = ieee1905_1::eIpv4AddressType::UNKNOWN;
            beerocks::net::sIpv4Addr dhcp_server = {};
        };

        struct sIPv6Address {
            sMacAddr mac;
            std::string address;
            ieee1905_1::eIpv6AddressType type = ieee1905_1::eIpv6AddressType::UNKNOWN;
            std::string origin;
        };

        std::string friendly_name;
        std::string manufacturer_name;
        std::string manufacturer_model;
        std::string control_url;
        std::vector<sIPv4Address> ipv4_addresses;
        std::vector<sIPv6Address> ipv6_addresses;
        bool set_profile_version = false;
        ieee1905_1::e1905ProfileVersion profile_version =
            ieee1905_1::e1905ProfileVersion::IEEE_1905_1;
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

        ASSERT_TRUE(m_event_loop->register_handlers(amxp_signal_fd(), {}));

        m_ambiorix = std::make_shared<beerocks::nbapi::AmbiorixImpl>(
            m_event_loop, std::vector<beerocks::nbapi::sActionsCallback>{},
            prplmesh::controller::actions::get_events_list(),
            std::vector<beerocks::nbapi::sFunctions>{});

        ASSERT_TRUE(m_ambiorix->load_datamodel(IEEE1905_ODL_PATH));
        ASSERT_TRUE(m_ambiorix->load_datamodel(DATAELEMENTS_NETWORK_DEVICE_TEST_ODL_PATH));

        m_local_al_mac = tlvf::mac_from_string("11:22:33:44:55:66");
        m_database =
            std::make_unique<son::db>(m_master_conf, *m_logger, m_local_al_mac, m_ambiorix);
        prplmesh::controller::actions::g_database = m_database.get();

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
        prplmesh::controller::actions::g_database = nullptr;
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

        if (packet.interfaces.empty() && packet.interface_infos.empty()) {
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

        for (const auto &interface_info : packet.interface_infos) {
            auto iface_info = device_information->create_local_interface_list();
            if (!iface_info) {
                return false;
            }

            iface_info->mac()        = interface_info.mac;
            iface_info->media_type() = interface_info.media_type;

            if (interface_info.has_media_info) {
                iface_info->alloc_media_info(sizeof(interface_info.media_info));
                std::copy_n(reinterpret_cast<const uint8_t *>(&interface_info.media_info),
                            sizeof(interface_info.media_info), iface_info->media_info(0));
            }

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

        if (!packet.bridging_tuples.empty()) {
            auto tlv_bridging_cap = m_cmdu_tx->addClass<ieee1905_1::tlvDeviceBridgingCapability>();
            if (!tlv_bridging_cap) {
                return false;
            }

            for (const auto &tuple_interfaces : packet.bridging_tuples) {
                auto mac_list = tlv_bridging_cap->create_bridging_tuples_list();
                if (!mac_list) {
                    return false;
                }
                if (!mac_list->alloc_mac_list(tuple_interfaces.size())) {
                    return false;
                }

                for (size_t i = 0; i < tuple_interfaces.size(); ++i) {
                    auto mac = unwrap(mac_list->mac_list(i));
                    if (!mac) {
                        return false;
                    }

                    *mac = tuple_interfaces[i];
                }

                if (!tlv_bridging_cap->add_bridging_tuples_list(mac_list)) {
                    return false;
                }
            }
        }

        if (!m_cmdu_tx->finalize()) {
            return false;
        }

        std::copy_n(m_tx_buffer, m_cmdu_tx->getMessageLength(), m_rx_buffer);
        return cmdu_rx.parse();
    }

    bool build_higher_layer_response_cmdu(const sMacAddr &al_mac,
                                          ieee1905_1::CmduMessageRx &cmdu_rx)
    {
        return build_higher_layer_response_cmdu(al_mac, cmdu_rx, sHigherLayerResponsePacket{});
    }

    bool build_higher_layer_response_cmdu(const sMacAddr &al_mac,
                                          ieee1905_1::CmduMessageRx &cmdu_rx,
                                          const sHigherLayerResponsePacket &packet)
    {
        if (!m_cmdu_tx->create(0, ieee1905_1::eMessageType::HIGHER_LAYER_RESPONSE_MESSAGE)) {
            return false;
        }

        auto tlv_al_mac = m_cmdu_tx->addClass<ieee1905_1::tlvAlMacAddress>();
        if (!tlv_al_mac) {
            return false;
        }
        tlv_al_mac->mac() = al_mac;

        if (packet.set_profile_version) {
            auto profile_tlv = m_cmdu_tx->addClass<ieee1905_1::tlv1905ProfileVersion>();
            if (!profile_tlv) {
                return false;
            }
            profile_tlv->version() = packet.profile_version;
        }

        if (!packet.friendly_name.empty() || !packet.manufacturer_name.empty() ||
            !packet.manufacturer_model.empty()) {
            auto device_id_tlv = m_cmdu_tx->addClass<ieee1905_1::tlvDeviceIdentification>();
            if (!device_id_tlv) {
                return false;
            }

            constexpr size_t kDeviceIdentificationStringSize                        = 64;
            std::array<uint8_t, kDeviceIdentificationStringSize> friendly_name      = {};
            std::array<uint8_t, kDeviceIdentificationStringSize> manufacturer_name  = {};
            std::array<uint8_t, kDeviceIdentificationStringSize> manufacturer_model = {};

            std::copy_n(packet.friendly_name.begin(),
                        std::min(packet.friendly_name.size(), friendly_name.size()),
                        friendly_name.begin());
            std::copy_n(packet.manufacturer_name.begin(),
                        std::min(packet.manufacturer_name.size(), manufacturer_name.size()),
                        manufacturer_name.begin());
            std::copy_n(packet.manufacturer_model.begin(),
                        std::min(packet.manufacturer_model.size(), manufacturer_model.size()),
                        manufacturer_model.begin());

            if (!device_id_tlv->set_friendly_name(friendly_name.data(), friendly_name.size())) {
                return false;
            }
            if (!device_id_tlv->set_manufacturer_name(manufacturer_name.data(),
                                                      manufacturer_name.size())) {
                return false;
            }
            if (!device_id_tlv->set_manufacturer_model(manufacturer_model.data(),
                                                       manufacturer_model.size())) {
                return false;
            }
        }

        for (const auto &ipv4_address : packet.ipv4_addresses) {
            auto ipv4_tlv = m_cmdu_tx->addClass<ieee1905_1::tlvIpv4>();
            if (!ipv4_tlv) {
                return false;
            }

            auto ipv4_iface = ipv4_tlv->create_ipv4_interfaces_list();
            if (!ipv4_iface) {
                return false;
            }

            ipv4_iface->mac_address() = ipv4_address.mac;
            if (!ipv4_iface->alloc_ipv4_address_entries(1)) {
                return false;
            }

            auto ipv4_entry = unwrap(ipv4_iface->ipv4_address_entries(0));
            if (!ipv4_entry) {
                return false;
            }

            ipv4_entry->ipv4_address_type = ipv4_address.type;
            ipv4_entry->ipv4_address      = ipv4_to_u32(ipv4_address.address);
            ipv4_entry->ipv4_dhcp_server  = ipv4_to_u32(ipv4_address.dhcp_server);

            if (!ipv4_tlv->add_ipv4_interfaces_list(ipv4_iface)) {
                return false;
            }
        }

        for (const auto &ipv6_address : packet.ipv6_addresses) {
            auto ipv6_tlv = m_cmdu_tx->addClass<ieee1905_1::tlvIpv6>();
            if (!ipv6_tlv) {
                return false;
            }

            auto ipv6_iface = ipv6_tlv->create_ipv6_interfaces_list();
            if (!ipv6_iface) {
                return false;
            }

            ipv6_iface->mac_address() = ipv6_address.mac;

            constexpr uint8_t kEmptyIpv6LinkLocal[16] = {};
            if (!ipv6_iface->set_ipv6_link_local_address(kEmptyIpv6LinkLocal,
                                                         sizeof(kEmptyIpv6LinkLocal))) {
                return false;
            }
            if (!ipv6_iface->alloc_ipv6_address_entries(1)) {
                return false;
            }

            auto ipv6_entry = unwrap(ipv6_iface->ipv6_address_entries(0));
            if (!ipv6_entry) {
                return false;
            }

            ipv6_entry->ipv6_address_type = ipv6_address.type;
            if (inet_pton(AF_INET6, ipv6_address.address.c_str(), ipv6_entry->ipv6_address) != 1) {
                return false;
            }
            if (inet_pton(AF_INET6, ipv6_address.origin.c_str(), ipv6_entry->ipv6_address_origin) !=
                1) {
                return false;
            }

            if (!ipv6_tlv->add_ipv6_interfaces_list(ipv6_iface)) {
                return false;
            }
        }

        if (!packet.control_url.empty()) {
            auto control_url_tlv = m_cmdu_tx->addClass<ieee1905_1::tlvControlUrl>();
            if (!control_url_tlv) {
                return false;
            }
            if (!control_url_tlv->set_control_url(packet.control_url.data(),
                                                  packet.control_url.size())) {
                return false;
            }
        }

        if (!m_cmdu_tx->finalize()) {
            return false;
        }

        std::copy_n(m_tx_buffer, m_cmdu_tx->getMessageLength(), m_rx_buffer);
        return cmdu_rx.parse();
    }

    bool build_link_metric_response_cmdu(
        ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &reporter_al_mac,
        const sMacAddr &neighbor_al_mac,
        const std::vector<ieee1905_1::tlvTransmitterLinkMetric::sInterfacePairInfo> &tx_pairs = {},
        const std::vector<ieee1905_1::tlvReceiverLinkMetric::sInterfacePairInfo> &rx_pairs    = {})
    {
        if (!m_cmdu_tx->create(0, ieee1905_1::eMessageType::LINK_METRIC_RESPONSE_MESSAGE)) {
            return false;
        }

        if (!tx_pairs.empty()) {
            auto tx_tlv = m_cmdu_tx->addClass<ieee1905_1::tlvTransmitterLinkMetric>();
            if (!tx_tlv) {
                return false;
            }

            tx_tlv->reporter_al_mac() = reporter_al_mac;
            tx_tlv->neighbor_al_mac() = neighbor_al_mac;
            if (!tx_tlv->alloc_interface_pair_info(tx_pairs.size())) {
                return false;
            }

            for (size_t i = 0; i < tx_pairs.size(); ++i) {
                auto tx_pair = unwrap(tx_tlv->interface_pair_info(i));
                if (!tx_pair) {
                    return false;
                }

                *tx_pair = tx_pairs[i];
            }
        }

        if (!rx_pairs.empty()) {
            auto rx_tlv = m_cmdu_tx->addClass<ieee1905_1::tlvReceiverLinkMetric>();
            if (!rx_tlv) {
                return false;
            }

            rx_tlv->reporter_al_mac() = reporter_al_mac;
            rx_tlv->neighbor_al_mac() = neighbor_al_mac;
            if (!rx_tlv->alloc_interface_pair_info(rx_pairs.size())) {
                return false;
            }

            for (size_t i = 0; i < rx_pairs.size(); ++i) {
                auto rx_pair = unwrap(rx_tlv->interface_pair_info(i));
                if (!rx_pair) {
                    return false;
                }

                *rx_pair = rx_pairs[i];
            }
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

    bool link_metric_query_sent_to(const sMacAddr &al_mac) const
    {
        return std::any_of(m_query_sender->link_metric_queries.begin(),
                           m_query_sender->link_metric_queries.end(),
                           [&](const sMacAddr &mac) { return mac == al_mac; });
    }

    void clear_sent_queries()
    {
        m_query_sender->topology_queries.clear();
        m_query_sender->higher_layer_queries.clear();
        m_query_sender->link_metric_queries.clear();
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

    void read_all_pending_signals()
    {
        while (amxp_signal_read() == 0)
            ;
    }

    void advance_time(std::chrono::seconds delta)
    {
        m_now += delta;
        m_task->work();
    }

    beerocks::config_file::SConfigLog m_log_conf{};
    son::db::sDbMasterConfig m_master_conf{};
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

// cppcheck-suppress syntaxError
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

TEST_F(IEEE1905TaskTest,
       topology_response_materializes_wireless_interface_network_membership_and_role)
{
    const auto iface_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:11");
    const auto bssid     = tlvf::mac_from_string("aa:bb:cc:dd:ee:12");

    ieee1905_1::CmduMessageRx topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    sTopologyResponsePacket packet;
    packet.interface_infos.push_back({
        iface_mac,
        ieee1905_1::IEEE_802_11AX,
        true,
        {
            .network_membership = bssid,
            .role               = ieee1905_1::eRole::NON_AP_NON_PCP_STA,
        },
    });

    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, topology_rx, packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, topology_rx));

    EXPECT_EQ(tlvf::mac_to_string(iface_mac),
              read_interface_param(m_local_al_mac, iface_mac, "InterfaceId"));
    EXPECT_EQ(tlvf::mac_to_string(bssid),
              read_interface_param(m_local_al_mac, iface_mac, "NetworkMembership"));
    EXPECT_EQ("non-AP/non-PCP STA", read_interface_param(m_local_al_mac, iface_mac, "Role"));
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

TEST_F(IEEE1905TaskTest, assoc_wifi_network_device_ref_updates_on_dataelements_events)
{
    const auto remote_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:31");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {remote_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    ieee1905_1::CmduMessageRx remote_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(remote_al_mac, remote_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, remote_topology_rx));
    ASSERT_TRUE(m_task->ensure_al_in_dm(remote_al_mac));

    EXPECT_TRUE(read_al_param(remote_al_mac, "AssocWiFiNetworkDeviceRef").empty());

    const auto device_path = m_ambiorix->add_instance(DATAELEMENTS_ROOT_DM ".Network.Device");
    ASSERT_FALSE(device_path.empty());

    read_all_pending_signals();
    EXPECT_TRUE(read_al_param(remote_al_mac, "AssocWiFiNetworkDeviceRef").empty());

    ASSERT_TRUE(m_ambiorix->set(device_path, "ID", remote_al_mac));
    read_all_pending_signals();

    const auto device_index = m_ambiorix->get_instance_index(
        DATAELEMENTS_ROOT_DM ".Network.Device.[ID == '%s'].", tlvf::mac_to_string(remote_al_mac));
    ASSERT_NE(0, device_index);

    EXPECT_EQ("Device.WiFi.DataElements.Network.Device." + std::to_string(device_index),
              read_al_param(remote_al_mac, "AssocWiFiNetworkDeviceRef"));

    ASSERT_TRUE(m_ambiorix->remove_instance(DATAELEMENTS_ROOT_DM ".Network.Device", device_index));
    read_all_pending_signals();

    EXPECT_TRUE(read_al_param(remote_al_mac, "AssocWiFiNetworkDeviceRef").empty());
}

TEST_F(IEEE1905TaskTest, bridging_tuple_interface_list_reads_interface_refs)
{
    const auto al_mac  = m_local_al_mac;
    const auto if1_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:11");
    const auto if2_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:12");

    sTopologyResponsePacket packet;
    packet.interfaces      = {if1_mac, if2_mac};
    packet.bridging_tuples = {{if2_mac, if1_mac}};

    ieee1905_1::CmduMessageRx topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(al_mac, topology_rx, packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(al_mac, topology_rx));

    const auto &al = m_database->ieee1905_network->al.at(al_mac);
    ASSERT_EQ(1, al.bridging_tuples.size());

    std::string interface_list;
    EXPECT_TRUE(m_ambiorix->read_param(al.bridging_tuples[0].dm_path.path, "InterfaceList",
                                       &interface_list));

    std::vector<std::string> expected_refs = {
        "Device." + al.interfaces.at(if1_mac).dm_path.path,
        "Device." + al.interfaces.at(if2_mac).dm_path.path,
    };
    std::sort(expected_refs.begin(), expected_refs.end());

    EXPECT_EQ(expected_refs[0] + "," + expected_refs[1], interface_list);
}

TEST_F(IEEE1905TaskTest, topology_response_keeps_bridging_tuple_path_for_same_interfaces)
{
    const auto al_mac  = m_local_al_mac;
    const auto if1_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:11");
    const auto if2_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:12");

    sTopologyResponsePacket first_packet;
    first_packet.interfaces      = {if1_mac, if2_mac};
    first_packet.bridging_tuples = {{if1_mac, if2_mac}};

    ieee1905_1::CmduMessageRx first_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(al_mac, first_topology_rx, first_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(al_mac, first_topology_rx));

    const auto first_path =
        m_database->ieee1905_network->al.at(al_mac).bridging_tuples[0].dm_path.path;

    sTopologyResponsePacket second_packet;
    second_packet.interfaces      = {if1_mac, if2_mac};
    second_packet.bridging_tuples = {{if2_mac, if1_mac}};

    ieee1905_1::CmduMessageRx second_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(al_mac, second_topology_rx, second_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(al_mac, second_topology_rx));

    const auto &al = m_database->ieee1905_network->al.at(al_mac);
    ASSERT_EQ(1, al.bridging_tuples.size());
    EXPECT_EQ(first_path, al.bridging_tuples[0].dm_path.path);

    std::string interface_list;
    EXPECT_TRUE(m_ambiorix->read_param(first_path, "InterfaceList", &interface_list));
}

TEST_F(IEEE1905TaskTest, topology_response_recreates_bridging_tuple_when_interfaces_change)
{
    const auto al_mac  = m_local_al_mac;
    const auto if1_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:11");
    const auto if2_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:12");
    const auto if3_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:13");

    sTopologyResponsePacket first_packet;
    first_packet.interfaces      = {if1_mac, if2_mac, if3_mac};
    first_packet.bridging_tuples = {{if1_mac, if2_mac}};

    ieee1905_1::CmduMessageRx first_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(al_mac, first_topology_rx, first_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(al_mac, first_topology_rx));

    const auto first_path =
        m_database->ieee1905_network->al.at(al_mac).bridging_tuples[0].dm_path.path;

    sTopologyResponsePacket second_packet;
    second_packet.interfaces      = {if1_mac, if2_mac, if3_mac};
    second_packet.bridging_tuples = {{if1_mac, if3_mac}};

    ieee1905_1::CmduMessageRx second_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(al_mac, second_topology_rx, second_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(al_mac, second_topology_rx));

    const auto &al = m_database->ieee1905_network->al.at(al_mac);
    ASSERT_EQ(1, al.bridging_tuples.size());
    const auto &tuple = al.bridging_tuples[0];
    EXPECT_EQ(std::unordered_set<sMacAddr>({if1_mac, if3_mac}), tuple.interfaces);
    EXPECT_NE(first_path, tuple.dm_path.path);

    std::string interface_list;
    EXPECT_FALSE(m_ambiorix->read_param(first_path, "InterfaceList", &interface_list));
    EXPECT_TRUE(m_ambiorix->read_param(tuple.dm_path.path, "InterfaceList", &interface_list));
}

TEST_F(IEEE1905TaskTest, topology_response_materializes_added_bridging_tuple_in_new_object)
{
    const auto al_mac  = m_local_al_mac;
    const auto if1_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:11");
    const auto if2_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:12");
    const auto if3_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:13");

    sTopologyResponsePacket first_packet;
    first_packet.interfaces      = {if1_mac, if2_mac, if3_mac};
    first_packet.bridging_tuples = {{if1_mac, if2_mac}};

    ieee1905_1::CmduMessageRx first_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(al_mac, first_topology_rx, first_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(al_mac, first_topology_rx));

    const auto first_path =
        m_database->ieee1905_network->al.at(al_mac).bridging_tuples[0].dm_path.path;

    sTopologyResponsePacket second_packet;
    second_packet.interfaces      = {if1_mac, if2_mac, if3_mac};
    second_packet.bridging_tuples = {{if1_mac, if2_mac}, {if2_mac, if3_mac}};

    ieee1905_1::CmduMessageRx second_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(al_mac, second_topology_rx, second_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(al_mac, second_topology_rx));

    const auto &al = m_database->ieee1905_network->al.at(al_mac);
    ASSERT_EQ(2, al.bridging_tuples.size());

    const auto first_tuple_it = std::find_if(
        al.bridging_tuples.begin(), al.bridging_tuples.end(),
        [if1_mac, if2_mac](const auto &tuple) {
            return tuple.interfaces == std::unordered_set<sMacAddr>({if1_mac, if2_mac});
        });
    ASSERT_NE(al.bridging_tuples.end(), first_tuple_it);
    EXPECT_EQ(first_path, first_tuple_it->dm_path.path);

    const auto second_tuple_it = std::find_if(
        al.bridging_tuples.begin(), al.bridging_tuples.end(),
        [if2_mac, if3_mac](const auto &tuple) {
            return tuple.interfaces == std::unordered_set<sMacAddr>({if2_mac, if3_mac});
        });
    ASSERT_NE(al.bridging_tuples.end(), second_tuple_it);
    EXPECT_NE(first_path, second_tuple_it->dm_path.path);

    const auto second_path = second_tuple_it->dm_path.path;

    std::string interface_list;
    EXPECT_TRUE(m_ambiorix->read_param(first_path, "InterfaceList", &interface_list));
    EXPECT_TRUE(m_ambiorix->read_param(second_path, "InterfaceList", &interface_list));
}

TEST_F(IEEE1905TaskTest, higher_layer_response_materializes_identification_and_ip_addresses)
{
    const auto remote_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:26");

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {remote_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    ieee1905_1::CmduMessageRx remote_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(remote_al_mac, remote_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, remote_topology_rx));

    sHigherLayerResponsePacket higher_layer_packet;
    higher_layer_packet.set_profile_version = true;
    higher_layer_packet.profile_version     = ieee1905_1::e1905ProfileVersion::IEEE_1905_1_A;
    higher_layer_packet.friendly_name       = u8"Контроллер πрплМеш";
    higher_layer_packet.manufacturer_name   = u8"Inango";
    higher_layer_packet.manufacturer_model  = u8"Модель-1";
    higher_layer_packet.control_url         = "https://example.com/control";
    higher_layer_packet.ipv4_addresses.push_back(
        {remote_al_mac, net_utils::ipv4_from_string("192.168.10.2"),
         ieee1905_1::eIpv4AddressType::DHCP, net_utils::ipv4_from_string("192.168.10.1")});
    higher_layer_packet.ipv6_addresses.push_back(
        {remote_al_mac, "2001:db8::2", ieee1905_1::eIpv6AddressType::SLAAC, "2001:db8::1"});

    ieee1905_1::CmduMessageRx higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(
        build_higher_layer_response_cmdu(remote_al_mac, higher_layer_rx, higher_layer_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, higher_layer_rx));

    const auto &db_al = m_database->ieee1905_network->al.at(remote_al_mac);
    ASSERT_TRUE(db_al.dm_path);

    EXPECT_EQ("1905.1a", read_al_param(remote_al_mac, "Version"));
    EXPECT_EQ(higher_layer_packet.friendly_name, read_al_param(remote_al_mac, "FriendlyName"));
    EXPECT_EQ(higher_layer_packet.manufacturer_name,
              read_al_param(remote_al_mac, "ManufacturerName"));
    EXPECT_EQ(higher_layer_packet.manufacturer_model,
              read_al_param(remote_al_mac, "ManufacturerModel"));
    EXPECT_EQ(higher_layer_packet.control_url, read_al_param(remote_al_mac, "ControlURL"));

    auto ipv4_addr = net_utils::ipv4_from_string("192.168.10.2");
    auto ipv4_it   = db_al.ipv4_addresses.find({remote_al_mac, ipv4_addr});
    ASSERT_NE(ipv4_it, db_al.ipv4_addresses.end());
    EXPECT_EQ(ieee1905_1::eIpv4AddressType::DHCP, ipv4_it->second.type);
    EXPECT_EQ(net_utils::ipv4_from_string("192.168.10.1"), ipv4_it->second.dhcp_server);
    std::string ipv4_value;
    EXPECT_TRUE(m_ambiorix->read_param(ipv4_it->second.dm_path.path, "IPv4Address", &ipv4_value));
    EXPECT_EQ("192.168.10.2", ipv4_value);

    auto ipv6_it = db_al.ipv6_addresses.find({remote_al_mac, "2001:db8::2"});
    ASSERT_NE(ipv6_it, db_al.ipv6_addresses.end());
    EXPECT_EQ(ieee1905_1::eIpv6AddressType::SLAAC, ipv6_it->second.type);
    EXPECT_EQ("2001:db8::1", ipv6_it->second.origin);
    std::string ipv6_value;
    EXPECT_TRUE(m_ambiorix->read_param(ipv6_it->second.dm_path.path, "IPv6Address", &ipv6_value));
    EXPECT_EQ("2001:db8::2", ipv6_value);
}

TEST_F(IEEE1905TaskTest, higher_layer_response_serializes_ipv4_in_network_byte_order)
{
    const auto remote_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:26");

    sHigherLayerResponsePacket higher_layer_packet;
    higher_layer_packet.ipv4_addresses.push_back(
        {remote_al_mac, net_utils::ipv4_from_string("192.168.10.2"),
         ieee1905_1::eIpv4AddressType::DHCP, net_utils::ipv4_from_string("192.168.10.1")});

    ieee1905_1::CmduMessageRx higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(
        build_higher_layer_response_cmdu(remote_al_mac, higher_layer_rx, higher_layer_packet));

    const std::array<uint8_t, 16> expected_ipv4_tlv_payload = {
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x26, // iface_mac
        0x01,                               // ipv4_entry_cnt
        0x01,                               // eIpv4AddressType::DHCP
        0xc0, 0xa8, 0x0a, 0x02,             // 192.168.10.2
        0xc0, 0xa8, 0x0a, 0x01,             // 192.168.10.1
    };

    const auto *payload_begin = m_tx_buffer;
    const auto *payload_end   = m_tx_buffer + m_cmdu_tx->getMessageLength();
    const auto it = std::search(payload_begin, payload_end, expected_ipv4_tlv_payload.begin(),
                                expected_ipv4_tlv_payload.end());
    EXPECT_NE(payload_end, it);
}

TEST_F(IEEE1905TaskTest, link_metric_response_updates_and_replaces_interface_links)
{
    using sRef = son::db::ieee1905_network_db::sAL::sRef;

    const auto remote_al_mac      = tlvf::mac_from_string("aa:bb:cc:dd:ee:d1");
    const auto remote_if_mac      = tlvf::mac_from_string("aa:bb:cc:dd:ee:d2");
    const auto neighbor_if1_mac   = tlvf::mac_from_string("aa:bb:cc:dd:ee:e1");
    const auto neighbor_if2_mac   = tlvf::mac_from_string("aa:bb:cc:dd:ee:e2");
    const auto first_packet_error = 11;
    const auto first_rx_error     = 22;
    const auto second_packet_err  = 111;
    const auto second_rx_error    = 222;

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {remote_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    sTopologyResponsePacket remote_packet;
    remote_packet.interfaces = {remote_if_mac};
    ieee1905_1::CmduMessageRx remote_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(remote_al_mac, remote_topology_rx, remote_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, remote_topology_rx));

    ieee1905_1::CmduMessageRx remote_higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(remote_al_mac, remote_higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, remote_higher_layer_rx));

    ieee1905_1::tlvTransmitterLinkMetric::sInterfacePairInfo tx_pair1 = {
        .rc_interface_mac       = remote_if_mac,
        .neighbor_interface_mac = neighbor_if1_mac,
        .link_metric_info =
            {
                .intfType = ieee1905_1::IEEE_802_11AX,
                .IEEE802_1BridgeFlag =
                    ieee1905_1::tlvTransmitterLinkMetric::LINK_DOES_INCLUDE_ONE_OR_MORE_BRIDGE,
                .packet_errors           = first_packet_error,
                .transmitted_packets     = 33,
                .mac_throughput_capacity = 44,
                .link_availability       = 55,
                .phy_rate                = 66,
            },
    };

    ieee1905_1::tlvReceiverLinkMetric::sInterfacePairInfo rx_pair1 = {
        .rc_interface_mac       = remote_if_mac,
        .neighbor_interface_mac = neighbor_if1_mac,
        .link_metric_info =
            {
                .intfType         = ieee1905_1::IEEE_802_11AX,
                .packet_errors    = first_rx_error,
                .packets_received = 77,
                .rssi_db          = 88,
            },
    };

    ieee1905_1::CmduMessageRx first_link_metric_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_link_metric_response_cmdu(first_link_metric_rx, remote_al_mac, m_local_al_mac,
                                                {tx_pair1}, {rx_pair1}));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, first_link_metric_rx));

    auto &remote_al = m_database->ieee1905_network->al[remote_al_mac];
    const sRef first_ref{m_local_al_mac, neighbor_if1_mac};
    ASSERT_EQ(1, remote_al.interfaces[remote_if_mac].links.count(first_ref));
    auto &first_link = remote_al.interfaces[remote_if_mac].links.at(first_ref);
    EXPECT_EQ(first_packet_error, int(first_link.tx_link_metric.packet_errors));
    EXPECT_EQ(first_rx_error, int(first_link.rx_link_metric.packet_errors));
    const auto first_link_path = first_link.dm_path.path;

    bool dot1bridge                 = false;
    uint32_t packet_errors          = 0;
    uint32_t packet_errors_received = 0;
    auto metric_path                = first_link.dm_path.subpath(".Metric").path;
    EXPECT_TRUE(m_ambiorix->read_param(metric_path, "IEEE802dot1Bridge", &dot1bridge));
    EXPECT_TRUE(m_ambiorix->read_param(metric_path, "PacketErrors", &packet_errors));
    EXPECT_TRUE(
        m_ambiorix->read_param(metric_path, "PacketErrorsReceived", &packet_errors_received));
    EXPECT_TRUE(dot1bridge);
    EXPECT_EQ(first_packet_error, int(packet_errors));
    EXPECT_EQ(first_rx_error, int(packet_errors_received));

    ieee1905_1::tlvTransmitterLinkMetric::sInterfacePairInfo tx_pair2 = {
        .rc_interface_mac       = remote_if_mac,
        .neighbor_interface_mac = neighbor_if2_mac,
        .link_metric_info =
            {
                .intfType = ieee1905_1::IEEE_802_11AX,
                .IEEE802_1BridgeFlag =
                    ieee1905_1::tlvTransmitterLinkMetric::LINK_DOES_INCLUDE_ONE_OR_MORE_BRIDGE,
                .packet_errors           = second_packet_err,
                .transmitted_packets     = 33,
                .mac_throughput_capacity = 44,
                .link_availability       = 55,
                .phy_rate                = 66,
            },
    };

    ieee1905_1::tlvReceiverLinkMetric::sInterfacePairInfo rx_pair2 = {
        .rc_interface_mac       = remote_if_mac,
        .neighbor_interface_mac = neighbor_if2_mac,
        .link_metric_info =
            {
                .intfType         = ieee1905_1::IEEE_802_11AX,
                .packet_errors    = second_rx_error,
                .packets_received = 77,
                .rssi_db          = 88,
            },
    };

    ieee1905_1::CmduMessageRx second_link_metric_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_link_metric_response_cmdu(second_link_metric_rx, remote_al_mac,
                                                m_local_al_mac, {tx_pair2}, {rx_pair2}));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, second_link_metric_rx));

    const sRef second_ref{m_local_al_mac, neighbor_if2_mac};
    ASSERT_EQ(0, remote_al.interfaces[remote_if_mac].links.count(first_ref));
    ASSERT_EQ(1, remote_al.interfaces[remote_if_mac].links.count(second_ref));

    auto &second_link = remote_al.interfaces[remote_if_mac].links.at(second_ref);
    EXPECT_EQ(second_packet_err, int(second_link.tx_link_metric.packet_errors));
    EXPECT_EQ(second_rx_error, int(second_link.rx_link_metric.packet_errors));
    EXPECT_NE(first_link_path, second_link.dm_path.path);

    std::string old_link_ieee1905_id;
    EXPECT_FALSE(m_ambiorix->read_param(first_link_path, "IEEE1905Id", &old_link_ieee1905_id));
}

TEST_F(IEEE1905TaskTest, topology_response_preserves_existing_interface_links)
{
    using sRef = son::db::ieee1905_network_db::sAL::sRef;

    const auto remote_al_mac    = tlvf::mac_from_string("aa:bb:cc:dd:ee:f1");
    const auto remote_if_mac    = tlvf::mac_from_string("aa:bb:cc:dd:ee:f2");
    const auto neighbor_if_mac  = tlvf::mac_from_string("aa:bb:cc:dd:ee:f3");
    const auto packet_errors_tx = 17;
    const auto packet_errors_rx = 23;

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {remote_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    sTopologyResponsePacket remote_packet;
    remote_packet.interfaces = {remote_if_mac};
    ieee1905_1::CmduMessageRx remote_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(remote_al_mac, remote_topology_rx, remote_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, remote_topology_rx));

    ieee1905_1::CmduMessageRx remote_higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(remote_al_mac, remote_higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, remote_higher_layer_rx));

    ieee1905_1::tlvTransmitterLinkMetric::sInterfacePairInfo tx_pair = {
        .rc_interface_mac       = remote_if_mac,
        .neighbor_interface_mac = neighbor_if_mac,
        .link_metric_info =
            {
                .intfType = ieee1905_1::IEEE_802_11AX,
                .IEEE802_1BridgeFlag =
                    ieee1905_1::tlvTransmitterLinkMetric::LINK_DOES_INCLUDE_ONE_OR_MORE_BRIDGE,
                .packet_errors           = packet_errors_tx,
                .transmitted_packets     = 33,
                .mac_throughput_capacity = 44,
                .link_availability       = 55,
                .phy_rate                = 66,
            },
    };

    ieee1905_1::tlvReceiverLinkMetric::sInterfacePairInfo rx_pair = {
        .rc_interface_mac       = remote_if_mac,
        .neighbor_interface_mac = neighbor_if_mac,
        .link_metric_info =
            {
                .intfType         = ieee1905_1::IEEE_802_11AX,
                .packet_errors    = packet_errors_rx,
                .packets_received = 77,
                .rssi_db          = 88,
            },
    };

    ieee1905_1::CmduMessageRx link_metric_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_link_metric_response_cmdu(link_metric_rx, remote_al_mac, m_local_al_mac,
                                                {tx_pair}, {rx_pair}));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, link_metric_rx));

    auto &remote_al = m_database->ieee1905_network->al[remote_al_mac];
    const sRef link_ref{m_local_al_mac, neighbor_if_mac};
    auto &link           = remote_al.interfaces[remote_if_mac].links.at(link_ref);
    const auto link_path = link.dm_path.path;

    uint32_t link_count = 0;
    EXPECT_TRUE(m_ambiorix->read_param(remote_al.interfaces[remote_if_mac].dm_path.path,
                                       "LinkNumberOfEntries", &link_count));
    EXPECT_EQ(1, link_count);

    ieee1905_1::CmduMessageRx next_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(remote_al_mac, next_topology_rx, remote_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, next_topology_rx));

    ASSERT_EQ(1, remote_al.interfaces[remote_if_mac].links.count(link_ref));
    EXPECT_EQ(link_path, remote_al.interfaces[remote_if_mac].links.at(link_ref).dm_path.path);
    EXPECT_EQ(packet_errors_tx,
              remote_al.interfaces[remote_if_mac].links.at(link_ref).tx_link_metric.packet_errors);
    EXPECT_EQ(packet_errors_rx,
              remote_al.interfaces[remote_if_mac].links.at(link_ref).rx_link_metric.packet_errors);

    EXPECT_TRUE(m_ambiorix->read_param(remote_al.interfaces[remote_if_mac].dm_path.path,
                                       "LinkNumberOfEntries", &link_count));
    EXPECT_EQ(1, link_count);
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

TEST_F(IEEE1905TaskTest, periodic_topology_query_is_sent_after_interval_from_last_response)
{
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    clear_sent_queries();

    advance_time(son::ieee1905_task::periodic_topology_requery_interval - std::chrono::seconds(1));
    EXPECT_TRUE(m_query_sender->topology_queries.empty());

    advance_time(std::chrono::seconds(1));
    EXPECT_TRUE(topology_query_sent_to(m_local_al_mac));
}

TEST_F(IEEE1905TaskTest, periodic_link_metric_query_is_sent_immediately_and_then_rearmed)
{
    const auto interval = std::chrono::seconds(5);

    m_database->config.link_metrics_request_interval_seconds = interval;

    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    clear_sent_queries();

    // With min() sentinel, the first periodic query is emitted on first work() tick.
    advance_time(std::chrono::seconds(1));
    EXPECT_TRUE(link_metric_query_sent_to(m_local_al_mac));

    clear_sent_queries();

    advance_time(interval + son::ieee1905_task::link_metric_response_requery_delay_guard -
                 std::chrono::seconds(1));
    EXPECT_TRUE(m_query_sender->link_metric_queries.empty());

    advance_time(std::chrono::seconds(1));
    EXPECT_TRUE(link_metric_query_sent_to(m_local_al_mac));
}

TEST_F(IEEE1905TaskTest, remote_discovery_initial_higher_layer_query_rearms_periodic_deadline)
{
    const auto interval      = std::chrono::seconds(5);
    const auto remote_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:91");

    m_database->config.higher_layer_request_interval_seconds = interval;

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {remote_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));
    ASSERT_TRUE(higher_layer_query_sent_to(remote_al_mac));

    ieee1905_1::CmduMessageRx remote_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(remote_al_mac, remote_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, remote_topology_rx));

    clear_sent_queries();

    advance_time(interval - std::chrono::seconds(1));
    EXPECT_TRUE(m_query_sender->higher_layer_queries.empty());

    advance_time(std::chrono::seconds(1));
    EXPECT_TRUE(higher_layer_query_sent_to(remote_al_mac));
}

TEST_F(IEEE1905TaskTest, periodic_higher_layer_query_is_sent_to_local_after_interval)
{
    const auto interval = std::chrono::seconds(5);

    m_database->config.higher_layer_request_interval_seconds = interval;

    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    clear_sent_queries();

    advance_time(interval - std::chrono::seconds(1));
    EXPECT_TRUE(m_query_sender->higher_layer_queries.empty());

    advance_time(std::chrono::seconds(1));
    EXPECT_TRUE(higher_layer_query_sent_to(m_local_al_mac));
}

TEST_F(IEEE1905TaskTest, higher_layer_response_rearms_next_periodic_query)
{
    const auto interval      = std::chrono::seconds(5);
    const auto remote_al_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:92");

    m_database->config.higher_layer_request_interval_seconds = interval;

    sTopologyResponsePacket local_packet;
    local_packet.ieee1905_neighbors[m_local_al_mac] = {remote_al_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    ieee1905_1::CmduMessageRx remote_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(remote_al_mac, remote_topology_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, remote_topology_rx));

    ieee1905_1::CmduMessageRx higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(remote_al_mac, higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(remote_al_mac, higher_layer_rx));

    clear_sent_queries();

    advance_time(interval - std::chrono::seconds(1));
    EXPECT_TRUE(m_query_sender->higher_layer_queries.empty());

    advance_time(std::chrono::seconds(1));
    EXPECT_TRUE(higher_layer_query_sent_to(remote_al_mac));
}

TEST_F(IEEE1905TaskTest, link_metric_response_delays_next_periodic_query_with_guard)
{
    const auto interval        = std::chrono::seconds(5);
    const auto local_if_mac    = tlvf::mac_from_string("11:22:33:44:55:77");
    const auto neighbor_if_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:77");

    m_database->config.link_metrics_request_interval_seconds = interval;

    sTopologyResponsePacket local_packet;
    local_packet.interfaces = {local_if_mac};
    ieee1905_1::CmduMessageRx local_topology_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_topology_response_cmdu(m_local_al_mac, local_topology_rx, local_packet));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, local_topology_rx));

    clear_sent_queries();

    advance_time(interval);
    EXPECT_TRUE(link_metric_query_sent_to(m_local_al_mac));

    ieee1905_1::tlvTransmitterLinkMetric::sInterfacePairInfo tx_pair = {
        .rc_interface_mac       = local_if_mac,
        .neighbor_interface_mac = neighbor_if_mac,
        .link_metric_info =
            {
                .intfType = ieee1905_1::IEEE_802_11AX,
                .IEEE802_1BridgeFlag =
                    ieee1905_1::tlvTransmitterLinkMetric::LINK_DOES_INCLUDE_ONE_OR_MORE_BRIDGE,
                .packet_errors           = 1,
                .transmitted_packets     = 2,
                .mac_throughput_capacity = 3,
                .link_availability       = 4,
                .phy_rate                = 5,
            },
    };

    ieee1905_1::tlvReceiverLinkMetric::sInterfacePairInfo rx_pair = {
        .rc_interface_mac       = local_if_mac,
        .neighbor_interface_mac = neighbor_if_mac,
        .link_metric_info =
            {
                .intfType         = ieee1905_1::IEEE_802_11AX,
                .packet_errors    = 6,
                .packets_received = 7,
                .rssi_db          = 8,
            },
    };

    ieee1905_1::CmduMessageRx link_metric_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_link_metric_response_cmdu(link_metric_rx, m_local_al_mac, m_local_al_mac,
                                                {tx_pair}, {rx_pair}));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, link_metric_rx));

    clear_sent_queries();

    advance_time(interval);
    EXPECT_TRUE(m_query_sender->link_metric_queries.empty());

    advance_time(son::ieee1905_task::link_metric_response_requery_delay_guard);
    EXPECT_TRUE(link_metric_query_sent_to(m_local_al_mac));
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
    ASSERT_TRUE(build_higher_layer_response_cmdu(mac1_al_mac, mac1_higher_layer_rx));
    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(mac1_al_mac, mac1_higher_layer_rx));

    ieee1905_1::CmduMessageRx mac2_higher_layer_rx(m_rx_buffer, sizeof(m_rx_buffer));
    ASSERT_TRUE(build_higher_layer_response_cmdu(mac2_al_mac, mac2_higher_layer_rx));
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
    ASSERT_TRUE(build_higher_layer_response_cmdu(mac1_al_mac, mac1_higher_layer_rx));
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
    ASSERT_TRUE(build_higher_layer_response_cmdu(n1_al_mac, n1_higher_layer_rx));
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
    ASSERT_TRUE(build_higher_layer_response_cmdu(n1_al_mac, n1_higher_layer_rx));
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

TEST_F(IEEE1905TaskTest, nonstandard_topology_response_with_device_identification_tlv)
{
    const std::string friendly_name      = "Topology Controller";
    const std::string manufacturer_name  = "Inango";
    const std::string manufacturer_model = "Test Model 1905";

    ieee1905_1::CmduMessageRx topology_rx(m_rx_buffer, sizeof(m_rx_buffer));

    ASSERT_TRUE(m_cmdu_tx->create(0, ieee1905_1::eMessageType::TOPOLOGY_RESPONSE_MESSAGE));

    auto device_information = m_cmdu_tx->addClass<ieee1905_1::tlvDeviceInformation>();
    ASSERT_NE(device_information, nullptr);
    device_information->mac() = m_local_al_mac;

    auto device_id_tlv = m_cmdu_tx->addClass<ieee1905_1::tlvDeviceIdentification>();
    ASSERT_NE(device_id_tlv, nullptr);

    ASSERT_TRUE(device_id_tlv->set_friendly_name(friendly_name.data(), friendly_name.size()));
    ASSERT_TRUE(
        device_id_tlv->set_manufacturer_name(manufacturer_name.data(), manufacturer_name.size()));
    ASSERT_TRUE(device_id_tlv->set_manufacturer_model(manufacturer_model.data(),
                                                      manufacturer_model.size()));

    ASSERT_TRUE(m_cmdu_tx->finalize());
    std::copy_n(m_tx_buffer, m_cmdu_tx->getMessageLength(), m_rx_buffer);
    ASSERT_TRUE(topology_rx.parse());

    ASSERT_TRUE(m_task->handle_ieee1905_1_msg(m_local_al_mac, topology_rx));

    const auto &db_al = m_database->ieee1905_network->al.at(m_local_al_mac);
    EXPECT_EQ(friendly_name, db_al.friendly_name);
    EXPECT_EQ(manufacturer_name, db_al.manufacturer_name);
    EXPECT_EQ(manufacturer_model, db_al.manufacturer_model);
    EXPECT_EQ(friendly_name, read_al_param(m_local_al_mac, "FriendlyName"));
    EXPECT_EQ(manufacturer_name, read_al_param(m_local_al_mac, "ManufacturerName"));
    EXPECT_EQ(manufacturer_model, read_al_param(m_local_al_mac, "ManufacturerModel"));
}

} // namespace
