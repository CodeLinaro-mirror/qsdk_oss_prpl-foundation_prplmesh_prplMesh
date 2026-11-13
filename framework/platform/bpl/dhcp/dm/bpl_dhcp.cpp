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

    // Two bulk reads, independent of station count. Depth 2 includes address instances.
    auto clients = client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>(
        "DHCPv4Server.Pool.*.Client.*.", 2);
    if (!clients) {
        LOG(ERROR) << "Failed to read DHCPv4Server clients";
        return false;
    }

    auto hosts = client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>("Hosts.Host.*.", 0);
    if (!hosts) {
        LOG(WARNING) << "Failed to read Hosts hostnames; updating DHCP IPv4 addresses only";
    }

    // Publish a complete snapshot only after the required DHCP read succeeds.
    const wbapi::AmbiorixVariantMap empty_hosts;
    leases_pair_t leases;
    leases.first = dhcp::parse_ipv4_leases(*clients, hosts ? *hosts : empty_hosts,
                                           std::chrono::system_clock::now());
    leases_map   = std::move(leases);
    return true;
}

} // namespace bpl
} // namespace beerocks
