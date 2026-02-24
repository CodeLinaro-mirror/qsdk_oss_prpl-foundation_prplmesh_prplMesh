/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef PORT_PLUMBING_H
#define PORT_PLUMBING_H

#include "traffic_separation_utils.h"

namespace beerocks::net {

/**
 * @brief Common base class for per-port plumbing (trunk or access).
 */
class PortPlumbing {
public:
    virtual ~PortPlumbing() = default;

    /**
     * @brief Apply plumbing for this port.
     *
     * @param cfg Current traffic separation configuration.
     * @return true on success, false on error.
     */
    virtual bool apply(const sTrafficSeparationConfig &cfg) = 0;

    /**
     * @brief Rollback the last applied configuration.
     *
     * @return true on success (or nothing to do), false on error.
     */
    virtual bool clear() = 0;
};

} // namespace beerocks::net

#endif // PORT_PLUMBING_H
