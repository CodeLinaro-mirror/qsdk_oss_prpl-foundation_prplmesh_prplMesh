/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "higher_layer_collection_task.h"

#include "../agent_db.h"

#include <bcl/network/network_utils.h>
#include <tlvf/ieee_1905_1/e1905ProfileVersion.h>
#include <tlvf/ieee_1905_1/tlv1905ProfileVersion.h>
#include <tlvf/ieee_1905_1/tlvAlMacAddress.h>
#include <tlvf/ieee_1905_1/tlvDeviceIdentification.h>
#include <tlvf/ieee_1905_1/tlvIpv4.h>
#include <tlvf/ieee_1905_1/tlvIpv6.h>
#include <tlvf/wfa_map/tlvHigherLayerData.h>

#include <algorithm>
#include <unordered_set>

#include <easylogging++.h>

using namespace beerocks;

namespace {

bool is_zero_ipv6(const uint8_t *address)
{
    return std::all_of(address, address + 16, [](uint8_t octet) { return octet == 0; });
}

bool has_ipv6_address(const HigherLayerCollectionTask::sInterfaceNetworkStatus &status)
{
    return !is_zero_ipv6(status.ipv6_link_local) || !status.ipv6_list.empty();
}

std::unordered_set<std::string> collect_reportable_interface_names()
{
    std::unordered_set<std::string> names;
    std::string bridge_iface;
    std::string selected_backhaul_iface;
    bool local_gw = false;

    {
        auto db                 = AgentDB::get();
        bridge_iface            = db->bridge.iface_name;
        selected_backhaul_iface = db->backhaul.selected_iface_name;
        local_gw                = db->device_conf.local_gw;

        for (const auto &lan_iface : db->ethernet.lan) {
            if (!lan_iface.iface_name.empty()) {
                names.insert(lan_iface.iface_name);
            }
        }
    }

    auto add_bridge = [&](const std::string &bridge) {
        if (bridge.empty()) {
            return;
        }

        names.insert(bridge);

        const auto bridge_ports =
            beerocks::net::network_utils::linux_get_iface_list_from_bridge(bridge);
        names.insert(bridge_ports.begin(), bridge_ports.end());
    };

    add_bridge(bridge_iface);
    for (const auto &bridge : beerocks::net::network_utils::linux_get_bridges()) {
        add_bridge(bridge);
    }

    if (!local_gw && !selected_backhaul_iface.empty()) {
        names.insert(std::move(selected_backhaul_iface));
    }

    return names;
}

void filter_non_reportable_interfaces(
    std::unordered_map<std::string, HigherLayerCollectionTask::sInterfaceNetworkStatus>
        &interfaces_network_status)
{
    const auto reportable_interfaces = collect_reportable_interface_names();
    if (reportable_interfaces.empty()) {
        return;
    }

    for (auto it = interfaces_network_status.begin(); it != interfaces_network_status.end();) {
        if (reportable_interfaces.count(it->first) == 0) {
            LOG(DEBUG) << "Skipping IP address report for non-1905 interface " << it->first;
            it = interfaces_network_status.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

/**
 * @brief Constructor for HigherLayerCollectionTask.
 * Initializes the task with necessary callbacks for communication and data retrieval.
 * Performs fatal checks to ensure all providers and callbacks are valid.
 * @param send_cmdu_to_mac Callback function to send the generated CMDU.
 * @param get_bridge_mac Callback to retrieve the bridge (AL) MAC address.
 * @param interface_status_provider Implementation of the network data provider (e.g., getifaddrs).
 * @param cmdu_tx Reference to the message transmitter buffer.
 */
HigherLayerCollectionTask::HigherLayerCollectionTask(
    send_cmdu_to_mac_f send_cmdu_to_mac, get_bridge_mac_f get_bridge_mac,
    std::unique_ptr<InterfaceStatusProvider> interface_status_provider,
    ieee1905_1::CmduMessageTx &cmdu_tx, get_friendly_name_f get_friendly_name)
    : Task(eTaskType::HIGHER_LAYER_COLLECTION_TASK), m_cmdu_tx(cmdu_tx),
      m_send_cmdu_to_mac(std::move(send_cmdu_to_mac)), m_get_bridge_mac(std::move(get_bridge_mac)),
      m_get_friendly_name(std::move(get_friendly_name)),
      m_interface_status_provider(std::move(interface_status_provider))
{
    LOG_IF(!m_send_cmdu_to_mac, FATAL) << "HigherLayerCollectionTask send callback is empty";
    LOG_IF(!m_get_bridge_mac, FATAL) << "HigherLayerCollectionTask bridge mac callback is empty";
    LOG_IF(!m_interface_status_provider, FATAL)
        << "HigherLayerCollectionTask interface status provider is empty";

    LOG(DEBUG) << "HigherLayerCollectionTask created";
}

/**
 * @brief Main entry point for handling incoming CMDU messages.
 * @param cmdu_rx The received CMDU message.
 * @param iface_index Index of the interface the message was received on.
 * @param dst_mac Destination MAC address of the frame.
 * @param src_mac Source MAC address of the frame.
 * @param fd File descriptor associated with the connection.
 * @param beerocks_header Shared pointer to the beerocks-specific header.
 * @return true if the message was handled, false otherwise.
 */
bool HigherLayerCollectionTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx,
                                            uint32_t iface_index, const sMacAddr &dst_mac,
                                            const sMacAddr &src_mac, int fd,
                                            std::shared_ptr<beerocks_header> beerocks_header)
{
    if (cmdu_rx.getMessageType() == ieee1905_1::eMessageType::HIGHER_LAYER_QUERY_MESSAGE) {
        return handle_higher_layer_query(src_mac, cmdu_rx);
    }

    return false;
}

/**
 * @brief Processes a Higher Layer Query and sends a Higher Layer Response.
 * This function:
 * 1. Refreshes interface data using the provider.
 * 2. Adds mandatory 1905.1 TLVs (AL MAC, Profile Version, Device ID).
 * 3. Iterates through interfaces and adds IPv4/IPv6 TLVs if addresses are present.
 * 4. Sends the finalized CMDU back to the controller.
 * @param src_mac MAC address of the query sender.
 * @param cmdu_rx Incoming query CMDU.
 * @return true on successful response generation and sending, false on failure.
 */
bool HigherLayerCollectionTask::handle_higher_layer_query(const sMacAddr &src_mac,
                                                          ieee1905_1::CmduMessageRx &cmdu_rx)
{
    LOG(DEBUG) << "Received HIGHER_LAYER_QUERY from " << src_mac;

    auto cmdu_header = m_cmdu_tx.create(cmdu_rx.getMessageId(),
                                        ieee1905_1::eMessageType::HIGHER_LAYER_RESPONSE_MESSAGE);
    if (!cmdu_header) {
        LOG(ERROR) << "Failed to create CMDU Higher Layer Response";
        return false;
    }

    std::unordered_map<std::string, sInterfaceNetworkStatus> interfaces_network_status;
    if (!m_interface_status_provider->refresh(interfaces_network_status)) {
        LOG(WARNING) << "Failed to refresh interface status";
    }
    for (auto it = interfaces_network_status.begin(); it != interfaces_network_status.end();) {
        if (it->second.mac_address == beerocks::net::network_utils::ZERO_MAC) {
            it = interfaces_network_status.erase(it);
        } else {
            ++it;
        }
    }
    filter_non_reportable_interfaces(interfaces_network_status);

    // --- AL MAC Address TLV ---
    auto tlv_al_mac = m_cmdu_tx.addClass<ieee1905_1::tlvAlMacAddress>();
    if (!tlv_al_mac) {
        LOG(ERROR) << "Failed to create AL MAC TLV";
        return false;
    }
    tlv_al_mac->mac() = m_get_bridge_mac();

    // --- 1905 Profile Version TLV ---
    auto tlv_profile = m_cmdu_tx.addClass<ieee1905_1::tlv1905ProfileVersion>();
    if (!tlv_profile) {
        LOG(ERROR) << "Failed to create 1905 Profile Version TLV";
        return false;
    }
    tlv_profile->version() = ieee1905_1::e1905ProfileVersion::IEEE_1905_1_A;

    // --- Device Identification TLV ---
    auto tlv_dev_id = m_cmdu_tx.addClass<ieee1905_1::tlvDeviceIdentification>();
    if (!tlv_dev_id) {
        LOG(ERROR) << "Failed to create Device Identification TLV";
        return false;
    }

    std::string friendly_name;
    std::string manufacturer;
    std::string model_name;

    if (m_get_friendly_name) {
        friendly_name = m_get_friendly_name();
    }

    {
        auto db = AgentDB::get();
        if (!m_get_friendly_name) {
            friendly_name = db->device_conf.device_friendly_name;
        }
        manufacturer = db->device_conf.device_manufacturer;
        model_name   = db->device_conf.device_model_name;
    }

    if (!tlv_dev_id->set_friendly_name(friendly_name.c_str(), friendly_name.length())) {
        LOG(ERROR) << "Failed to set Device Identification FriendlyName";
        return false;
    }

    if (!tlv_dev_id->set_manufacturer_name(manufacturer.c_str(), manufacturer.length())) {
        LOG(ERROR) << "Failed to set Device Identification ManufacturerName";
        return false;
    }

    if (!tlv_dev_id->set_manufacturer_model(model_name.c_str(), model_name.length())) {
        LOG(ERROR) << "Failed to set Device Identification ManufacturerModel";
        return false;
    }

    // --- IPv4 Interfaces and Addresses ---
    bool has_ipv4 = std::any_of(interfaces_network_status.begin(), interfaces_network_status.end(),
                                [](const auto &p) { return !p.second.ipv4_list.empty(); });

    if (has_ipv4) {
        auto tlv_ipv4 = m_cmdu_tx.addClass<ieee1905_1::tlvIpv4>();
        if (!tlv_ipv4) {
            LOG(ERROR) << "Failed to create IPv4 TLV";
            return false;
        }
        for (const auto &it : interfaces_network_status) {
            const auto &status = it.second;
            if (status.ipv4_list.empty()) {
                continue;
            }

            auto block = tlv_ipv4->create_ipv4_interfaces_list();
            if (!block) {
                LOG(ERROR) << "Failed to create IPv4 interface block for interface " << it.first;
                return false;
            }

            block->mac_address() = status.mac_address;
            if (!block->alloc_ipv4_address_entries(status.ipv4_list.size())) {
                LOG(ERROR) << "Failed to allocate IPv4 address entries for interface " << it.first;
                return false;
            }

            for (size_t i = 0; i < status.ipv4_list.size(); ++i) {
                auto &entry             = std::get<1>(block->ipv4_address_entries(i));
                entry.ipv4_address_type = status.ipv4_list[i].ipv4_address_type;
                entry.ipv4_address      = status.ipv4_list[i].ipv4_address;
                entry.ipv4_dhcp_server  = status.ipv4_list[i].ipv4_dhcp_server;
            }

            if (!tlv_ipv4->add_ipv4_interfaces_list(std::move(block))) {
                LOG(ERROR) << "Failed to add IPv4 interface block for interface " << it.first;
                return false;
            }
        }
    }

    // --- IPv6 Interfaces and Addresses ---
    bool has_ipv6 = std::any_of(interfaces_network_status.begin(), interfaces_network_status.end(),
                                [](const auto &p) { return has_ipv6_address(p.second); });

    if (has_ipv6) {
        auto tlv_ipv6 = m_cmdu_tx.addClass<ieee1905_1::tlvIpv6>();
        if (!tlv_ipv6) {
            LOG(ERROR) << "Failed to create IPv6 TLV";
            return false;
        }

        for (const auto &it : interfaces_network_status) {
            const auto &status = it.second;
            if (!has_ipv6_address(status)) {
                continue;
            }

            auto block = tlv_ipv6->create_ipv6_interfaces_list();
            if (!block) {
                LOG(ERROR) << "Failed to create IPv6 interface block for interface " << it.first;
                return false;
            }

            block->mac_address() = status.mac_address;
            if (!block->set_ipv6_link_local_address(status.ipv6_link_local, 16)) {
                LOG(ERROR) << "Failed to set IPv6 link-local address for interface " << it.first;
                return false;
            }

            if (!block->alloc_ipv6_address_entries(status.ipv6_list.size())) {
                LOG(ERROR) << "Failed to allocate IPv6 address entries for interface " << it.first;
                return false;
            }

            for (size_t i = 0; i < status.ipv6_list.size(); ++i) {
                auto &entry             = std::get<1>(block->ipv6_address_entries(i));
                entry.ipv6_address_type = status.ipv6_list[i].ipv6_address_type;

                std::copy(std::begin(status.ipv6_list[i].ipv6_address),
                          std::end(status.ipv6_list[i].ipv6_address), entry.ipv6_address);

                std::copy(std::begin(status.ipv6_list[i].ipv6_origin),
                          std::end(status.ipv6_list[i].ipv6_origin), entry.ipv6_address_origin);
            }

            if (!tlv_ipv6->add_ipv6_interfaces_list(std::move(block))) {
                LOG(ERROR) << "Failed to add IPv6 interface block for interface " << it.first;
                return false;
            }
        }
    }

    return m_send_cmdu_to_mac(src_mac, m_cmdu_tx);
}
