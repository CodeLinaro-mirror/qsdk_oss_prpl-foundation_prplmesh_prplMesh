/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef BPL_DHCP_UTILS_H_
#define BPL_DHCP_UTILS_H_

#include <ambiorix_variant.h>
#include <bpl/bpl_dhcp.h>

namespace beerocks {
namespace bpl {
namespace dhcp {

/**
 * Parse a DHCPv4Server client subtree and MAC-matched Hosts base objects.
 * Addresses come exclusively from active DHCP clients, never from Hosts.
 * Select the address with the latest expiry per MAC, breaking ties by lowest IPv4.
 * Unknown expiry is accepted for active clients, with lower priority than a known expiry.
 */
ipv4_lease_map_t parse_ipv4_leases(const wbapi::AmbiorixVariantMap &clients,
                                   const wbapi::AmbiorixVariantMap &hosts,
                                   std::chrono::system_clock::time_point now);

} // namespace dhcp
} // namespace bpl
} // namespace beerocks

#endif // BPL_DHCP_UTILS_H_
