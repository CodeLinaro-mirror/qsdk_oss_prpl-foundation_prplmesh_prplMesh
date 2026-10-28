/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "../agent_db.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <vector>

using beerocks::AgentDB;

namespace {

constexpr size_t kMaxSize = ASSOCIATION_FRAME_SIZE;

std::vector<uint8_t> make_frame(size_t length)
{
    std::vector<uint8_t> frame(length);
    // A counting pattern: a copy that is shifted or cut at the wrong place does not match.
    std::iota(frame.begin(), frame.end(), uint8_t{1});
    return frame;
}

AgentDB::sRadio::sClient make_client(std::vector<uint8_t> &frame, size_t length)
{
    return AgentDB::sRadio::sClient(sMacAddr{}, length, frame.empty() ? nullptr : frame.data(),
                                    /* btm_supported_ */ 0);
}

} // namespace

TEST(AgentDbClient, FrameThatFitsIsStoredWhole)
{
    auto frame  = make_frame(100);
    auto client = make_client(frame, frame.size());

    EXPECT_EQ(client.association_frame_length, frame.size());
    EXPECT_EQ(client.association_frame, frame);
}

TEST(AgentDbClient, FrameOfMaximumSizeIsStoredWhole)
{
    auto frame  = make_frame(kMaxSize);
    auto client = make_client(frame, frame.size());

    EXPECT_EQ(client.association_frame_length, kMaxSize);
    EXPECT_EQ(client.association_frame, frame);
}

TEST(AgentDbClient, FrameOneByteOverTheMaximumIsTruncated)
{
    auto frame  = make_frame(kMaxSize + 1);
    auto client = make_client(frame, frame.size());

    EXPECT_EQ(client.association_frame_length, kMaxSize);
    ASSERT_EQ(client.association_frame.size(), kMaxSize);
    EXPECT_TRUE(std::equal(client.association_frame.begin(), client.association_frame.end(),
                           frame.begin()));
}

TEST(AgentDbClient, FarOversizedFrameIsTruncated)
{
    auto frame  = make_frame(kMaxSize * 4);
    auto client = make_client(frame, frame.size());

    EXPECT_EQ(client.association_frame_length, kMaxSize);
    EXPECT_EQ(client.association_frame.size(), kMaxSize);
}

TEST(AgentDbClient, NullFrameIsStoredEmpty)
{
    // The length is not zero, so a missing null check would read from a null pointer.
    AgentDB::sRadio::sClient client(sMacAddr{}, 100, nullptr, 0);

    EXPECT_EQ(client.association_frame_length, 0u);
    EXPECT_TRUE(client.association_frame.empty());
}

TEST(AgentDbClient, ZeroLengthFrameIsStoredEmpty)
{
    auto frame = make_frame(100);
    AgentDB::sRadio::sClient client(sMacAddr{}, 0, frame.data(), 0);

    EXPECT_EQ(client.association_frame_length, 0u);
    EXPECT_TRUE(client.association_frame.empty());
}

// The truncation warning is not checked here: it goes to the easylogging sink. To assert it,
// add a logging dispatch callback that records the messages.
