/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "ieee1905_task.h"

#include <bcl/network/network_utils.h>
#include <tlvf/ieee_1905_1/tlv1905NeighborDevice.h>
#include <tlvf/ieee_1905_1/tlv1905ProfileVersion.h>
#include <tlvf/ieee_1905_1/tlvAlMacAddress.h>
#include <tlvf/ieee_1905_1/tlvControlUrl.h>
#include <tlvf/ieee_1905_1/tlvDeviceIdentification.h>
#include <tlvf/ieee_1905_1/tlvDeviceInformation.h>
#include <tlvf/ieee_1905_1/tlvIpv4.h>
#include <tlvf/ieee_1905_1/tlvIpv6.h>
#include <tlvf/ieee_1905_1/tlvNon1905neighborDeviceList.h>
#include <tlvf/wfa_map/tlvClientAssociationEvent.h>

#include <easylogging++.h>

#include <arpa/inet.h>

#include <cstring>
#include <unordered_set>

using namespace son;

using net_utils = beerocks::net::network_utils;

constexpr std::chrono::seconds ieee1905_task::topology_response_timeout;
constexpr std::chrono::seconds ieee1905_task::higher_layer_response_timeout;
constexpr std::chrono::seconds ieee1905_task::periodic_topology_requery_interval;

template <typename Tuple> static auto unwrap(Tuple &&result)
{
    return std::get<0>(result) ? &std::get<1>(result) : nullptr;
}

static std::string add_device_prefix(const std::string &path)
{
    constexpr char device_prefix[] = "Device.";

    if (path.empty()) {
        return {};
    }

    if (path.compare(0, sizeof(device_prefix) - 1, device_prefix) == 0) {
        return path;
    }

    return device_prefix + path;
}

static std::string fixed_utf8_string(const uint8_t *data, size_t len)
{
    if (!data) {
        return {};
    }

    while (len > 0 && data[len - 1] == '\0') {
        --len;
    }

    return std::string(reinterpret_cast<const char *>(data), len);
}

static std::string ipv6_to_string(const uint8_t *ipv6)
{
    if (!ipv6) {
        return {};
    }

    char out[INET6_ADDRSTRLEN] = {};
    if (!inet_ntop(AF_INET6, ipv6, out, sizeof(out))) {
        return {};
    }

    return out;
}

static auto ipv4_from_u32(uint32_t ipv4)
{
    beerocks::net::sIpv4Addr addr = {};

    addr.oct[0] = (ipv4 >> 24) & 0xFF;
    addr.oct[1] = (ipv4 >> 16) & 0xFF;
    addr.oct[2] = (ipv4 >> 8) & 0xFF;
    addr.oct[3] = ipv4 & 0xFF;

    return addr;
}

static const char *ipv4_address_type_to_dm_string(ieee1905_1::eIpv4AddressType type)
{
    switch (type) {
    case ieee1905_1::eIpv4AddressType::UNKNOWN:
        return "Unknown";
    case ieee1905_1::eIpv4AddressType::DHCP:
        return "DHCP";
    case ieee1905_1::eIpv4AddressType::STATIC:
        return "Static";
    case ieee1905_1::eIpv4AddressType::AUTO_IP:
        return "Auto-IP";
    default:
        return "Unknown";
    }
}

static const char *ipv6_address_type_to_dm_string(ieee1905_1::eIpv6AddressType type,
                                                  const std::string &address)
{
    // Detect link-local by fe80::/10 prefix (inet_ntop always produces lowercase)
    if (address.compare(0, 4, "fe80") == 0) {
        return "LinkLocal";
    }
    switch (type) {
    case ieee1905_1::eIpv6AddressType::UNKNOWN:
        return "Unknown";
    case ieee1905_1::eIpv6AddressType::DHCP:
        return "DHCP";
    case ieee1905_1::eIpv6AddressType::STATIC:
        return "Static";
    case ieee1905_1::eIpv6AddressType::SLAAC:
        return "SLAAC";
    default:
        return "Unknown";
    }
}

ieee1905_task::ieee1905_task(db &database, ieee1905_1::CmduMessageTx &cmdu_tx,
                             std::unique_ptr<IEEE1905QuerySender> query_sender_, now_f now_)
    : task("ieee1905_task"), database(database), cmdu_tx(cmdu_tx),
      query_sender(std::move(query_sender_)), now(std::move(now_))
{
    database.assign_ieee1905_task_id(id);
    LOG_IF(!query_sender, FATAL) << "IEEE1905 query sender is a null pointer!";
    LOG_IF(!now, FATAL) << "IEEE1905 clock callback is empty!";

    bool network_enable = true;
    auto ambiorix       = database.get_ambiorix_obj();

    if (!ambiorix ||
        !ambiorix->read_param(IEEE1905_ROOT_DM ".Network", "Enable", &network_enable)) {
        LOG(WARNING) << "Failed to read " << IEEE1905_ROOT_DM
                     << ".Network.Enable, using default: " << network_enable;
    }

    set_ieee1905_network_enabled(network_enable);
}

void ieee1905_task::work()
{
    if (!database.ieee1905_network) {
        return;
    }

    std::vector<sMacAddr> topology_timeouts;
    std::vector<sMacAddr> higher_layer_timeouts;
    const auto current_time = now();

    // Check for first topology/higher layer response timeouts
    for (const auto &entry : m_als) {
        const auto &al_mac = entry.first;
        const auto &al     = entry.second;

        if (current_time >= al.first_topology_query_deadline) {
            topology_timeouts.push_back(al_mac);
            continue; // we'll remove this AL, so don't care about anything else
        }

        if (current_time >= al.first_higher_layer_query_deadline) {
            higher_layer_timeouts.push_back(al_mac);
        }
    }

    for (const auto &al_mac : topology_timeouts) {
        handle_topology_timeout(al_mac);
    }

    for (const auto &al_mac : higher_layer_timeouts) {
        handle_higher_layer_timeout(al_mac);
    }

    // periodic (re-)queries
    for (auto &entry : m_als) {
        const auto &al_mac = entry.first;
        auto &al           = entry.second;

        if (current_time >= al.next_periodic_topology_query_deadline) {
            if (!query_sender->send_topology_query(al_mac, cmdu_tx)) {
                LOG(ERROR) << "Failed to send periodic topology query to " << al_mac;
            }
            al.next_periodic_topology_query_deadline =
                current_time + periodic_topology_requery_interval;
        }
    }
}

