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

/**
 * Join DHCPv6 clients to MACs using Hosts.DHCPClient references, and supplement with
 * Hosts IPv6 discovery. Never infer a MAC from a DUID or an IPv6 interface identifier.
 * Prefer non-deprecated addresses, then global over ULA over link-local, then DHCP
 * over discovery, latest validity, and finally the lowest numeric IPv6 address.
 * Reject conflicting ownership and known unusable discovery status. Missing status
 * is accepted because Hosts declares Status protected and normally omits it.
 * The returned map is keyed by canonical MAC, with one address per station.
 */
ipv6_lease_map_t parse_ipv6_leases(const wbapi::AmbiorixVariantMap &clients,
                                   const wbapi::AmbiorixVariantMap &hosts,
                                   const wbapi::AmbiorixVariantMap &addresses,
                                   std::chrono::system_clock::time_point now);

/**
 * Assemble independently read sources. A null pointer denotes failure; an empty map
 * denotes a successful empty read. IPv6 removal requires Hosts discovery plus DHCPv6
 * when Hosts contains DHCPv6 references. Failed reads must never authorize removal.
 * On total failure, leave leases unchanged and reset status.
 */
bool assemble_leases(const wbapi::AmbiorixVariantMap *ipv4, const wbapi::AmbiorixVariantMap *hosts,
                     const wbapi::AmbiorixVariantMap *ipv6,
                     const wbapi::AmbiorixVariantMap *addresses, leases_pair_t &leases,
                     sLeaseReadStatus &status, std::chrono::system_clock::time_point now);

} // namespace dhcp
} // namespace bpl
} // namespace beerocks

#endif // BPL_DHCP_UTILS_H_
