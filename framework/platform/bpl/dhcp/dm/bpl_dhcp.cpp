/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_dhcp_utils.h"

#include "ambiorix_client.h"
#include "bpl_cfg_service.h"
#include <bpl/bpl_err.h>

namespace beerocks {
namespace bpl {

int dhcp_mon_start(dhcp_mon_cb cb)
{
    return -static_cast<int>(eErrorCode::OPERATION_NOT_SUPPORTED);
}

int dhcp_mon_handle_event() { return 0; }

int dhcp_mon_stop() { return 0; }

bool dhcp_manual_procedure_init()
{
    return BplConfigService::instance().common_client() != nullptr;
}

// The shared bus client belongs to BplConfigService.
bool dhcp_manual_procedure_destroy() { return true; }

bool dhcp_get_leases(leases_pair_t &leases_map)
{
    auto *client = BplConfigService::instance().common_client();
    if (!client) {
        LOG(ERROR) << "BPL common Ambiorix client is not initialized";
        return false;
    }

    // Bulk reads are independent of station count. Depth 2 includes address instances.
    auto clients = client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>(
        "DHCPv4Server.Pool.*.Client.*.", 2);
    if (!clients) {
        LOG(DEBUG) << "DHCPv4 leases unavailable; attempting IPv6 discovery";
    }

    auto hosts = client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>("Hosts.Host.*.", 0);
    if (!hosts) {
        LOG(WARNING) << "Failed to read Hosts; IPv6 MAC association and hostnames unavailable";
    }

    // IPv6 reads are optional: an unavailable IPv6 service must not block IPv4 updates.
    // Read only the IPv6 children, avoiding the potentially large Hosts IPv4 tables.
    wbapi::AmbiorixVariantMapSmartPtr ipv6_clients, ipv6_addresses;
    if (hosts) {
        ipv6_clients = client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>(
            "DHCPv6Server.Pool.*.Client.*.", 2);
        ipv6_addresses = client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>(
            "Hosts.Host.*.IPv6Address.*.", 0);
        LOG_IF(!ipv6_clients, DEBUG) << "DHCPv6 leases unavailable; using Hosts IPv6 discovery";
        LOG_IF(!ipv6_addresses, DEBUG) << "Hosts IPv6 discovery unavailable";
    }

    // Preserve the caller's snapshot if neither address family could be read.
    // Either family can update independently; omitted entries leave cached STA values intact.
    if (!clients && (!hosts || (!ipv6_clients && !ipv6_addresses))) {
        return false;
    }
    const wbapi::AmbiorixVariantMap empty;
    const auto now = std::chrono::system_clock::now();
    leases_pair_t leases;
    leases.first = dhcp::parse_ipv4_leases(clients ? *clients : empty, hosts ? *hosts : empty, now);
    leases.second =
        dhcp::parse_ipv6_leases(ipv6_clients ? *ipv6_clients : empty, hosts ? *hosts : empty,
                                ipv6_addresses ? *ipv6_addresses : empty, now);
    leases_map = std::move(leases);
    return true;
}

} // namespace bpl
} // namespace beerocks