bool ieee1905_task::handle_ieee1905_1_msg(const sMacAddr &src_mac,
                                          ieee1905_1::CmduMessageRx &cmdu_rx)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::TOPOLOGY_RESPONSE_MESSAGE:
        return handle_topology_response(src_mac, cmdu_rx);
    case ieee1905_1::eMessageType::HIGHER_LAYER_RESPONSE_MESSAGE:
        return handle_higher_layer_response(src_mac, cmdu_rx);
    case ieee1905_1::eMessageType::LINK_METRIC_RESPONSE_MESSAGE:
        return handle_link_metric_response(src_mac, cmdu_rx);
    case ieee1905_1::eMessageType::TOPOLOGY_NOTIFICATION_MESSAGE:
        return handle_topology_notification(src_mac, cmdu_rx);
    default:
        return false;
    }
}

void ieee1905_task::set_ieee1905_network_enabled(bool enabled)
{
    if (!enabled) {
        status_pending = {};
        database.ieee1905_network.reset();
        m_als.clear();
        return;
    }

    if (!database.ieee1905_network) {
        database.ieee1905_network = std::make_unique<db::ieee1905_network_db>();
        m_als.clear();
        status_pending = single_shot_counter(1, [this]() { set_network_status("Available"); });
        set_network_status("Incomplete");

        if (!start_local_al_discovery()) {
            LOG(ERROR) << "Failed to start local IEEE1905 discovery";
        }
    }
}

bool ieee1905_task::set_network_status(const std::string &status)
{
    auto ambiorix = database.get_ambiorix_obj();
    if (!ambiorix) {
        LOG(ERROR) << "Failed to update IEEE1905 network status: Ambiorix is not available";
        return false;
    }

    if (!ambiorix->set(std::string(IEEE1905_ROOT_DM) + ".Network", "Status", status)) {
        LOG(ERROR) << "Failed to set " << IEEE1905_ROOT_DM << ".Network.Status to " << status;
        return false;
    }

    return true;
}

bool ieee1905_task::start_local_al_discovery()
{
    const auto &al_mac = database.get_local_bridge_mac();
    auto &al           = m_als[al_mac];

    al.info_pending = single_shot_counter(1, [this]() { status_pending.count_down(); });
    al.topology_response_pending = single_shot_counter(1, [this, al_mac]() {
        auto al_it = m_als.find(al_mac);
        if (al_it == m_als.end()) {
            return;
        }
        al_it->second.info_pending.count_down();
    });

    al.first_topology_query_deadline = now() + topology_response_timeout;
    if (!query_sender->send_topology_query(al_mac, cmdu_tx)) {
        LOG(ERROR) << "Failed to send topology query to " << al_mac;
        return false;
    }

    return true;
}

bool ieee1905_task::start_remote_al_discovery(const sMacAddr &al_mac)
{
    auto &al = m_als[al_mac];
    database.ieee1905_network->al[al_mac];
    status_pending.count_up();

    al.info_pending = single_shot_counter(2, [this, al_mac]() { complete_remote_al(al_mac); });
    al.topology_response_pending     = single_shot_counter(1, [this, al_mac]() {
        auto al_it = m_als.find(al_mac);
        if (al_it == m_als.end()) {
            return;
        }
        al_it->second.info_pending.count_down();
    });
    al.higher_layer_response_pending = single_shot_counter(1, [this, al_mac]() {
        auto al_it = m_als.find(al_mac);
        if (al_it == m_als.end()) {
            return;
        }
        al_it->second.info_pending.count_down();
    });

    const auto start                     = now();
    al.first_topology_query_deadline     = start + topology_response_timeout;
    al.first_higher_layer_query_deadline = start + higher_layer_response_timeout;

    bool ret = true;
    if (!query_sender->send_topology_query(al_mac, cmdu_tx)) {
        LOG(ERROR) << "Failed to send topology query to " << al_mac;
        ret = false;
    }

    if (!query_sender->send_higher_layer_query(al_mac, cmdu_tx)) {
        LOG(ERROR) << "Failed to send higher layer query to " << al_mac;
        ret = false;
    }

    return ret;
}

bool ieee1905_task::materialize_local_al()
{
    const auto &al_mac = database.get_local_bridge_mac();

    auto al_it = m_als.find(al_mac);
    if (al_it == m_als.end()) {
        return false;
    }

    auto &db_al            = database.ieee1905_network->al[al_mac];
    db_al.version_is_1905a = true;
    if (!ensure_al_in_dm(al_mac)) {
        LOG(ERROR) << "Failed to materialize local AL " << al_mac << " in DM";
        return false;
    }

    return true;
}

