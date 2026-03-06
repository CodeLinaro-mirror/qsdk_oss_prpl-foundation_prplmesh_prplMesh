/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "higher_layer_collection_task.h"

#include <bpl/bpl_cfg.h>
#include <tlvf/ieee_1905_1/e1905ProfileVersion.h>
#include <tlvf/ieee_1905_1/tlv1905ProfileVersion.h>
#include <tlvf/ieee_1905_1/tlvAlMacAddress.h>
#include <tlvf/ieee_1905_1/tlvDeviceIdentification.h>
#include <tlvf/ieee_1905_1/tlvIpv4.h>
#include <tlvf/ieee_1905_1/tlvIpv6.h>
#include <tlvf/wfa_map/tlvHigherLayerData.h>

#include <algorithm>
#include <easylogging++.h>

using namespace beerocks;

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
    ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::HIGHER_LAYER_COLLECTION_TASK), m_cmdu_tx(cmdu_tx),
      m_send_cmdu_to_mac(std::move(send_cmdu_to_mac)), m_get_bridge_mac(std::move(get_bridge_mac)),
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
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::HIGHER_LAYER_QUERY_MESSAGE: {
        handle_higher_layer_query(src_mac, cmdu_rx);
        break;
    }
    default: {
        // Message was not handled, therefore return false.
        return false;
    }
    }
    return true;
}

/**
 * @brief Processes a Higher Layer Query and sends a Higher Layer Response.
 * This function:
 * 1. Refreshes interface data using the provider.
 * 2. Adds mandatory 1905.1 TLVs (AL MAC, Profile Version, Device ID).
 * 3. Iterates through interfaces and adds IPv4/IPv6 TLVs if addresses are present.
 * 4. Sends the finalized CMDU back to the controller.
 * @param src_mac MAC address of the query sender.
 * @param cmdu_rx Incoming query CMDU (unused in logic but required for API).
 * @return true on successful response generation and sending, false on failure.
 */
bool HigherLayerCollectionTask::handle_higher_layer_query(const sMacAddr &src_mac,
                                                          ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto cmdu_header = m_cmdu_tx.create(0, ieee1905_1::eMessageType::HIGHER_LAYER_RESPONSE_MESSAGE);
    if (!cmdu_header) {
        LOG(ERROR) << "Failed to create CMDU Higher Layer Response";
        return false;
    }

    if (!m_interface_status_provider->refresh(m_interfaces_network_status)) {
        LOG(WARNING) << "Failed to refresh interface status";
    }

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

    if (!beerocks::bpl::get_string_value_dm_di("FriendlyName", friendly_name)) {
        friendly_name = "";
    }
    if (!beerocks::bpl::get_string_value_dm_di("Manufacturer", manufacturer)) {
        manufacturer = "";
    }
    if (!beerocks::bpl::get_string_value_dm_di("ModelName", model_name)) {
        model_name = "";
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
    bool has_ipv4 =
        std::any_of(m_interfaces_network_status.begin(), m_interfaces_network_status.end(),
                    [](const auto &p) { return !p.second.ipv4_list.empty(); });

    if (has_ipv4) {
        auto tlv_ipv4 = m_cmdu_tx.addClass<ieee1905_1::tlvIpv4>();
        if (!tlv_ipv4) {
            LOG(ERROR) << "Failed to create IPv4 TLV";
            return false;
        }
        for (const auto &it : m_interfaces_network_status) {
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

            if (!tlv_ipv4->add_ipv4_interfaces_list(block)) {
                LOG(ERROR) << "Failed to add IPv4 interface block for interface " << it.first;
                return false;
            }
        }
    }

    // --- IPv6 Interfaces and Addresses ---
    bool has_ipv6 =
        std::any_of(m_interfaces_network_status.begin(), m_interfaces_network_status.end(),
                    [](const auto &p) { return !p.second.ipv6_list.empty(); });

    if (has_ipv6) {
        auto tlv_ipv6 = m_cmdu_tx.addClass<ieee1905_1::tlvIpv6>();
        if (!tlv_ipv6) {
            LOG(ERROR) << "Failed to create IPv6 TLV";
            return false;
        }

        for (const auto &it : m_interfaces_network_status) {
            const auto &status = it.second;
            if (status.ipv6_list.empty()) {
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

            if (!tlv_ipv6->add_ipv6_interfaces_list(block)) {
                LOG(ERROR) << "Failed to add IPv6 interface block for interface " << it.first;
                return false;
            }
        }
    }
    return m_send_cmdu_to_mac(src_mac, m_cmdu_tx);
}
