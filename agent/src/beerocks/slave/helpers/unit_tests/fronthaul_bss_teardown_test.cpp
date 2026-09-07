/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "../fronthaul_bss_teardown.h"

#include <gtest/gtest.h>

using beerocks::FronthaulBssTeardown;
using namespace std::chrono;

TEST(FronthaulBssTeardown, WaitsForEveryApManager)
{
    FronthaulBssTeardown teardown;
    const auto now = FronthaulBssTeardown::Clock::time_point{};
    teardown.start(now + seconds(30));
    teardown.add(10, "wlan0");
    teardown.add(20, "wlan1");

    EXPECT_FALSE(teardown.ready(now));
    EXPECT_TRUE(teardown.complete(20, teardown.request_id()));
    EXPECT_FALSE(teardown.ready(now + seconds(1)));
    EXPECT_TRUE(teardown.complete(10, teardown.request_id()));
    EXPECT_TRUE(teardown.ready(now + seconds(2)));
}

TEST(FronthaulBssTeardown, MissingResponseOnlyAllowsResetAtDeadline)
{
    FronthaulBssTeardown teardown;
    const auto now = FronthaulBssTeardown::Clock::time_point{};
    teardown.start(now + seconds(30));
    teardown.add(10, "wlan0");
    teardown.add(20, "wlan1");
    teardown.complete(10, teardown.request_id());

    EXPECT_FALSE(teardown.ready(now + seconds(30) - milliseconds(1)));
    EXPECT_TRUE(teardown.ready(now + seconds(30)));
    ASSERT_EQ(teardown.pending().size(), 1U);
    EXPECT_EQ(teardown.pending().at(20), "wlan1");
}

TEST(FronthaulBssTeardown, IgnoresUnrelatedAndDuplicateResponses)
{
    FronthaulBssTeardown teardown;
    const auto now = FronthaulBssTeardown::Clock::time_point{};
    teardown.start(now + seconds(30));
    teardown.add(10, "wlan0");
    teardown.add(20, "wlan1");

    EXPECT_FALSE(teardown.complete(10, 0));
    EXPECT_FALSE(teardown.complete(30, teardown.request_id()));
    EXPECT_TRUE(teardown.complete(10, teardown.request_id()));
    EXPECT_FALSE(teardown.complete(10, teardown.request_id()));
    EXPECT_FALSE(teardown.ready(now));
}

TEST(FronthaulBssTeardown, LateResponseCannotCompleteNewTeardownOnReusedSocket)
{
    FronthaulBssTeardown teardown;
    const auto now = FronthaulBssTeardown::Clock::time_point{};
    teardown.start(now + seconds(30));
    teardown.add(10, "wlan0");
    const auto old_id = teardown.request_id();
    ASSERT_TRUE(teardown.ready(now + seconds(30)));
    teardown.reset();
    EXPECT_FALSE(teardown.complete(10, old_id));

    teardown.start(now + seconds(60));
    teardown.add(10, "wlan0");
    EXPECT_FALSE(teardown.complete(10, old_id));
    EXPECT_FALSE(teardown.ready(now + seconds(31)));
    EXPECT_TRUE(teardown.complete(10, teardown.request_id()));
    EXPECT_TRUE(teardown.ready(now + seconds(31)));
}

TEST(FronthaulBssTeardown, DisconnectDoesNotCancelOtherRadios)
{
    FronthaulBssTeardown teardown;
    const auto now = FronthaulBssTeardown::Clock::time_point{};
    teardown.start(now + seconds(30));
    teardown.add(10, "wlan0");
    teardown.add(20, "wlan1");

    EXPECT_FALSE(teardown.disconnected(30));
    EXPECT_TRUE(teardown.disconnected(10));
    EXPECT_FALSE(teardown.ready(now));
    EXPECT_TRUE(teardown.complete(20, teardown.request_id()));
    EXPECT_TRUE(teardown.ready(now));
}

TEST(FronthaulBssTeardown, RepeatedStartDoesNotExtendDeadlineOrForgetRequests)
{
    FronthaulBssTeardown teardown;
    const auto now = FronthaulBssTeardown::Clock::time_point{};
    teardown.start(now + seconds(30));
    teardown.add(10, "wlan0");
    const auto id = teardown.request_id();

    teardown.start(now + seconds(50));
    EXPECT_EQ(teardown.request_id(), id);
    EXPECT_FALSE(teardown.ready(now + seconds(29)));
    EXPECT_TRUE(teardown.ready(now + seconds(30)));
    EXPECT_EQ(teardown.pending().size(), 1U);
}

TEST(FronthaulBssTeardown, NoSentRequestsDoesNotDelayReset)
{
    FronthaulBssTeardown teardown;
    const auto now = FronthaulBssTeardown::Clock::time_point{};
    EXPECT_FALSE(teardown.ready(now));
    teardown.start(now + seconds(30));
    EXPECT_TRUE(teardown.ready(now));
    teardown.reset();
    EXPECT_FALSE(teardown.active());
    EXPECT_FALSE(teardown.ready(now + seconds(60)));
}
