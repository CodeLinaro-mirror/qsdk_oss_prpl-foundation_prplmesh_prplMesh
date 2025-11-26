/* SPDX-License-Identifier: BSD-2-Clause-Patent
    *
    * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
    *
    * This code is subject to the terms of the BSD+Patent license.
    * See LICENSE file for more details.
    */

#include "trunk_port_plumbing_8021q.h"

#include <easylogging++.h>

#include <bcl/network/network_utils.h>

namespace beerocks::net {

TrunkPortPlumbing8021q::TrunkPortPlumbing8021q(const sTrunkPort &trunk_port) : m_trunk(trunk_port)
{
}

bool TrunkPortPlumbing8021q::apply(const sTrafficSeparationConfig &cfg)
{
    if (cfg.private_bridge.empty() || cfg.guest_bridge.empty() || cfg.private_vid == 0 ||
        cfg.guest_vid == 0) {
        LOG(ERROR) << "TrunkPortPlumbing8021q::apply: invalid config";
        return false;
    }

    // if TS is applied and config not changed -> do nothing,
    // else if config changed -> clear and apply
    if (m_is_applied) {
        if (m_last_cfg == cfg) {
            LOG(DEBUG) << "TrunkPortPlumbing8021q::apply trunk=" << m_trunk.iface_name
                       << " has already TS policies applied.";
            return true;
        }
        LOG(DEBUG) << "Clearing and applying new policies.";
        clear();
    }

    m_last_cfg   = cfg;
    m_is_applied = true;

    // If trunk was configured as UNTAGGED
    // For 8021q subifaces model UNTAGGED mode means to keep original iface in the br-lan -> do nothing
    if (m_trunk.is_untagged_mode) {
        LOG(DEBUG) << "TrunkPortPlumbing8021q::apply: trunk=" << m_trunk.iface_name
                   << " is configured as UNTAGGED";
        return true;
    }

    // ETH trunks support native vlan and do not need to have primary vlan subifaces
    if (!m_trunk.is_ethernet) {
        // Remove trunk from private bridge
        if (!network_utils::linux_remove_iface_from_bridge(cfg.private_bridge,
                                                           m_trunk.iface_name)) {
            LOG(ERROR) << "TrunkPortPlumbing8021q::apply: Failed to remove iface="
                       << m_trunk.iface_name << " from bridge=" << cfg.private_bridge;
            return false;
        }

        // Handle PRIVATE network
        m_private_subiface =
            network_utils::create_vlan_interface(m_trunk.iface_name, cfg.private_vid);
        network_utils::set_interface_state(m_private_subiface, true);
        if (!network_utils::linux_add_iface_to_bridge(cfg.private_bridge, m_private_subiface)) {
            LOG(ERROR) << "TrunkPortPlumbing8021q::apply: Failed to add iface="
                       << m_private_subiface << " to bridge=" << cfg.private_bridge;
            return false;
        }
    }

    // Handle GUEST network
    m_guest_subiface = network_utils::create_vlan_interface(m_trunk.iface_name, cfg.guest_vid);
    network_utils::set_interface_state(m_guest_subiface, true);
    if (!network_utils::linux_add_iface_to_bridge(cfg.guest_bridge, m_guest_subiface)) {
        LOG(ERROR) << "TrunkPortPlumbing8021q::apply: Failed to add iface=" << m_guest_subiface
                   << " to bridge=" << cfg.guest_bridge;
        return false;
    }

    LOG(TRACE) << "Applied TS policies for trunk iface=" << m_trunk.iface_name << " successfully";

    return true;
}

bool TrunkPortPlumbing8021q::clear()
{
    if (!m_is_applied) {
        // Trunk wasn't applied. Nothing to clear
        return true;
    }

    if (m_trunk.is_untagged_mode) {
        // Trunk was applied as UNTAGGED. Nothing to clear
        LOG(TRACE) << "TP8021q::clear: trunk is configured UNTAGGED; no L2 changes to rollback";
        return true;
    }

    // Clear GUEST network setup
    if (!m_guest_subiface.empty()) {
        network_utils::delete_interface(m_guest_subiface);
        m_guest_subiface = {};
    }

    // If wireless trunk, then PRIVATE network has subiface for primary vlan -> needs to be cleared
    if (!m_trunk.is_ethernet) {
        // Remove private subiface
        if (!m_private_subiface.empty()) {
            network_utils::delete_interface(m_private_subiface);
            m_private_subiface = {};
        }

        // Reattach m_trunk.iface_name to a default bridge
        if (!network_utils::linux_add_iface_to_bridge(m_last_cfg.private_bridge,
                                                      m_trunk.iface_name)) {
            LOG(ERROR) << "TrunkPortPlumbing8021q::clear: Failed to reattach iface="
                       << m_trunk.iface_name << " to bridge=" << m_last_cfg.private_bridge;
            return false;
        }
    }

    // Reset m_is_applied to false.
    m_is_applied = false;

    LOG(TRACE) << "Cleared TS policies for trunk iface=" << m_trunk.iface_name << " successfully";

    return true;
}

} // namespace beerocks::net
