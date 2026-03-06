/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _HIGHER_LAYER_COLLECTION_TASK_IFADDRS_IMPL_H_
#define _HIGHER_LAYER_COLLECTION_TASK_IFADDRS_IMPL_H_

#include "higher_layer_collection_task.h"

namespace beerocks {

/**
 * @brief Linux interface-address implementation for network interface data collection.
 * Concrete implementation that uses getifaddrs() to query the current
 * interface-address snapshot exposed by the kernel.
 */
class HigherLayerCollectionTaskIfAddrsImpl
    : public HigherLayerCollectionTask::InterfaceStatusProvider {
public:
    /**
     * @brief Fetches current network interface statuses from the Linux kernel.
     * Overrides the base refresh method. It reads the current interface list
     * with getifaddrs() and parses the returned entries into the provided map.
     * @param[out] interfaces_network_status Map of interface names to their
     * network status structures (MAC, IPv4, IPv6).
     * @return true if the kernel was successfully queried and data was parsed,
     * false if socket communication failed.
     */
    bool refresh(std::unordered_map<std::string, HigherLayerCollectionTask::sInterfaceNetworkStatus>
                     &interfaces_network_status) override;
};

} // namespace beerocks

#endif // _HIGHER_LAYER_COLLECTION_TASK_IFADDRS_IMPL_H_
