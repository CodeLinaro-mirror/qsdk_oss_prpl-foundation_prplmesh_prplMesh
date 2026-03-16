/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "ieee1905_task.h"

#include <easylogging++.h>

using namespace son;

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
    // Skeleton: no periodic flow yet.
}

bool ieee1905_task::handle_ieee1905_1_msg(const sMacAddr &, ieee1905_1::CmduMessageRx &)
{
    // Skeleton: message handling is added in follow-up commits.
    return false;
}

void ieee1905_task::set_ieee1905_network_enabled(bool enabled)
{
    if (!enabled) {
        database.ieee1905_network.reset();
        return;
    }

    if (!database.ieee1905_network) {
        database.ieee1905_network = std::make_unique<db::ieee1905_network_db>();
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
    if (!query_sender->send_topology_query(database.get_local_bridge_mac(), cmdu_tx)) {
        LOG(ERROR) << "Failed to send topology query to local bridge "
                   << database.get_local_bridge_mac();
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
