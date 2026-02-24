/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../traffic_separation_utils.h"

#include <gtest/gtest.h>

// cppcheck-suppress-file syntaxError
namespace {

using beerocks::eUnsupportedProfileDisallowPolicy;
using beerocks::net::is_untagged_mode;
using beerocks::net::resolve_profile_disallow_flags;

TEST(TrafficSeparationUtilsTest, ResolveKeepsValidProfileDisallowFlags)
{
    bool disallow_profile1 = true;
    bool disallow_profile2 = false;

    EXPECT_TRUE(resolve_profile_disallow_flags(disallow_profile1, disallow_profile2,
                                               eUnsupportedProfileDisallowPolicy::NO_OVERRIDE));
    EXPECT_TRUE(disallow_profile1);
    EXPECT_FALSE(disallow_profile2);

    disallow_profile1 = false;
    disallow_profile2 = true;

    EXPECT_TRUE(resolve_profile_disallow_flags(disallow_profile1, disallow_profile2,
                                               eUnsupportedProfileDisallowPolicy::NO_OVERRIDE));
    EXPECT_FALSE(disallow_profile1);
    EXPECT_TRUE(disallow_profile2);
}

// cppcheck-suppress syntaxError
TEST(TrafficSeparationUtilsTest, ResolveFailsWithoutOverrideWhenFlagsAreInvalid)
{
    bool disallow_profile1 = false;
    bool disallow_profile2 = false;

    EXPECT_FALSE(resolve_profile_disallow_flags(disallow_profile1, disallow_profile2,
                                                eUnsupportedProfileDisallowPolicy::NO_OVERRIDE));
    EXPECT_FALSE(disallow_profile1);
    EXPECT_FALSE(disallow_profile2);

    disallow_profile1 = true;
    disallow_profile2 = true;

    EXPECT_FALSE(resolve_profile_disallow_flags(disallow_profile1, disallow_profile2,
                                                eUnsupportedProfileDisallowPolicy::NO_OVERRIDE));
    EXPECT_TRUE(disallow_profile1);
    EXPECT_TRUE(disallow_profile2);
}

TEST(TrafficSeparationUtilsTest, ResolveAppliesForceDisallowProfile1Policy)
{
    bool disallow_profile1 = false;
    bool disallow_profile2 = false;

    EXPECT_TRUE(
        resolve_profile_disallow_flags(disallow_profile1, disallow_profile2,
                                       eUnsupportedProfileDisallowPolicy::FORCE_DISALLOW_PROFILE1));
    EXPECT_TRUE(disallow_profile1);
    EXPECT_FALSE(disallow_profile2);

    disallow_profile1 = true;
    disallow_profile2 = true;

    EXPECT_TRUE(
        resolve_profile_disallow_flags(disallow_profile1, disallow_profile2,
                                       eUnsupportedProfileDisallowPolicy::FORCE_DISALLOW_PROFILE1));
    EXPECT_TRUE(disallow_profile1);
    EXPECT_FALSE(disallow_profile2);
}

TEST(TrafficSeparationUtilsTest, ResolveAppliesForceDisallowProfile2Policy)
{
    bool disallow_profile1 = false;
    bool disallow_profile2 = false;

    EXPECT_TRUE(
        resolve_profile_disallow_flags(disallow_profile1, disallow_profile2,
                                       eUnsupportedProfileDisallowPolicy::FORCE_DISALLOW_PROFILE2));
    EXPECT_FALSE(disallow_profile1);
    EXPECT_TRUE(disallow_profile2);

    disallow_profile1 = true;
    disallow_profile2 = true;

    EXPECT_TRUE(
        resolve_profile_disallow_flags(disallow_profile1, disallow_profile2,
                                       eUnsupportedProfileDisallowPolicy::FORCE_DISALLOW_PROFILE2));
    EXPECT_FALSE(disallow_profile1);
    EXPECT_TRUE(disallow_profile2);
}

TEST(TrafficSeparationUtilsTest, UntaggedModeMatchesResolvedProfileDisallowFlags)
{
    // Valid values should be used as-is.
    EXPECT_TRUE(is_untagged_mode(false, true, eUnsupportedProfileDisallowPolicy::NO_OVERRIDE));
    EXPECT_FALSE(is_untagged_mode(true, false, eUnsupportedProfileDisallowPolicy::NO_OVERRIDE));

    // Invalid values with no override fallback to "false" (tagged mode).
    EXPECT_FALSE(is_untagged_mode(false, false, eUnsupportedProfileDisallowPolicy::NO_OVERRIDE));
    EXPECT_FALSE(is_untagged_mode(true, true, eUnsupportedProfileDisallowPolicy::NO_OVERRIDE));

    // Invalid values with override policy should map to corresponding mode.
    EXPECT_FALSE(
        is_untagged_mode(false, false, eUnsupportedProfileDisallowPolicy::FORCE_DISALLOW_PROFILE1));
    EXPECT_TRUE(
        is_untagged_mode(false, false, eUnsupportedProfileDisallowPolicy::FORCE_DISALLOW_PROFILE2));
}

} // namespace
