/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <gtest/gtest.h>

#include <bcl/beerocks_string_utils.h>
#include <bwl/base_wlan_hal.h>

namespace bwl {
namespace tests {

TEST(csv_bitmask_test, op_class_extraction)
{
    const std::string channels_5ghz =
        "36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144";

    constexpr uint16_t op_class_115_bitmask = 0b1111;
    constexpr uint16_t op_class_118_bitmask = op_class_115_bitmask << 4;
    const std::string theoretical_115_chans = "36,40,44,48";
    const std::string theoretical_118_chans = "52,56,60,64";

    std::string practical_115_chans, practical_118_chans;
    bwl::base_wlan_hal::apply_bitmask_to_csv(channels_5ghz, op_class_115_bitmask,
                                             practical_115_chans);
    bwl::base_wlan_hal::apply_bitmask_to_csv(channels_5ghz, op_class_118_bitmask,
                                             practical_118_chans);

    // validate in a static puncturing use case
    GTEST_ASSERT_EQ(theoretical_115_chans, practical_115_chans);
    GTEST_ASSERT_EQ(theoretical_118_chans, practical_118_chans);
}

TEST(csv_bitmask_test, generic_csv_scenario)
{
    const std::string rs = "the,quick,brown,fox,jumps,over,the,lazy,dog";
    // rs - stands for random string

    // extract subset : first 4 elements
    constexpr uint16_t the_quick_brown_mask              = 0b1111;
    const std::string theoretical_the_quick_brown_string = "the,quick,brown,fox";

    std::string practical_the_quick_brown_string;
    bwl::base_wlan_hal::apply_bitmask_to_csv(rs, the_quick_brown_mask,
                                             practical_the_quick_brown_string);
    GTEST_ASSERT_EQ(theoretical_the_quick_brown_string, practical_the_quick_brown_string);

    // extract arbitrary element
    const std::string theoretically_lazy = "lazy";

    auto discrete_elems = beerocks::string_utils::str_split(rs, ',');
    auto index_of_lazy =
        std::distance(discrete_elems.begin(),
                      std::find(discrete_elems.begin(), discrete_elems.end(), theoretically_lazy));
    uint16_t bitmask_of_lazy = (1 << index_of_lazy);

    std::string practically_lazy;
    bwl::base_wlan_hal::apply_bitmask_to_csv(rs, bitmask_of_lazy, practically_lazy);

    GTEST_ASSERT_EQ(practically_lazy, theoretically_lazy);
}

} // namespace tests
} // namespace bwl
