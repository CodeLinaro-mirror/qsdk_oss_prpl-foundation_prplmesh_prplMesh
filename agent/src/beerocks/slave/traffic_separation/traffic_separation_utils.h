/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef TRAFFIC_SEPARATION_UTILS_H
#define TRAFFIC_SEPARATION_UTILS_H

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
 * @brief Resolve unsupported disallow flags using certification policy.
 *
 * Valid inputs have exactly one disallow bit set:
 * - disallow Profile-1 => tagged mode
 * - disallow Profile-2 => untagged mode
 *
 * Unsupported inputs (both true or both false) may be overridden by policy.
 *
 * @return true if flags are valid after resolution, false if unresolved.
 */
inline bool resolve_profile_disallow_flags(bool &dis_p1, bool &dis_p2,
                                           eUnsupportedProfileDisallowPolicy policy)
{
    if (dis_p1 != dis_p2) {
        return true;
    }

    switch (policy) {
    case eUnsupportedProfileDisallowPolicy::FORCE_DISALLOW_PROFILE1:
        dis_p1 = true;
        dis_p2 = false;
        return true;
    case eUnsupportedProfileDisallowPolicy::FORCE_DISALLOW_PROFILE2:
        dis_p1 = false;
        dis_p2 = true;
        return true;
    case eUnsupportedProfileDisallowPolicy::NO_OVERRIDE:
    default:
        return false;
    }
}

/**
 * @brief Derive trunk mode (tagged/untagged) from profile-disallow flags and policy.
 *
 * @return true for untagged mode, false for tagged mode.
 */
inline bool is_untagged_mode(bool dis_p1, bool dis_p2, eUnsupportedProfileDisallowPolicy policy)
{
    if (!resolve_profile_disallow_flags(dis_p1, dis_p2, policy)) {
        return false;
    }
    return (!dis_p1 && dis_p2);
}

} // namespace beerocks::net

#endif // TRAFFIC_SEPARATION_UTILS_H
