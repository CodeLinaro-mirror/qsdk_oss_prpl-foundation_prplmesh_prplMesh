/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_dhcp_utils.h"

#include <algorithm>
#include <arpa/inet.h>
#include <sstream>
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

namespace {

using Ipv6Bytes = std::array<uint8_t, 16>;

struct Ipv6Owner {
    sMacAddr mac{};
    bool ambiguous = false;
};

struct Ipv6Candidate {
    sIPv6Lease lease;
    Ipv6Bytes bytes{};
    int scope         = 0;
    int64_t expiry_ms = 0;
    bool dhcp         = false;
    bool deprecated   = false;
};

std::string address_parent(const std::string &object_path)
{
    const auto path         = normalize_path(object_path);
    const std::string table = ".IPv6Address.";
    const auto pos          = path.rfind(table);
    if (pos == std::string::npos) {
        return {};
    }
    const auto instance = path.substr(pos + table.size());
    if (instance.size() < 2 || instance.find_first_not_of("0123456789") != instance.size() - 1 ||
        instance.back() != '.') {
        return {};
    }
    return path.substr(0, pos + 1);
}

// Hosts references use TR-181 names, whereas the common bus exposes service roots.
std::string normalize_ipv6_client(std::string path)
{
    const auto begin = path.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    path = normalize_path(path.substr(begin, path.find_last_not_of(" \t\r\n") - begin + 1));
    const std::string prefix = "DHCPv6.Server.";
    if (path.compare(0, prefix.size(), prefix) == 0) {
        path.replace(0, prefix.size(), "DHCPv6Server.");
    }
    return path;
}

void add_owner(std::map<Ipv6Bytes, Ipv6Owner> &owners, const Ipv6Bytes &ip, const Ipv6Owner &owner)
{
    auto result = owners.emplace(ip, owner);
    if (!result.second && (result.first->second.mac != owner.mac || owner.ambiguous)) {
        result.first->second.ambiguous = true;
    }
}

bool read_ipv6(const wbapi::AmbiorixVariant &object, Ipv6Candidate &candidate)
{
    std::string value;
    in6_addr ip{};
    if (!object.read_child(value, "IPAddress") || inet_pton(AF_INET6, value.c_str(), &ip) != 1) {
        return false;
    }
    // Limit display addresses to global unicast, ULA, and link-local unicast.
    if ((ip.s6_addr[0] & 0xe0) == 0x20) {
        candidate.scope = 3;
    } else if ((ip.s6_addr[0] & 0xfe) == 0xfc) {
        candidate.scope = 2;
    } else if (IN6_IS_ADDR_LINKLOCAL(&ip)) {
        candidate.scope = 1;
    } else {
        return false;
    }
    std::copy(std::begin(ip.s6_addr), std::end(ip.s6_addr), candidate.bytes.begin());
    char canonical[INET6_ADDRSTRLEN];
    if (!inet_ntop(AF_INET6, &ip, canonical, sizeof(canonical))) {
        return false;
    }
    candidate.lease.ip_address = canonical;
    return true;
}

int64_t expiry_ms(const wbapi::AmbiorixVariant &object, const char *parameter)
{
    std::string value;
    amxc_ts_t timestamp{};
    if (!object.read_child(value, parameter) ||
        amxc_ts_parse(&timestamp, value.c_str(), value.size()) != 0 || timestamp.sec <= 0) {
        return 0;
    }
    return timestamp.sec * 1000 + timestamp.nsec / 1000000;
}

bool better_address(const Ipv6Candidate &candidate, const Ipv6Candidate &current)
{
    if (candidate.deprecated != current.deprecated) {
        return !candidate.deprecated;
    }
    if (candidate.scope != current.scope) {
        return candidate.scope > current.scope;
    }
    if (candidate.dhcp != current.dhcp) {
        return candidate.dhcp;
    }
    if (candidate.expiry_ms != current.expiry_ms) {
        return candidate.expiry_ms > current.expiry_ms;
    }
    return candidate.bytes < current.bytes;
}

} // namespace