bool ieee1905_task::ensure_al_in_dm(const sMacAddr &al_mac)
{
    auto db_al_it = database.ieee1905_network->al.find(al_mac);
    if (db_al_it == database.ieee1905_network->al.end()) {
        LOG(ERROR) << "Failed to add AL " << al_mac << " to DM: AL DB entry is missing";
        return false;
    }

    auto &db_al = db_al_it->second;
    if (db_al.dm_path) {
        return update_al_in_dm(al_mac);
    }

    auto ambiorix = database.get_ambiorix_obj();
    if (!ambiorix) {
        LOG(ERROR) << "Failed to add AL " << al_mac << " to DM: Ambiorix is not available";
        return false;
    }

    auto path = ambiorix->add_instance(std::string(IEEE1905_ROOT_DM) + ".Network.AL");
    if (path.empty()) {
        LOG(ERROR) << "Failed to add AL " << al_mac << " to DM";
        return false;
    }

    if (!ambiorix->set(path, "IEEE1905Id", al_mac)) {
        LOG(ERROR) << "Failed to set IEEE1905Id for AL " << al_mac;
        const auto instance = db::get_dm_index_from_path(path);
        if (!ambiorix->remove_instance(instance.first, instance.second)) {
            LOG(WARNING) << "Failed to rollback AL DM instance " << path;
        }
        return false;
    }

    db_al.dm_path.dm   = ambiorix;
    db_al.dm_path.path = std::move(path);

    bool ok = update_al_in_dm(al_mac);

    // update incoming references
    const auto ieee1905_device_ref = add_device_prefix(db_al.dm_path.path);
    for (const auto &ref : db_al.references) {
        auto source_al_it = database.ieee1905_network->al.find(ref.al_mac);
        if (source_al_it == database.ieee1905_network->al.end()) {
            continue;
        }

        auto &source_al = source_al_it->second;
        if (!source_al.dm_path) {
            continue;
        }

        auto source_if_it = source_al.interfaces.find(ref.if_mac);
        if (source_if_it == source_al.interfaces.end()) {
            continue;
        }

        auto &source_if         = source_if_it->second;
        auto source_neighbor_it = source_if.ieee1905_neighbors.find(al_mac);
        if (source_neighbor_it == source_if.ieee1905_neighbors.end() ||
            !source_neighbor_it->second.dm_path) {
            continue;
        }

        ok &= source_neighbor_it->second.dm_path.set("IEEE1905DeviceRef", ieee1905_device_ref);
    }

    return ok;
}

bool ieee1905_task::update_al_in_dm(const sMacAddr &al_mac)
{
    auto db_al_it = database.ieee1905_network->al.find(al_mac);
    if (db_al_it == database.ieee1905_network->al.end()) {
        return false;
    }

    auto &al = db_al_it->second;
    if (!al.dm_path) {
        return true;
    }

    bool ok = true;
    ok &= al.dm_path.set("Version", al.version_is_1905a ? "1905.1a" : "1905.1");
    ok &= al.dm_path.set("FriendlyName", al.friendly_name);
    ok &= al.dm_path.set("ManufacturerName", al.manufacturer_name);
    ok &= al.dm_path.set("ManufacturerModel", al.manufacturer_model);
    ok &= al.dm_path.set("ControlURL", al.control_url);

    for (auto &ipv4_entry : al.ipv4_addresses) {
        const auto &key = ipv4_entry.first;
        auto &ipv4      = ipv4_entry.second;

        if (!ipv4.dm_path) {
            ipv4.dm_path = al.dm_path.add_instance(".IPv4Address");
            if (!ipv4.dm_path) {
                ok = false;
                continue;
            }
        }

        ok &= ipv4.dm_path.set("MACAddress", key.mac);
        ok &= ipv4.dm_path.set("IPv4Address", net_utils::ipv4_to_string(key.address));
        ok &= ipv4.dm_path.set("IPv4AddressType", ipv4_address_type_to_dm_string(ipv4.type));
        ok &= ipv4.dm_path.set("DHCPServer", net_utils::ipv4_to_string(ipv4.dhcp_server));
    }

    for (auto &ipv6_entry : al.ipv6_addresses) {
        const auto &key = ipv6_entry.first;
        auto &ipv6      = ipv6_entry.second;

        if (!ipv6.dm_path) {
            ipv6.dm_path = al.dm_path.add_instance(".IPv6Address");
            if (!ipv6.dm_path) {
                ok = false;
                continue;
            }
        }

        ok &= ipv6.dm_path.set("MACAddress", key.mac);
        ok &= ipv6.dm_path.set("IPv6Address", key.address);
        ok &= ipv6.dm_path.set("IPv6AddressType",
                               ipv6_address_type_to_dm_string(ipv6.type, key.address));
        ok &= ipv6.dm_path.set("IPv6AddressOrigin", ipv6.origin);
    }

    for (auto &iface_entry : al.interfaces) {
        const auto &if_mac = iface_entry.first;
        auto &iface        = iface_entry.second;

        if (!iface.dm_path) {
            iface.dm_path = al.dm_path.add_instance(".Interface");
            if (!iface.dm_path) {
                ok = false;
                continue;
            }
        }

        ok &= iface.dm_path.set("InterfaceId", if_mac);
        ok &= iface.dm_path.set("MediaType", ieee1905_1::eMediaType_str(iface.type));

        for (auto &neighbor_entry : iface.non_1905_neighbors) {
            const auto &neighbor_mac = neighbor_entry.first;
            auto &neighbor_dm_path   = neighbor_entry.second;

            if (!neighbor_dm_path) {
                neighbor_dm_path = iface.dm_path.add_instance(".NonIEEE1905Neighbor");
                if (!neighbor_dm_path) {
                    ok = false;
                    continue;
                }
            }

            ok &= neighbor_dm_path.set("NeighborInterfaceId", neighbor_mac);
        }

        for (auto &neighbor_entry : iface.ieee1905_neighbors) {
            const auto &neighbor_al_mac = neighbor_entry.first;
            auto &neighbor              = neighbor_entry.second;

            if (!neighbor.dm_path) {
                neighbor.dm_path = iface.dm_path.add_instance(".IEEE1905Neighbor");
                if (!neighbor.dm_path) {
                    ok = false;
                    continue;
                }
            }

            ok &= neighbor.dm_path.set("NeighborDeviceId", neighbor_al_mac);
            auto neighbor_al_it = database.ieee1905_network->al.find(neighbor_al_mac);
            const auto ieee1905_device_ref =
                (neighbor_al_it != database.ieee1905_network->al.end())
                    ? add_device_prefix(neighbor_al_it->second.dm_path.path)
                    : std::string{};
            ok &= neighbor.dm_path.set("IEEE1905DeviceRef", ieee1905_device_ref);
            ok &= neighbor.dm_path.set("IEEE802dot1Bridge", neighbor.ieee802dot1_bridge);
        }

        for (auto &link_entry : iface.links) {
            const auto &ref = link_entry.first;
            auto &link      = link_entry.second;

            if (!link.dm_path) {
                link.dm_path = iface.dm_path.add_instance(".Link");
                if (!link.dm_path) {
                    ok = false;
                    continue;
                }
            }

            auto media_type = link.tx_link_metric.intfType;
            if (media_type == ieee1905_1::UNKNOWN_MEDIA) {
                media_type = link.rx_link_metric.intfType;
            }

            ok &= link.dm_path.set("InterfaceId", ref.if_mac);
            ok &= link.dm_path.set("IEEE1905Id", ref.al_mac);
            ok &= link.dm_path.set("MediaType", ieee1905_1::eMediaType_str(media_type));

            auto metric = link.dm_path.subpath(".Metric");
            auto dot1bridge =
                link.tx_link_metric.IEEE802_1BridgeFlag ==
                ieee1905_1::tlvTransmitterLinkMetric::LINK_DOES_INCLUDE_ONE_OR_MORE_BRIDGE;

            ok &= metric.set("IEEE802dot1Bridge", dot1bridge);
            ok &= metric.set("PacketErrors", link.tx_link_metric.packet_errors);
            ok &= metric.set("PacketErrorsReceived", link.rx_link_metric.packet_errors);
            ok &= metric.set("TransmittedPackets", link.tx_link_metric.transmitted_packets);
            ok &= metric.set("PacketsReceived", link.rx_link_metric.packets_received);
            ok &= metric.set("MACThroughputCapacity", link.tx_link_metric.mac_throughput_capacity);
            ok &= metric.set("LinkAvailability", link.tx_link_metric.link_availability);
            ok &= metric.set("PHYRate", link.tx_link_metric.phy_rate);
            ok &= metric.set("RSSI", link.rx_link_metric.rssi_db);
        }
    }

    LOG_IF(!ok, ERROR) << "Failed to update AL " << al_mac << " in DM";

    return ok;
}

