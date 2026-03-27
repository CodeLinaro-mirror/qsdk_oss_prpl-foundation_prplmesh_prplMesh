/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef __FDB_MONITOR_H__
#define __FDB_MONITOR_H__

namespace beerocks {

/**
 * @brief Monitors Linux bridge FDB updates through NETLINK_ROUTE neighbor events.
 */
class fdb_monitor {
public:
    enum class EEvent {
        eInvalid,
        eFdbChanged,
    };

    ~fdb_monitor();

    /**
     * @brief Initialize the FDB monitor.
     *
     * @return true on success and false otherwise.
     */
    bool initialize();

    /**
     * @brief Stop the FDB monitor and release the netlink socket.
     */
    void stop();

    /**
     * @brief Process incoming netlink messages and detect AF_BRIDGE FDB updates.
     *
     * @return eFdbChanged when a bridge FDB entry changed, otherwise eInvalid.
     */
    EEvent process();

    int get_netlink_fd() const { return m_iNetlinkFD; }

private:
    int m_iNetlinkFD = -1; ///< netlink fd
};

} // namespace beerocks

#endif
