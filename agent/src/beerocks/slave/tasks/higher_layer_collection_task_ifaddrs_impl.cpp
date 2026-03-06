/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "higher_layer_collection_task_ifaddrs_impl.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <bcl/network/network_utils.h>
#include <tlvf/tlvftypes.h>

#include <algorithm>
#include <cstring>

#include <easylogging++.h>
#include <tlvf/swap.h>

namespace {

using sStatus = beerocks::HigherLayerCollectionTask::sInterfaceNetworkStatus;

/**
 * @brief Helper to find or create a status entry for a specific interface.
 * @param[in,out] interfaces_network_status Map of existing statuses.
 * @param interface_name Name of the interface to search for.
 * @return Pointer to the existing or newly created sStatus object.
 */
sStatus *get_or_create_status(std::unordered_map<std::string, sStatus> &interfaces_network_status,
                              const std::string &interface_name)
{
    auto it = interfaces_network_status.find(interface_name);
    if (it == interfaces_network_status.end()) {
        sStatus new_status{};

        std::string mac_str;
        if (beerocks::net::network_utils::linux_iface_get_mac(interface_name, mac_str)) {
            new_status.mac_address = tlvf::mac_from_string(mac_str);
        }

        it = interfaces_network_status.emplace(interface_name, std::move(new_status)).first;
    }

    return &it->second;
}

/**
 * @brief Parses an interface address entry and extracts IP address information.
 * This function handles both AF_INET and AF_INET6 families.
 * @param ifa The interface address entry to parse.
 * @param[out] interfaces_network_status Map where extracted data is stored.
 */
void parse_and_store_address(const struct ifaddrs *ifa,
                             std::unordered_map<std::string, sStatus> &interfaces_network_status)
{
    if (!ifa || !ifa->ifa_addr) {
        return;
    }

    const std::string interface_name(ifa->ifa_name);
    auto *status = get_or_create_status(interfaces_network_status, interface_name);

    if (ifa->ifa_addr->sa_family == AF_INET) {
        auto *addr = reinterpret_cast<const struct sockaddr_in *>(ifa->ifa_addr);
        sStatus::sIpv4Entry ipv4_entry{};
        std::memcpy(&ipv4_entry.ipv4_address, &addr->sin_addr.s_addr, sizeof(uint32_t));
        // Convert from Network Byte Order to Host Byte Order.
        swap_32(ipv4_entry.ipv4_address);
        ipv4_entry.ipv4_dhcp_server  = 0;
        ipv4_entry.ipv4_address_type = ieee1905_1::eIpv4AddressType::UNKNOWN;

        auto it = std::find_if(status->ipv4_list.begin(), status->ipv4_list.end(),
                               [&](const sStatus::sIpv4Entry &e) {
                                   return e.ipv4_address == ipv4_entry.ipv4_address;
                               });
        if (it == status->ipv4_list.end()) {
            status->ipv4_list.push_back(ipv4_entry);
        }
    } else if (ifa->ifa_addr->sa_family == AF_INET6) {
        auto *addr = reinterpret_cast<const struct sockaddr_in6 *>(ifa->ifa_addr);

        if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) {
            std::memcpy(status->ipv6_link_local, &addr->sin6_addr, 16);
            return;
        }

        sStatus::sIpv6Entry ipv6_entry{};
        std::memcpy(ipv6_entry.ipv6_address, &addr->sin6_addr, 16);
        std::memset(ipv6_entry.ipv6_origin, 0, 16);
        ipv6_entry.ipv6_address_type = ieee1905_1::eIpv6AddressType::UNKNOWN;

        auto it = std::find_if(
            status->ipv6_list.begin(), status->ipv6_list.end(), [&](const sStatus::sIpv6Entry &e) {
                return std::memcmp(e.ipv6_address, ipv6_entry.ipv6_address, 16) == 0;
            });
        if (it == status->ipv6_list.end()) {
            status->ipv6_list.push_back(ipv6_entry);
        }
    }
}

} // namespace

namespace beerocks {

/**
 * @brief Refreshes the network status by reading current interface addresses.
 * Uses getifaddrs() to iterate over the current interface address snapshot.
 * @param[out] interfaces_network_status Map to store the collected interface data.
 * @return true if the interface list was successfully read, false on error.
 */
bool HigherLayerCollectionTaskIfAddrsImpl::refresh(
    std::unordered_map<std::string, HigherLayerCollectionTask::sInterfaceNetworkStatus>
        &interfaces_network_status)
{
    interfaces_network_status.clear();

    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        LOG(ERROR) << "Failed to get interface addresses";
        return false;
    }

    for (auto *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        parse_and_store_address(ifa, interfaces_network_status);
    }

    freeifaddrs(ifaddr);
    return true;
}

} // namespace beerocks
