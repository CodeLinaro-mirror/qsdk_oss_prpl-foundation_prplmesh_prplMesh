/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "access_port_plumbing_8021q.h"

#include <bcl/network/network_utils.h>
#include <easylogging++.h>

namespace beerocks::net {

AccessPortPlumbing8021q::AccessPortPlumbing8021q(const sAccessPort &access_port)
    : m_access_port(access_port)
{
}

bool AccessPortPlumbing8021q::apply(const sTrafficSeparationConfig & /*cfg*/)
{
    if (m_access_port.iface_name.empty()) {
        LOG(ERROR) << "AccessPortPlumbing8021q::apply: empty iface_name";
        return false;
    }

    if (m_is_applied) {
        LOG(TRACE) << "AccessPortPlumbing8021q::apply: iface=" << m_access_port.iface_name
                   << " already applied";
        return true;
    }

    // In 8021q subifaces design for access ports is needed only a VLAN traffic filter
    // to avoid tagged, double-tagged traffic be sent from frothaul STAs side
    if (!network_utils::set_vlan_packet_filter(true, m_access_port.iface_name)) {
        LOG(ERROR) << "AccessPortPlumbing8021q::apply: failed to set VLAN filter iface="
                   << m_access_port.iface_name;
        return false;
    }

    m_is_applied = true;

    LOG(TRACE) << "AccessPortPlumbing8021q::apply: applied VLAN filter iface="
               << m_access_port.iface_name;
    return true;
}

bool AccessPortPlumbing8021q::clear()
{
    if (!m_is_applied) {
        // Nothing to clear for this instance.
        return true;
    }

    if (!network_utils::set_vlan_packet_filter(false, m_access_port.iface_name)) {
        LOG(ERROR) << "AccessPortPlumbing8021q::clear: failed to clear VLAN filter iface="
                   << m_access_port.iface_name;
        return false;
    }

    m_is_applied = false;

    LOG(TRACE) << "AccessPortPlumbing8021q::clear: cleared VLAN filter iface="
               << m_access_port.iface_name;
    return true;
}

} // namespace beerocks::net
