/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <gtest/gtest.h>

#include <bwl/afc_spectrum_helper.h>

namespace bwl {
namespace tests {

using bwl::whm::afc_spectrum_helper::parse_possible_channels_list;

TEST(afc_spectrum_helper, parse_list_basic)
{
    std::unordered_set<uint8_t> channels;
    ASSERT_TRUE(parse_possible_channels_list("5,21,37,7", channels));
    ASSERT_EQ(channels.size(), 4U);
    EXPECT_NE(channels.count(5), 0U);
    EXPECT_NE(channels.count(21), 0U);
    EXPECT_NE(channels.count(37), 0U);
    EXPECT_NE(channels.count(7), 0U);
}

TEST(afc_spectrum_helper, parse_list_skips_empty_tokens)
{
    std::unordered_set<uint8_t> channels;
    ASSERT_TRUE(parse_possible_channels_list("5,,21,", channels));
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_NE(channels.count(5), 0U);
    EXPECT_NE(channels.count(21), 0U);
}

TEST(afc_spectrum_helper, parse_list_rejects_empty)
{
    std::unordered_set<uint8_t> channels;
    EXPECT_FALSE(parse_possible_channels_list("", channels));
    EXPECT_TRUE(channels.empty());
    EXPECT_FALSE(parse_possible_channels_list(",,", channels));
    EXPECT_TRUE(channels.empty());
}

TEST(afc_spectrum_helper, parse_list_clears_previous_contents)
{
    std::unordered_set<uint8_t> channels;
    channels.insert(1);
    channels.insert(2);
    channels.insert(3);
    ASSERT_TRUE(parse_possible_channels_list("9", channels));
    ASSERT_EQ(channels.size(), 1U);
    EXPECT_NE(channels.count(9), 0U);
}

TEST(afc_spectrum_helper, dm_path_constants)
{
    EXPECT_STREQ(bwl::whm::afc_spectrum_helper::AFC_STATS_PATH, "Device.WiFi.AFC.Stats.");
    EXPECT_STREQ(bwl::whm::afc_spectrum_helper::AFC_REQUEST_PARAM, "AvailableSpectrumRequest");
    EXPECT_STREQ(bwl::whm::afc_spectrum_helper::AFC_RESPONSE_PARAM, "AvailableSpectrumResponse");
    EXPECT_STREQ(bwl::whm::afc_spectrum_helper::AFC_GRANT_STATUS_PARAM, "GrantStatus");
    EXPECT_STREQ(bwl::whm::afc_spectrum_helper::RADIO_POWER_TYPE_PARAM, "PowerType");
    EXPECT_STREQ(bwl::whm::afc_spectrum_helper::RADIO_POSSIBLE_CHANNELS_PARAM, "PossibleChannels");
}

} // namespace tests
} // namespace bwl
