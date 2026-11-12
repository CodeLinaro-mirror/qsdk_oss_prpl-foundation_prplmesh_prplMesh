/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef BACKHAUL_UTILS_H
#define BACKHAUL_UTILS_H

#include "agent.h"

namespace prplmesh {
namespace controller {
namespace db {

// A one-sided Ethernet neighbor report may identify the parent without identifying its port.
// Reuse the previous port only for the same Ethernet parent; a wireless BSSID is not a LAN port.
inline sMacAddr resolve_ethernet_parent_interface(const Agent::sBackhaul &previous_backhaul,
                                                  const sMacAddr &parent_al_mac,
                                                  const sMacAddr &reported_parent_interface)
{
    if (reported_parent_interface != beerocks::net::network_utils::ZERO_MAC) {
        return reported_parent_interface;
    }
    const auto previous_parent = previous_backhaul.parent_agent.lock();
    if (previous_parent && previous_backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_ETHERNET &&
        previous_parent->al_mac == parent_al_mac) {
        return previous_backhaul.parent_interface;
    }
    return beerocks::net::network_utils::ZERO_MAC;
}

} // namespace db
} // namespace controller
} // namespace prplmesh

#endif // BACKHAUL_UTILS_H
