/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef TRAFFIC_SEPARATION_UTILS
#define TRAFFIC_SEPARATION_UTILS

#include <cstdint>
#include <string>

#include <bcl/beerocks_defines.h>

namespace beerocks::net {

/**
 * @brief Traffic separation configuration that includes information about 
 * - bridge names
 * - bridge VLANs
 */
struct sTrafficSeparationConfig {
    std::string private_bridge;
    uint32_t private_vid;

    std::string guest_bridge;
    uint32_t guest_vid;

    bool operator==(const sTrafficSeparationConfig &other) const
    {
        return private_bridge == other.private_bridge && private_vid == other.private_vid &&
               guest_bridge == other.guest_bridge && guest_vid == other.guest_vid;
    }
};

/**
 * @brief Describes a trunk port candidate for traffic separation
 * 
 * @example usually it's backhaul iface or AP iface (e.g. "wlan1", "eth1" or "wlan0.sta1")
 */
struct sTrunkPort {
    std::string iface_name;

    // Depends on the Multi-AP profile of the connected upstream/downstream device
    bool is_untagged_mode = true;

    // ETH or WiFi trunk
    bool is_ethernet = false;
};

/**
 * @brief Logical access role for a fronthaul/access interface.
 */
enum class eAccessRole : uint8_t { HOME = 0, GUEST };

/**
 * @brief Describes a fronthaul/access interface and its logical role.
 * 
 * @example usually it's FH ifaces (e.g. "wlan0.x")
 */
struct sAccessPort {
    std::string iface_name;
    eAccessRole role = eAccessRole::HOME;
};

/**
 * @brief Function to derrive if trunk is expected to be TAGGED or UNTAGGED
 * 
 * @param[in] dis_p1 - disallow Profile-1 flag
 * @param[in] dis_p2 - disallow Profile-1 flag
 * @param[in] policy - eUnsupportedProfileDisallowPolicy enum to force settings if unresolved via dissaloved flags
 * 
 * @return true if untagged mode, false otherwise
 * 
 * @details https://prplfoundationcloud.atlassian.net/wiki/spaces/PRPLMESH/pages/1364426774/TS+Design.+Two+bridges+with+802.1Q+subinterfaces#Allow%2Fdissallow-legacy-logic
 * 
 */
inline bool is_untagged_mode(bool dis_p1, bool dis_p2, eUnsupportedProfileDisallowPolicy policy)
{
    const bool valid_p1 = (!dis_p1 && dis_p2);
    const bool valid_p2 = (dis_p1 && !dis_p2);

    if (valid_p1)
        return false;
    if (valid_p2)
        return true;

    return (policy == eUnsupportedProfileDisallowPolicy::FORCE_PROFILE2);
}

} // namespace beerocks::net

#endif // TRAFFIC_SEPARATION_UTILS
