// port_plumbing.h
/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#ifndef PORT_PLUMBING
#define PORT_PLUMBING

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

#endif // PORT_PLUMBING
