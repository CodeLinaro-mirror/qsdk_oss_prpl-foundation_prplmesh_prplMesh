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
        cfg.guest_vid == 0 || cfg.private_vid > net::MAX_VLAN_ID ||
        cfg.guest_vid > net::MAX_VLAN_ID) {
        LOG(ERROR) << "invalid config";
        return false;
    }

    if (m_trunk.iface_name.empty()) {
        LOG(ERROR) << "empty trunk iface_name";
        return false;
    }

    if (!network_utils::linux_iface_exists(m_trunk.iface_name)) {
        LOG(ERROR) << "missing trunk iface=" << m_trunk.iface_name;
        return false;
    }

    // If TS is applied and config is unchanged -> do nothing.
    // If config changed -> clear old plumbing first.
    if (m_is_applied) {
        if (m_last_cfg == cfg) {
            LOG(DEBUG) << "trunk=" << m_trunk.iface_name << " has already TS policies applied.";
            return true;
        }
        LOG(DEBUG) << "Clearing and applying new policies.";
        if (!clear()) {
            LOG(ERROR) << "failed to clear old plumbing";
            return false;
        }
    }

    // For 802.1Q subifaces model, UNTAGGED mode keeps trunk on native/private bridge.
    if (m_trunk.is_untagged_mode) {
        m_private_subiface.clear();
        m_guest_subiface.clear();
        m_last_cfg   = cfg;
        m_is_applied = true;
        LOG(DEBUG) << "trunk=" << m_trunk.iface_name << " is configured as UNTAGGED";
        return true;
    }

    std::string private_subiface;
    std::string guest_subiface;
    std::string previous_bridge;

    auto rollback = [&]() {
        if (!guest_subiface.empty()) {
            network_utils::delete_interface(guest_subiface);
            guest_subiface.clear();
        }
        if (!private_subiface.empty()) {
            network_utils::delete_interface(private_subiface);
            private_subiface.clear();
        }
        if (!m_trunk.is_ethernet && !previous_bridge.empty()) {
            (void)network_utils::linux_add_iface_to_bridge(previous_bridge, m_trunk.iface_name);
        }
    };

    // ETH trunks support native VLAN and do not need a primary-VLAN subinterface.
    if (!m_trunk.is_ethernet) {
        previous_bridge = network_utils::linux_iface_get_host_bridge(m_trunk.iface_name);
        if (!previous_bridge.empty() &&
            !network_utils::linux_remove_iface_from_bridge(previous_bridge, m_trunk.iface_name)) {
            LOG(ERROR) << "failed to remove iface=" << m_trunk.iface_name
                       << " from bridge=" << previous_bridge;
            return false;
        }

        private_subiface =
            network_utils::create_vlan_interface(m_trunk.iface_name, cfg.private_vid);
        if (private_subiface.empty()) {
            LOG(ERROR) << "failed to create private subiface";
            rollback();
            return false;
        }

        network_utils::set_interface_state(private_subiface, true);
        if (!network_utils::linux_add_iface_to_bridge(cfg.private_bridge, private_subiface)) {
            LOG(ERROR) << "failed to add iface=" << private_subiface
                       << " to bridge=" << cfg.private_bridge;
            rollback();
            return false;
        }
    }

    guest_subiface = network_utils::create_vlan_interface(m_trunk.iface_name, cfg.guest_vid);
    if (guest_subiface.empty()) {
        LOG(ERROR) << "failed to create guest subiface";
        rollback();
        return false;
    }

    network_utils::set_interface_state(guest_subiface, true);
    if (!network_utils::linux_add_iface_to_bridge(cfg.guest_bridge, guest_subiface)) {
        LOG(ERROR) << "failed to add iface=" << guest_subiface << " to bridge=" << cfg.guest_bridge;
        rollback();
        return false;
    }

    m_private_subiface = std::move(private_subiface);
    m_guest_subiface   = std::move(guest_subiface);
    m_last_cfg         = cfg;
    m_is_applied       = true;

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
        LOG(TRACE) << "trunk is configured UNTAGGED; no L2 changes to rollback";
        m_is_applied = false;
        m_last_cfg   = {};
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

        // Reattach m_trunk.iface_name to a default bridge.
        // When backhaul switches away from this STA interface, reattach may become impossible
        // because the old wireless interface is no longer up/runnable. Treat that as best-effort.
        if (!m_last_cfg.private_bridge.empty()) {
            if (!network_utils::linux_iface_exists(m_trunk.iface_name)) {
                LOG(WARNING) << "skip reattach, iface=" << m_trunk.iface_name
                             << " no longer exists";
            } else if (!network_utils::linux_add_iface_to_bridge(m_last_cfg.private_bridge,
                                                                 m_trunk.iface_name)) {
                const auto is_up_and_running =
                    network_utils::linux_iface_is_up_and_running(m_trunk.iface_name);
                const auto current_bridge =
                    network_utils::linux_iface_get_host_bridge(m_trunk.iface_name);

                if (!is_up_and_running) {
                    LOG(WARNING) << "best-effort reattach skipped for inactive iface="
                                 << m_trunk.iface_name << ", bridge=" << m_last_cfg.private_bridge
                                 << ", current_bridge="
                                 << (current_bridge.empty() ? "<none>" : current_bridge);
                } else {
                    LOG(ERROR) << "failed to reattach iface=" << m_trunk.iface_name
                               << " to bridge=" << m_last_cfg.private_bridge << ", current_bridge="
                               << (current_bridge.empty() ? "<none>" : current_bridge);
                    return false;
                }
            }
        }
    }

    // Reset state after rollback.
    m_is_applied = false;
    m_last_cfg   = {};

    LOG(TRACE) << "Cleared TS policies for trunk iface=" << m_trunk.iface_name << " successfully";

    return true;
}

} // namespace beerocks::net