void ieee1905_task::complete_remote_al(const sMacAddr &al_mac)
{
    auto db_al_it = database.ieee1905_network->al.find(al_mac);
    if (db_al_it == database.ieee1905_network->al.end()) {
        return;
    }

    if (!ensure_al_in_dm(al_mac)) {
        LOG(ERROR) << "Failed to materialize AL " << al_mac << " in DM";
    }

    status_pending.count_down();
}

void ieee1905_task::handle_topology_timeout(const sMacAddr &al_mac)
{
    auto al_it = m_als.find(al_mac);
    if (al_it == m_als.end()) {
        return;
    }

    auto &al = al_it->second;
    if (al.first_topology_query_deadline == time_point::max()) {
        return;
    }

    LOG(INFO) << "Topology response timed out for AL " << al_mac;

    // this is extremely unlikely, but whatever, lets retry
    if (al_mac == database.get_local_bridge_mac()) {
        m_als.erase(al_it);
        start_local_al_discovery();
        return;
    }

    // Consider this remote AL discovery failed: drop only sidecar state.
    // DB node is kept until orphan cleanup drops references to it.
    if (al.info_pending) {
        status_pending.count_down();
    }
    m_als.erase(al_it);
    cleanup_orphan_als();
}

void ieee1905_task::handle_higher_layer_timeout(const sMacAddr &al_mac)
{
    auto al_it = m_als.find(al_mac);
    if (al_it == m_als.end()) {
        return;
    }

    auto &al = al_it->second;
    if (al.first_higher_layer_query_deadline == time_point::max()) {
        return;
    }

    LOG(INFO) << "Higher layer response timed out for AL " << al_mac;
    al.first_higher_layer_query_deadline = time_point::max();
    al.higher_layer_response_pending.count_down();
}

/**
 * @brief Collect ALs reachable from the given root through outgoing IEEE1905 neighbor links.
 *
 * @param db_als      IEEE1905 AL database indexed by AL MAC.
 * @param root_al_mac Root AL from which reachability is evaluated.
 *
 * @return Set of AL MACs reachable from @p root_al_mac, excluding @p root_al_mac itself.
 */
static auto reachable_from(const db::ieee1905_network_db::ALMap &db_als,
                           const sMacAddr &root_al_mac)
{
    std::unordered_set<sMacAddr> reachable;
    std::vector<sMacAddr> pending;

    auto root_al_it = db_als.find(root_al_mac);
    if (root_al_it == db_als.end()) {
        return reachable;
    }

    pending.push_back(root_al_mac);

    while (!pending.empty()) {
        const auto al_mac = pending.back();
        pending.pop_back();

        auto al_it = db_als.find(al_mac);
        if (al_it == db_als.end()) {
            continue;
        }

        for (const auto &iface_entry : al_it->second.interfaces) {
            for (const auto &neighbor_entry : iface_entry.second.ieee1905_neighbors) {
                const auto &neighbor_al_mac = neighbor_entry.first;
                if (reachable.insert(neighbor_al_mac).second) {
                    pending.push_back(neighbor_al_mac);
                }
            }
        }
    }

    return reachable;
}

