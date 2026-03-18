/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025-2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "access_port_plumbing_8021q.h"
#include "traffic_separation/traffic_separation_utils.h"

#include <bcl/network/network_utils.h>
#include <easylogging++.h>

namespace beerocks::net {

AccessPortPlumbing8021q::AccessPortPlumbing8021q(const sAccessPort &access_port)
    : m_access_port(access_port)
{
}

bool AccessPortPlumbing8021q::apply(const sTrafficSeparationConfig &cfg)
{
    if (cfg == beerocks::net::sTrafficSeparationConfig()) {
        LOG(WARNING) << "TS config is empty";
    }

    if (m_access_port.iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return false;
    }

    if (!network_utils::linux_iface_exists(m_access_port.iface_name)) {
        LOG(WARNING) << "skip VLAN filter, iface=" << m_access_port.iface_name << " does not exist";
        m_is_applied = false;
        return true;
    }

    if (m_is_applied) {
        LOG(TRACE) << "iface=" << m_access_port.iface_name << " already applied";
        return true;
    }

    // In 802.1Q subifaces design, access ports only need a VLAN traffic filter
    // to block tagged and double-tagged traffic from fronthaul STAs.
    if (!network_utils::set_vlan_packet_filter(true, m_access_port.iface_name)) {
        LOG(ERROR) << "failed to set VLAN filter iface=" << m_access_port.iface_name;
        return false;
    }

    m_is_applied = true;

    LOG(TRACE) << "applied VLAN filter iface=" << m_access_port.iface_name;
    return true;
}

bool AccessPortPlumbing8021q::clear()
{
    if (!m_is_applied) {
        // Nothing to clear for this instance.
        return true;
    }

    // Disabled VAPs can disappear before TS cleanup runs.
    // A missing iface means the ingress filter is already effectively gone.
    if (!network_utils::linux_iface_exists(m_access_port.iface_name)) {
        m_is_applied = false;
        return true;
    }

    if (!network_utils::set_vlan_packet_filter(false, m_access_port.iface_name)) {
        LOG(ERROR) << "failed to clear VLAN filter iface=" << m_access_port.iface_name;
        return false;
    }

    m_is_applied = false;

    LOG(TRACE) << "cleared VLAN filter iface=" << m_access_port.iface_name;
    return true;
}

} // namespace beerocks::net
