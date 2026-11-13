/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_dhcp_utils.h"

#include <arpa/inet.h>
#include <tlvf/tlvftypes.h>

namespace beerocks {
namespace bpl {
namespace dhcp {
namespace {

std::string normalize_path(std::string path)
{
    if (path.compare(0, 7, "Device.") == 0) {
        path.erase(0, 7);
    }
    if (!path.empty() && path.back() != '.') {
        path += '.';
    }
    return path;
}

bool read_mac(const wbapi::AmbiorixVariant &object, const char *parameter, sMacAddr &mac)
{
    std::string value;
    return object.read_child(value, parameter) && value.size() == 17 &&
           tlvf::mac_from_string(mac.oct, value) && mac != sMacAddr{} && !(mac.oct[0] & 1);
}

struct Address {
    sIPv4Lease lease;
    int64_t expiry_ms = 0;
    uint32_t ipv4     = 0;
};

} // namespace

ipv4_lease_map_t parse_ipv4_leases(const wbapi::AmbiorixVariantMap &clients,
                                   const wbapi::AmbiorixVariantMap &hosts,
                                   std::chrono::system_clock::time_point now)
{
    // Host discovery may contain duplicated or incorrect address lists. Read names only.
    std::unordered_map<sMacAddr, std::string> names;
    for (const auto &host : hosts) {
        sMacAddr mac{};
        std::string name;
        if (read_mac(host.second, "PhysAddress", mac) && host.second.read_child(name, "HostName") &&
            !name.empty()) {
            names.emplace(mac, name);
        }
    }

    std::unordered_map<std::string, sMacAddr> active_clients;
    for (const auto &client : clients) {
        sMacAddr mac{};
        bool active = false;
        if (client.second.read_child(active, "Active") && active &&
            read_mac(client.second, "Chaddr", mac)) {
            active_clients.emplace(normalize_path(client.first), mac);
        }
    }

    const auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::unordered_map<sMacAddr, Address> selected;
    const std::string address_table = "IPv4Address.";
    for (const auto &object : clients) {
        const auto path = normalize_path(object.first);
        const auto pos  = path.rfind("." + address_table);
        if (pos == std::string::npos) {
            continue;
        }
        // Match only direct address instances, not the table or deeper descendants.
        const auto instance = path.substr(pos + 1 + address_table.size());
        if (instance.size() < 2 || instance.back() != '.' ||
            instance.find_first_not_of("0123456789") != instance.size() - 1) {
            continue;
        }
        const auto client = active_clients.find(path.substr(0, pos + 1));
        if (client == active_clients.end()) {
            continue;
        }

        Address address;
        in_addr ip{};
        if (!object.second.read_child(address.lease.ip_address, "IPAddress") ||
            inet_pton(AF_INET, address.lease.ip_address.c_str(), &ip) != 1) {
            continue;
        }
        address.ipv4 = ntohl(ip.s_addr);
        if (address.ipv4 == INADDR_ANY || address.ipv4 == INADDR_BROADCAST ||
            IN_MULTICAST(address.ipv4) || (address.ipv4 >> 24) == 127) {
            continue;
        }

        // Despite its name, LeaseTimeRemaining is an absolute dateTime in this DM.
        // The TR-181 unknown time sentinel (year 1) and absent times have rank zero.
        std::string expiry;
        amxc_ts_t timestamp{};
        if (object.second.read_child(expiry, "LeaseTimeRemaining") &&
            amxc_ts_parse(&timestamp, expiry.c_str(), expiry.size()) == 0 && timestamp.sec > 0) {
            address.expiry_ms = timestamp.sec * 1000 + timestamp.nsec / 1000000;
            if (address.expiry_ms <= now_ms) {
                continue;
            }
            address.lease.validity = std::chrono::milliseconds(address.expiry_ms - now_ms);
        }

        auto entry = selected.find(client->second);
        if (entry == selected.end() || address.expiry_ms > entry->second.expiry_ms ||
            (address.expiry_ms == entry->second.expiry_ms && address.ipv4 < entry->second.ipv4)) {
            selected[client->second] = std::move(address);
        }
    }

    ipv4_lease_map_t leases;
    for (auto &entry : selected) {
        const auto name = names.find(entry.first);
        if (name != names.end()) {
            entry.second.lease.host_name = name->second;
        }
        leases.emplace(entry.first, std::move(entry.second.lease));
    }
    return leases;
}

} // namespace dhcp
} // namespace bpl
} // namespace beerocks