void ieee1905_task::cleanup_orphan_als()
{
    if (!database.ieee1905_network) {
        return;
    }

    auto &db_als             = database.ieee1905_network->al;
    const auto local_al_mac  = database.get_local_bridge_mac();
    const auto reachable_als = reachable_from(db_als, local_al_mac);

    for (auto it = db_als.begin(); it != db_als.end();) {
        if (it->first == local_al_mac || reachable_als.count(it->first) != 0) {
            ++it;
            continue;
        }

        LOG(DEBUG) << it->first << " is an orphan AL, removing";

        auto al_it = m_als.find(it->first);
        if (al_it != m_als.end()) {
            if (al_it->second.info_pending) {
                status_pending.count_down();
            }
            m_als.erase(al_it);
        }
        it = db_als.erase(it);
    }
}

void ieee1905_task::handle_event(int event_type, void *obj)
{
    if (event_type != IEEE1905_NETWORK_ENABLE_CHANGED) {
        return;
    }

    if (!obj) {
        LOG(ERROR) << "enable changed event without payload";
        return;
    }

    auto enabled = *static_cast<bool *>(obj);
    LOG(INFO) << IEEE1905_ROOT_DM << ".Network.Enable changed to " << enabled;
    set_ieee1905_network_enabled(enabled);
}

bool ieee1905_task::handle_topology_response(const sMacAddr &src_mac,
                                             ieee1905_1::CmduMessageRx &cmdu_rx)
{
    if (!database.ieee1905_network) {
        return true;
    }

    auto mid = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received TOPOLOGY_RESPONSE_MESSAGE from " << src_mac << ", mid=" << std::hex
               << mid;

    auto tlv_device_information = cmdu_rx.getClass<ieee1905_1::tlvDeviceInformation>();
    if (!tlv_device_information) {
        LOG(ERROR) << "ieee1905_1::tlvDeviceInformation not found";
        return false;
    }

    const auto &al_mac = tlv_device_information->mac();
    auto al_it         = m_als.find(al_mac);
    if (al_it == m_als.end()) {
        LOG(WARNING) << "Unexpected topology response from AL " << al_mac << ", ignoring";
        return false;
    }
    LOG(DEBUG) << "Topology response about AL " << al_mac;

    auto &al    = al_it->second;
    auto &db_al = database.ieee1905_network->al[al_mac];

    const bool first_local_response = (al_mac == database.get_local_bridge_mac()) && !db_al.dm_path;
    if (first_local_response) {
        if (!materialize_local_al()) {
            return false;
        }

        if (!query_sender->send_higher_layer_query(al_mac, cmdu_tx)) {
            LOG(ERROR) << "Failed to send higher layer query to local " << al_mac;
        }
    }

    using sInterface = db::ieee1905_network_db::sAL::sInterface;

    // snapshot and (re)build interfaces
    auto &ifs = db_al.interfaces;
    decltype(db_al.interfaces) prev_ifs;
    prev_ifs.swap(ifs);

    for (int i = 0; i < tlv_device_information->local_interface_list_length(); i++) {
        const auto iface_info = unwrap(tlv_device_information->local_interface_list(i));
        if (!iface_info) {
            LOG(ERROR) << "Failed to get " << i
                       << " element of local iface info on Device Information TLV";
            continue;
        }

        const auto &if_mac = iface_info->mac();
        auto prev_iface_it = prev_ifs.find(if_mac);
        if (prev_iface_it == prev_ifs.end()) {
            prev_iface_it = prev_ifs.emplace(if_mac, sInterface{}).first;
        }

        auto &iface   = ifs[if_mac];
        iface.dm_path = std::move(prev_iface_it->second.dm_path);
        iface.links   = std::move(prev_iface_it->second.links);
        iface.type    = iface_info->media_type();

        if (iface_info->media_info_length() == sizeof(iface.spec)) {
            std::memcpy(&iface.spec, iface_info->media_info(), sizeof(iface.spec));
        }
    }

    // (re)build non-1905 neighbors
    auto tlv_non_1905_neighbors = cmdu_rx.getClassList<ieee1905_1::tlvNon1905neighborDeviceList>();
    for (const auto &tlv_non_1905_neighbor : tlv_non_1905_neighbors) {
        if (!tlv_non_1905_neighbor) {
            LOG(ERROR) << "ieee1905_1::tlvNon1905neighborDeviceList has invalid pointer";
            continue;
        }

        const auto &if_mac = tlv_non_1905_neighbor->mac_local_iface();
        auto iface_it      = ifs.find(if_mac);
        if (iface_it == ifs.end()) {
            LOG(WARNING) << "Non-1905 neighbor on interface " << if_mac
                         << ", which is not in the list of interfaces, skipping";
            continue;
        }

        auto &neighbors      = iface_it->second.non_1905_neighbors;
        auto &prev_neighbors = prev_ifs[if_mac].non_1905_neighbors;

        const auto count = tlv_non_1905_neighbor->mac_non_1905_device_length() / sizeof(sMacAddr);
        for (size_t i = 0; i < count; ++i) {
            const auto neighbor = unwrap(tlv_non_1905_neighbor->mac_non_1905_device(i));
            if (!neighbor) {
                LOG(ERROR) << "Failed to read non-1905 neighbor TLV entry";
                continue;
            }

            const auto &neighbor_mac = *neighbor;
            neighbors[neighbor_mac]  = std::move(prev_neighbors[neighbor_mac]);
        }
    }

    using sNeighbor = db::ieee1905_network_db::sAL::sNeighbor;

    // (re)build ieee1905 neigbors, gather new ALs
    std::unordered_set<sMacAddr> seen_new_1905_neighbors;
    std::vector<sMacAddr> new_1905_neighbors;
    auto tlv_1905_neighbors = cmdu_rx.getClassList<ieee1905_1::tlv1905NeighborDevice>();
    for (const auto &tlv_1905_neighbor : tlv_1905_neighbors) {
        if (!tlv_1905_neighbor) {
            LOG(ERROR) << "ieee1905_1::tlv1905NeighborDevice has invalid pointer";
            continue;
        }

        const auto &if_mac = tlv_1905_neighbor->mac_local_iface();
        auto iface_it      = ifs.find(if_mac);
        if (iface_it == ifs.end()) {
            LOG(WARNING) << "IEEE1905 neighbor on interface " << if_mac
                         << ", which is not in the list of interfaces, skipping";
            continue;
        }

        auto &neighbors      = iface_it->second.ieee1905_neighbors;
        auto &prev_neighbors = prev_ifs[if_mac].ieee1905_neighbors;

        const auto count = tlv_1905_neighbor->mac_al_1905_device_length() /
                           sizeof(ieee1905_1::tlv1905NeighborDevice::sMacAl1905Device);
        for (size_t i = 0; i < count; ++i) {
            const auto neighbor = unwrap(tlv_1905_neighbor->mac_al_1905_device(i));
            if (!neighbor) {
                LOG(ERROR) << "Failed to read IEEE1905 neighbor TLV entry";
                continue;
            }

            const auto &neighbor_al_mac = neighbor->mac;
            if (m_als.find(neighbor_al_mac) == m_als.end() &&
                seen_new_1905_neighbors.insert(neighbor_al_mac).second) {
                new_1905_neighbors.push_back(neighbor_al_mac);
            }

            auto prev_neighbor_it = prev_neighbors.find(neighbor_al_mac);
            if (prev_neighbor_it != prev_neighbors.end()) {
                // move from prev to new
                auto &prev_neighbor = prev_neighbor_it->second;
                auto &db_neighbor =
                    neighbors.emplace(neighbor_al_mac, std::move(prev_neighbor)).first->second;
                db_neighbor.ieee802dot1_bridge =
                    neighbor->bridges_exist ==
                    ieee1905_1::tlv1905NeighborDevice::AT_LEAST_ONE_BRIDGES_EXIST;
                continue;
            }

            auto &als = database.ieee1905_network->al;
            auto ref  = sNeighbor::sRefHandle{als, neighbor_al_mac, {al_mac, if_mac}};
            const auto ieee802dot1_bridge =
                neighbor->bridges_exist ==
                ieee1905_1::tlv1905NeighborDevice::AT_LEAST_ONE_BRIDGES_EXIST;
            neighbors.emplace(neighbor_al_mac, sNeighbor{{}, ieee802dot1_bridge, std::move(ref)});
        }
    }

    prev_ifs.clear(); // dereferences lost/moved ALs
    cleanup_orphan_als();
    for (const auto &neighbor_al_mac : new_1905_neighbors) {
        LOG(DEBUG) << "Starting " << neighbor_al_mac << " AL discovery";
        if (!start_remote_al_discovery(neighbor_al_mac)) {
            LOG(ERROR) << "Failed to start discovery for neighbor AL " << neighbor_al_mac;
        }
    }

    update_al_in_dm(al_mac);

    al.first_topology_query_deadline = time_point::max();
    al.topology_response_pending.count_down();
    al.next_periodic_topology_query_deadline = now() + periodic_topology_requery_interval;

    return true;
}