ipv6_lease_map_t parse_ipv6_leases(const wbapi::AmbiorixVariantMap &clients,
                                   const wbapi::AmbiorixVariantMap &hosts,
                                   const wbapi::AmbiorixVariantMap &addresses,
                                   std::chrono::system_clock::time_point now)
{
    std::unordered_map<std::string, sMacAddr> host_macs;
    std::unordered_map<sMacAddr, std::string> names;
    std::unordered_map<std::string, Ipv6Owner> references;
    for (const auto &host : hosts) {
        sMacAddr mac{};
        if (!read_mac(host.second, "PhysAddress", mac)) {
            continue;
        }
        host_macs.emplace(normalize_path(host.first), mac);
        std::string name;
        if (host.second.read_child(name, "HostName") && !name.empty()) {
            names.emplace(mac, name);
        }
        std::string refs;
        if (!host.second.read_child(refs, "DHCPClient")) {
            continue;
        }
        std::istringstream stream(refs);
        std::string ref;
        while (std::getline(stream, ref, ',')) {
            ref = normalize_ipv6_client(ref);
            if (ref.compare(0, 18, "DHCPv6Server.Pool.") != 0) {
                continue;
            }
            auto result = references.emplace(ref, Ipv6Owner{mac, false});
            if (!result.second && result.first->second.mac != mac) {
                result.first->second.ambiguous = true;
            }
        }
    }

    std::unordered_map<std::string, Ipv6Owner> active_clients;
    for (const auto &client : clients) {
        bool active     = false;
        const auto path = normalize_ipv6_client(client.first);
        const auto ref  = references.find(path);
        if (ref != references.end() && client.second.read_child(active, "Active") && active) {
            active_clients.emplace(path, ref->second);
        }
    }

    const auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::vector<Ipv6Candidate> candidates;
    std::map<Ipv6Bytes, Ipv6Owner> dhcp_owners, discovered_owners;
    for (const auto &address : clients) {
        const auto client =
            active_clients.find(normalize_ipv6_client(address_parent(address.first)));
        Ipv6Candidate candidate;
        if (client == active_clients.end() || !read_ipv6(address.second, candidate)) {
            continue;
        }
        // Retain ownership even for an expired lease, so its discovery copy cannot revive it.
        add_owner(dhcp_owners, candidate.bytes, client->second);
        if (client->second.ambiguous) {
            continue;
        }
        candidate.lease.mac = client->second.mac;
        candidate.dhcp      = true;
        candidate.expiry_ms = expiry_ms(address.second, "ValidLifetime");
        if (candidate.expiry_ms && candidate.expiry_ms <= now_ms) {
            continue;
        }
        if (candidate.expiry_ms) {
            candidate.lease.validity = std::chrono::milliseconds(candidate.expiry_ms - now_ms);
        }
        const auto preferred = expiry_ms(address.second, "PreferredLifetime");
        candidate.deprecated = preferred && preferred <= now_ms;
        candidates.push_back(std::move(candidate));
    }

    for (const auto &address : addresses) {
        const auto host = host_macs.find(address_parent(address.first));
        Ipv6Candidate candidate;
        if (host == host_macs.end() || !read_ipv6(address.second, candidate)) {
            continue;
        }
        candidate.lease.mac = host->second;
        add_owner(discovered_owners, candidate.bytes, Ipv6Owner{host->second, false});
        // Status is protected in Hosts. Honor it when available; don't require it.
        std::string status;
        if (address.second.read_child(status, "Status") && !status.empty() &&
            status != "reachable") {
            continue;
        }
        candidates.push_back(std::move(candidate));
    }

    std::unordered_map<sMacAddr, Ipv6Candidate> selected;
    for (auto &candidate : candidates) {
        const auto dhcp = dhcp_owners.find(candidate.bytes);
        if (dhcp != dhcp_owners.end()) {
            if (dhcp->second.ambiguous || !candidate.dhcp) {
                continue;
            }
        } else if (discovered_owners.at(candidate.bytes).ambiguous) {
            continue;
        }
        auto current = selected.find(candidate.lease.mac);
        if (current == selected.end() || better_address(candidate, current->second)) {
            selected[candidate.lease.mac] = std::move(candidate);
        }
    }

    ipv6_lease_map_t leases;
    for (auto &entry : selected) {
        const auto name = names.find(entry.first);
        if (name != names.end()) {
            entry.second.lease.host_name = name->second;
        }
        leases.emplace(tlvf::mac_to_string(entry.first), std::move(entry.second.lease));
    }
    return leases;
}

bool assemble_leases(const wbapi::AmbiorixVariantMap *ipv4, const wbapi::AmbiorixVariantMap *hosts,
                     const wbapi::AmbiorixVariantMap *ipv6,
                     const wbapi::AmbiorixVariantMap *addresses, leases_pair_t &leases,
                     sLeaseReadStatus &status, std::chrono::system_clock::time_point now)
{
    status = {};
    if (!ipv4 && (!hosts || (!ipv6 && !addresses))) {
        return false;
    }
    bool needs_dhcpv6 = false;
    if (hosts) {
        for (const auto &host : *hosts) {
            std::string refs, ref;
            if (!host.second.read_child(refs, "DHCPClient")) {
                continue;
            }
            std::istringstream stream(refs);
            while (std::getline(stream, ref, ',')) {
                if (normalize_ipv6_client(ref).rfind("DHCPv6Server.Pool.", 0) == 0) {
                    needs_dhcpv6 = true;
                }
            }
        }
    }
    status.ipv4_complete = ipv4 != nullptr;
    status.ipv6_complete = hosts && addresses && (!needs_dhcpv6 || ipv6);
    status.retry         = !ipv4 || !hosts || !addresses || (needs_dhcpv6 && !ipv6);

    const wbapi::AmbiorixVariantMap empty;
    leases_pair_t snapshot;
    snapshot.first  = parse_ipv4_leases(ipv4 ? *ipv4 : empty, hosts ? *hosts : empty, now);
    snapshot.second = parse_ipv6_leases(ipv6 ? *ipv6 : empty, hosts ? *hosts : empty,
                                        addresses ? *addresses : empty, now);
    leases          = std::move(snapshot);
    return true;
}

} // namespace dhcp
} // namespace bpl
} // namespace beerocks
