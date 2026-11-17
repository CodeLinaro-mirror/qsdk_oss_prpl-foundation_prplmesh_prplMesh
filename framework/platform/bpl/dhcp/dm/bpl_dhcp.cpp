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

bool dhcp_get_leases(leases_pair_t &leases_map, sLeaseReadStatus *status)
{
    if (status) {
        *status = {};
    }
    auto *client = BplConfigService::instance().common_client();
    if (!client) {
        LOG(ERROR) << "BPL common Ambiorix client is not initialized";
        return false;
    }

    // Query tables so zero instances is a successful empty snapshot. Depth 5 includes
    // Pool instance -> Client table -> client instance -> address table -> address instance.
    // Four bulk reads remain independent of station count.
    auto clients =
        client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>("DHCPv4Server.Pool.", 5);
    if (!clients) {
        LOG(DEBUG) << "DHCPv4 leases unavailable; attempting IPv6 discovery";
    }

    auto hosts = client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>("Hosts.Host.", 1);
    if (!hosts) {
        LOG(WARNING) << "Failed to read Hosts; IPv6 MAC association and hostnames unavailable";
    }

    // IPv6 reads are optional: an unavailable IPv6 service must not block IPv4 updates.
    // Read only the IPv6 children, avoiding the potentially large Hosts IPv4 tables.
    wbapi::AmbiorixVariantMapSmartPtr ipv6_clients, ipv6_addresses;
    if (hosts) {
        ipv6_clients =
            client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>("DHCPv6Server.Pool.", 5);
        ipv6_addresses = client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>(
            "Hosts.Host.*.IPv6Address.", 1);
        // With no Host instances there are no address tables to match. The successful
        // base read still authoritatively establishes an empty discovery snapshot.
        if (!ipv6_addresses &&
            (hosts->empty() || (hosts->size() == 1 && (hosts->count("Hosts.Host.") ||
                                                       hosts->count("Device.Hosts.Host."))))) {
            ipv6_addresses = std::make_unique<wbapi::AmbiorixVariantMap>();
        }
        LOG_IF(!ipv6_clients, DEBUG) << "DHCPv6 leases unavailable; using Hosts IPv6 discovery";
        LOG_IF(!ipv6_addresses, DEBUG) << "Hosts IPv6 discovery unavailable";
    }

    sLeaseReadStatus result;
    const auto success =
        dhcp::assemble_leases(clients.get(), hosts.get(), ipv6_clients.get(), ipv6_addresses.get(),
                              leases_map, result, std::chrono::system_clock::now());
    if (status) {
        *status = result;
    }
    return success;
}

} // namespace bpl
} // namespace beerocks