bool ieee1905_task::handle_higher_layer_response(const sMacAddr &src_mac,
                                                 ieee1905_1::CmduMessageRx &cmdu_rx)
{
    if (!database.ieee1905_network) {
        return false;
    }

    auto mid = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received HIGHER_LAYER_RESPONSE_MESSAGE from " << src_mac << ", mid=" << std::hex
               << mid;

    auto tlv_al_mac = cmdu_rx.getClass<ieee1905_1::tlvAlMacAddress>();
    if (!tlv_al_mac) {
        LOG(ERROR) << "ieee1905_1::tlvAlMacAddress not found";
        return false;
    }

    auto al_mac = tlv_al_mac->mac();
    auto al_it  = m_als.find(al_mac);
    if (al_it == m_als.end()) {
        LOG(WARNING) << "Higher layer response from unknown AL " << al_mac << ", ignoring";
        return false;
    }
    LOG(DEBUG) << "Higher layer response about AL " << al_mac;

    using sAL      = db::ieee1905_network_db::sAL;
    using sIPv4Key = sAL::sIPv4Address::sKey;
    using sIPv6Key = sAL::sIPv6Address::sKey;

    constexpr size_t kDeviceIdStringSize = 64;

    auto &db_al = database.ieee1905_network->al[al_mac];

    if (auto tlv_profile_version = cmdu_rx.getClass<ieee1905_1::tlv1905ProfileVersion>()) {
        db_al.version_is_1905a =
            tlv_profile_version->version() == ieee1905_1::e1905ProfileVersion::IEEE_1905_1_A;
    }

    if (auto tlv_device_identification = cmdu_rx.getClass<ieee1905_1::tlvDeviceIdentification>()) {
        db_al.friendly_name =
            fixed_utf8_string(tlv_device_identification->friendly_name(), kDeviceIdStringSize);
        db_al.manufacturer_name =
            fixed_utf8_string(tlv_device_identification->manufacturer_name(), kDeviceIdStringSize);
        db_al.manufacturer_model =
            fixed_utf8_string(tlv_device_identification->manufacturer_model(), kDeviceIdStringSize);
    }

    if (auto tlv_control_url = cmdu_rx.getClass<ieee1905_1::tlvControlUrl>()) {
        db_al.control_url =
            fixed_utf8_string(tlv_control_url->control_url(), tlv_control_url->length());
    }

    // snapshot and (re)build
    decltype(db_al.ipv4_addresses) prev_ipv4_addresses;
    prev_ipv4_addresses.swap(db_al.ipv4_addresses);

    auto tlv_ipv4_list = cmdu_rx.getClassList<ieee1905_1::tlvIpv4>();
    for (const auto &tlv_ipv4 : tlv_ipv4_list) {
        if (!tlv_ipv4) {
            LOG(ERROR) << "ieee1905_1::tlvIpv4 has invalid pointer";
            continue;
        }

        for (int i = 0; i < tlv_ipv4->number_of_entries(); ++i) {
            auto iface_block = unwrap(tlv_ipv4->ipv4_interfaces_list(i));
            if (!iface_block) {
                LOG(ERROR) << "Failed to read IPv4 interface block #" << i;
                continue;
            }

            const auto &mac = iface_block->mac_address();
            for (int j = 0; j < iface_block->number_of_ipv4_addresses(); ++j) {
                auto ipv4_entry = unwrap(iface_block->ipv4_address_entries(j));
                if (!ipv4_entry) {
                    LOG(ERROR) << "Failed to read IPv4 address entry #" << j;
                    continue;
                }

                sIPv4Key key{mac, ipv4_from_u32(ipv4_entry->ipv4_address)};
                auto prev_ipv4_it = prev_ipv4_addresses.find(key);
                auto &db_ipv4     = db_al.ipv4_addresses[key];
                if (prev_ipv4_it != prev_ipv4_addresses.end()) {
                    db_ipv4.dm_path = std::move(prev_ipv4_it->second.dm_path);
                }
                db_ipv4.type        = ipv4_entry->ipv4_address_type;
                db_ipv4.dhcp_server = ipv4_from_u32(ipv4_entry->ipv4_dhcp_server);
            }
        }
    }
    prev_ipv4_addresses.clear();

    decltype(db_al.ipv6_addresses) prev_ipv6_addresses;
    prev_ipv6_addresses.swap(db_al.ipv6_addresses);

    auto tlv_ipv6_list = cmdu_rx.getClassList<ieee1905_1::tlvIpv6>();
    for (const auto &tlv_ipv6 : tlv_ipv6_list) {
        if (!tlv_ipv6) {
            LOG(ERROR) << "ieee1905_1::tlvIpv6 has invalid pointer";
            continue;
        }

        for (int i = 0; i < tlv_ipv6->number_of_entries(); ++i) {
            auto iface_block = unwrap(tlv_ipv6->ipv6_interfaces_list(i));
            if (!iface_block) {
                LOG(ERROR) << "Failed to read IPv6 interface block #" << i;
                continue;
            }

            const auto &mac = iface_block->mac_address();
            for (int j = 0; j < iface_block->number_of_ipv6_addresses(); ++j) {
                auto ipv6_entry = unwrap(iface_block->ipv6_address_entries(j));
                if (!ipv6_entry) {
                    LOG(ERROR) << "Failed to read IPv6 address entry #" << j;
                    continue;
                }

                const auto ipv6_address_string = ipv6_to_string(ipv6_entry->ipv6_address);
                sIPv6Key key{mac, ipv6_address_string};
                auto prev_ipv6_it = prev_ipv6_addresses.find(key);
                auto &db_ipv6     = db_al.ipv6_addresses[key];
                if (prev_ipv6_it != prev_ipv6_addresses.end()) {
                    db_ipv6.dm_path = std::move(prev_ipv6_it->second.dm_path);
                }
                db_ipv6.type   = ipv6_entry->ipv6_address_type;
                db_ipv6.origin = ipv6_to_string(ipv6_entry->ipv6_address_origin);
            }
        }
    }
    prev_ipv6_addresses.clear();

    update_al_in_dm(al_mac);

    auto &al = al_it->second;

    al.first_higher_layer_query_deadline = time_point::max();
    al.higher_layer_response_pending.count_down();

    return true;
}

