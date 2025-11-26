/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef ACCESS_PORT_PLUMBING_8021Q
#define ACCESS_PORT_PLUMBING_8021Q

#include "port_plumbing.h"
#include "traffic_separation_utils.h"

namespace beerocks::net {

/**
 * @brief 802.1Q-based access-port plumbing.
 *
 * This class implements the TS 8021q subifaces design behavior for fronthaul/access ports
 * (BSS interfaces). 
 */
class AccessPortPlumbing8021q final : public PortPlumbing {
public:
    explicit AccessPortPlumbing8021q(const sAccessPort &access_port);

    /**
     * @brief Apply plumbing for the access port (fronthaul).
     *
     * @param cfg Traffic separation configuration to apply.
     *            Currently unused for access ports, but accepted to keep
     *            the PortPlumbing interface uniform.
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
     * @brief Access port that is managed by this object.
     */
    sAccessPort m_access_port{};

    /**
     * @brief Indicates whether this access port has a valid applied state.
     */
    bool m_is_applied = false;
};

} // namespace beerocks::net

#endif // ACCESS_PORT_PLUMBING_8021Q
