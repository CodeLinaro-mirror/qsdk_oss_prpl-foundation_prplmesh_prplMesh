/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef TRUNK_PLUMBING_8021Q
#define TRUNK_PLUMBING_8021Q

#include "port_plumbing.h"
#include "traffic_separation_utils.h"

namespace beerocks::net {

/**
 * @brief 802.1Q-based implementation of per-trunk traffic-separation plumbing.
 *
 * This class applies the 8021q subifaces Design layout for a single trunk interface by:
 * - Detaching the trunk from its current bridge.
 * - Creating VLAN subinterfaces for private and guest VIDs.
 * - Enslaving those subinterfaces to the configured bridges.
 */
class TrunkPortPlumbing8021q final : public PortPlumbing {
public:
    /**
     * @brief Constructs a new TrunkPortPlumbing8021q object for a specific trunk.
     *
     * @param trunk_port Trunk port descriptor containing the interface name
     *                   (e.g. "wlan0.sta1" or "eth0") to be managed by this
     *                   plumbing instance.
     */
    explicit TrunkPortPlumbing8021q(const sTrunkPort &trunk_port);

    /**
     * @brief Apply plumbing for the associated trunk.
     *
     * @param cfg Traffic separation configuration to apply.
     * @return true on successful apply, false on error.
     */
    bool apply(const sTrafficSeparationConfig &cfg) override;

    /**
     * @brief Rollback of the last applied configuration.
     *
     * @return true if rollback completed successfully (or nothing to do),
     *         false if a rollback error occurred.
     */
    bool clear() override;

private:
    /**
     * @brief Trunk that is managed by current object
     */
    sTrunkPort m_trunk;

    /**
     * @brief Subifaces names
     */
    std::string m_private_subiface, m_guest_subiface;

    /**
     * @brief Last configuration successfully applied on this trunk.
     */
    sTrafficSeparationConfig m_last_cfg{};

    /**
     * @brief Indicates whether @c m_last_cfg contains a valid applied state.
     */
    bool m_is_applied = false;
};

} // namespace beerocks::net

#endif // TRUNK_PLUMBING_8021Q