bool ieee1905_task::handle_link_metric_response(const sMacAddr &src_mac,
                                                ieee1905_1::CmduMessageRx &cmdu_rx)
{
    if (!database.ieee1905_network) {
        return false;
    }

    auto mid = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received LINK_METRIC_RESPONSE_MESSAGE from " << src_mac << ", mid=" << std::hex
               << mid;

    auto tx_link_metrics = cmdu_rx.getClassList<ieee1905_1::tlvTransmitterLinkMetric>();
    auto rx_link_metrics = cmdu_rx.getClassList<ieee1905_1::tlvReceiverLinkMetric>();

    sMacAddr reporting_agent_al_mac    = beerocks::net::network_utils::ZERO_MAC;
    auto update_reporting_agent_al_mac = [&](const sMacAddr &reporter_al_mac,
                                             const char *metric_type) {
        if (reporter_al_mac == beerocks::net::network_utils::ZERO_MAC) {
            LOG(ERROR) << metric_type << " link metric reporter AL is zero";
            return false;
        }

        if (reporting_agent_al_mac != beerocks::net::network_utils::ZERO_MAC &&
            reporting_agent_al_mac != reporter_al_mac) {
            LOG(ERROR) << metric_type << " link metric reporter AL mismatch: expected "
                       << reporting_agent_al_mac << " got " << reporter_al_mac;
            return false;
        }

        reporting_agent_al_mac = reporter_al_mac;
        return true;
    };

    for (const auto &tx_link_metric : tx_link_metrics) {
        if (!tx_link_metric) {
            LOG(ERROR) << "ieee1905_1::tlvTransmitterLinkMetric has invalid pointer";
            continue;
        }

        if (!update_reporting_agent_al_mac(tx_link_metric->reporter_al_mac(), "TX")) {
            return false;
        }
    }

    for (const auto &rx_link_metric : rx_link_metrics) {
        if (!rx_link_metric) {
            LOG(ERROR) << "ieee1905_1::tlvReceiverLinkMetric has invalid pointer";
            continue;
        }

        if (!update_reporting_agent_al_mac(rx_link_metric->reporter_al_mac(), "RX")) {
            return false;
        }
    }

    if (reporting_agent_al_mac == beerocks::net::network_utils::ZERO_MAC) {
        LOG(WARNING) << "Link metric response without valid reporter AL, ignoring";
        return false;
    }

    auto al_it = m_als.find(reporting_agent_al_mac);
    if (al_it == m_als.end()) {
        LOG(WARNING) << "Link metric response from unexpected AL " << reporting_agent_al_mac
                     << ", ignoring";
        return false;
    }

    using sAL      = db::ieee1905_network_db::sAL;
    using sRef     = sAL::sRef;
    using sLink    = sAL::sInterface::sLink;
    using LinksMap = decltype(sAL::sInterface::links);

    auto &db_al = database.ieee1905_network->al[reporting_agent_al_mac];
    std::unordered_map<sMacAddr, LinksMap> new_links;

    auto get_or_create_link = [&](const sMacAddr &rc_if_mac, const sRef &link_ref) -> sLink * {
        auto iface_it = db_al.interfaces.find(rc_if_mac);
        if (iface_it == db_al.interfaces.end()) {
            LOG(WARNING) << "Link metric on unknown interface " << rc_if_mac << ", skipping";
            return nullptr;
        }

        auto &links  = new_links[rc_if_mac];
        auto link_it = links.find(link_ref);
        if (link_it != links.end()) {
            return &link_it->second;
        }

        auto inserted = links.emplace(link_ref, sLink{});
        auto &link    = inserted.first->second;

        auto prev_link_it = iface_it->second.links.find(link_ref);
        if (prev_link_it != iface_it->second.links.end()) {
            link.dm_path = std::move(prev_link_it->second.dm_path);
        }

        return &link;
    };

    bool has_link_metric = false;

    for (const auto &tx_link_metric : tx_link_metrics) {
        if (!tx_link_metric) {
            LOG(ERROR) << "ieee1905_1::tlvTransmitterLinkMetric has invalid pointer";
            continue;
        }

        const auto count = tx_link_metric->interface_pair_info_length() /
                           sizeof(ieee1905_1::tlvTransmitterLinkMetric::sInterfacePairInfo);
        for (size_t i = 0; i < count; ++i) {
            auto pair_info = unwrap(tx_link_metric->interface_pair_info(i));
            if (!pair_info) {
                LOG(ERROR) << "Failed to read TX link metric pair #" << i;
                continue;
            }

            sRef link_ref{tx_link_metric->neighbor_al_mac(), pair_info->neighbor_interface_mac};
            auto link = get_or_create_link(pair_info->rc_interface_mac, link_ref);
            if (!link) {
                continue;
            }

            link->tx_link_metric = pair_info->link_metric_info;
            has_link_metric      = true;
        }
    }

    for (const auto &rx_link_metric : rx_link_metrics) {
        if (!rx_link_metric) {
            LOG(ERROR) << "ieee1905_1::tlvReceiverLinkMetric has invalid pointer";
            continue;
        }

        const auto count = rx_link_metric->interface_pair_info_length() /
                           sizeof(ieee1905_1::tlvReceiverLinkMetric::sInterfacePairInfo);
        for (size_t i = 0; i < count; ++i) {
            auto pair_info = unwrap(rx_link_metric->interface_pair_info(i));
            if (!pair_info) {
                LOG(ERROR) << "Failed to read RX link metric pair #" << i;
                continue;
            }

            sRef link_ref{rx_link_metric->neighbor_al_mac(), pair_info->neighbor_interface_mac};
            auto *link = get_or_create_link(pair_info->rc_interface_mac, link_ref);
            if (!link) {
                continue;
            }

            link->rx_link_metric = pair_info->link_metric_info;
            has_link_metric      = true;
        }
    }

    if (!has_link_metric) {
        return true;
    }

    for (auto &iface_entry : db_al.interfaces) {
        auto new_iface_links_it = new_links.find(iface_entry.first);
        if (new_iface_links_it == new_links.end()) {
            iface_entry.second.links.clear();
            continue;
        }

        iface_entry.second.links.swap(new_iface_links_it->second);
    }

    update_al_in_dm(reporting_agent_al_mac);

    return true;
}

