/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../dpp_relay_frames.h"

#include <gtest/gtest.h>

#include <vector>

// cppcheck does not understand GoogleTest TEST() macros.
// cppcheck-suppress-file syntaxError

using namespace beerocks::dpp_relay;

namespace {

std::vector<uint8_t> attr(uint16_t id, uint16_t len, uint8_t fill)
{
    std::vector<uint8_t> a = {uint8_t(id), uint8_t(id >> 8), uint8_t(len), uint8_t(len >> 8)};
    a.insert(a.end(), len, fill);
    return a;
}

TEST(dpp_relay_frames, finds_hash_after_other_attribute)
{
    auto attrs = attr(0x1000, 1, 0x00);
    auto hash  = attr(0x1002, 32, 0xAB);
    attrs.insert(attrs.end(), hash.begin(), hash.end());

    uint8_t out[DPP_BOOTSTRAP_KEY_HASH_LEN] = {};
    ASSERT_TRUE(find_bootstrap_key_hash(attrs.data(), attrs.size(), out));
    EXPECT_EQ(out[0], 0xAB);
    EXPECT_EQ(out[31], 0xAB);
}

TEST(dpp_relay_frames, rejects_missing_or_malformed_hash)
{
    uint8_t out[DPP_BOOTSTRAP_KEY_HASH_LEN] = {};

    auto wrong_len = attr(0x1002, 31, 0xAB);
    EXPECT_FALSE(find_bootstrap_key_hash(wrong_len.data(), wrong_len.size(), out));

    auto overrun = attr(0x1002, 32, 0xAB);
    overrun.pop_back();
    EXPECT_FALSE(find_bootstrap_key_hash(overrun.data(), overrun.size(), out));

    const uint8_t huge_len[] = {0x00, 0x10, 0xFF, 0xFF};
    EXPECT_FALSE(find_bootstrap_key_hash(huge_len, sizeof(huge_len), out));

    EXPECT_FALSE(find_bootstrap_key_hash(nullptr, 0, out));
}

TEST(dpp_relay_frames, uplink_only_frames)
{
    const uint8_t gas_initial_req[]  = {0x0A, 0x00};
    const uint8_t gas_comeback_req[] = {0x0C, 0x00};
    const uint8_t auth_resp[]        = {0x09, 0x50, 0x6F, 0x9A, 0x1A, 0x01, 0x01};
    const uint8_t config_result[]    = {0x04, 0x09, 0x50, 0x6F, 0x9A, 0x1A, 0x01, 0x0B};

    EXPECT_TRUE(is_uplink_only_frame(gas_initial_req, sizeof(gas_initial_req)));
    EXPECT_TRUE(is_uplink_only_frame(gas_comeback_req, sizeof(gas_comeback_req)));
    EXPECT_TRUE(is_uplink_only_frame(auth_resp, sizeof(auth_resp)));
    EXPECT_TRUE(is_uplink_only_frame(config_result, sizeof(config_result)));
}

TEST(dpp_relay_frames, downlink_and_short_frames_pass)
{
    const uint8_t auth_req[]     = {0x09, 0x50, 0x6F, 0x9A, 0x1A, 0x01, 0x00};
    const uint8_t gas_resp[]     = {0x0B, 0x00};
    const uint8_t truncated_pa[] = {0x09, 0x50, 0x6F, 0x9A, 0x1A, 0x01};

    EXPECT_FALSE(is_uplink_only_frame(auth_req, sizeof(auth_req)));
    EXPECT_FALSE(is_uplink_only_frame(gas_resp, sizeof(gas_resp)));
    EXPECT_FALSE(is_uplink_only_frame(truncated_pa, sizeof(truncated_pa)));
    EXPECT_FALSE(is_uplink_only_frame(nullptr, 0));
}

} // namespace