bool ieee1905_task::handle_topology_notification(const sMacAddr &src_mac,
                                                 ieee1905_1::CmduMessageRx &cmdu_rx)
{
    if (!database.ieee1905_network) {
        return false;
    }

    auto mid = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received TOPOLOGY_NOTIFICATION_MESSAGE from " << src_mac << ", mid=" << std::hex
               << mid;

    auto tlv_al_mac = cmdu_rx.getClass<ieee1905_1::tlvAlMacAddress>();
    if (!tlv_al_mac) {
        LOG(ERROR) << "ieee1905_1::tlvAlMacAddress not found";
        return false;
    }

    const auto &al_mac = tlv_al_mac->mac();

    if (m_als.find(al_mac) == m_als.end()) {
        LOG(WARNING) << "Topology notification from unknown AL " << al_mac << ", ignoring";
        return false;
    }

    // this is a microoptimization to not to duplicate topology queries when we're sure we would
    auto client_association_event_tlv = cmdu_rx.getClass<wfa_map::tlvClientAssociationEvent>();
    if (!client_association_event_tlv && database.get_agent(al_mac)) {
        LOG(DEBUG) << "Skipping topology query for known agent " << al_mac
                   << " on topology notification without Client Association Event TLV";
        return true;
    }

    if (!query_sender->send_topology_query(al_mac, cmdu_tx)) {
        LOG(ERROR) << "Failed to send topology query to " << al_mac;
        return false;
    }

    return true;
}
