/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../dhcp_task_state.h"
#include <gtest/gtest.h>
#include <tlvf/tlvftypes.h>

namespace {
using Schedule = son::DhcpPollSchedule;
using Seconds  = std::chrono::seconds;
const Schedule::Time start{};

TEST(DhcpSchedule, StartupAndDelayedAcquisitionUseFastPollsThenNormalInterval)
{
    Schedule schedule(Seconds(300), start);
    EXPECT_FALSE(schedule.ready(start + Seconds(4)));
    for (int second = 5; second <= 60; second += 5) {
        const auto now = start + Seconds(second);
        ASSERT_TRUE(schedule.ready(now));
        schedule.completed(now, true);
    }
    EXPECT_FALSE(schedule.ready(start + Seconds(359)));
    EXPECT_TRUE(schedule.ready(start + Seconds(360)));
}

TEST(DhcpSchedule, ConnectionBurstNeverPostponesAnAlreadyScheduledPoll)
{
    Schedule schedule(Seconds(300), start);
    for (int i = 0; i < 200; ++i) {
        schedule.station_connected(start + std::chrono::milliseconds(i * 20));
    }
    EXPECT_TRUE(schedule.ready(start + Seconds(5)));
    schedule.completed(start + Seconds(5), true);
    EXPECT_TRUE(schedule.ready(start + Seconds(10)));
}

TEST(DhcpSchedule, ConnectionDuringIdleAdvancesNextPoll)
{
    Schedule schedule(Seconds(300), start);
    schedule.completed(start + Seconds(60), true);
    schedule.station_connected(start + Seconds(100));
    EXPECT_FALSE(schedule.ready(start + Seconds(104)));
    EXPECT_TRUE(schedule.ready(start + Seconds(105)));
}

TEST(DhcpSchedule, FailedInitializationAndProviderReadsBackOffAndRecover)
{
    Schedule schedule(Seconds(300), start);
    auto now = start + Seconds(5);
    for (int delay : {5, 10, 20, 40, 60, 60}) {
        ASSERT_TRUE(schedule.ready(now));
        schedule.completed(now, false);
        EXPECT_FALSE(schedule.ready(now + Seconds(delay - 1)));
        now += Seconds(delay);
    }
    ASSERT_TRUE(schedule.ready(now));
    schedule.completed(now, true);
    EXPECT_FALSE(schedule.ready(now + Seconds(299)));
    EXPECT_TRUE(schedule.ready(now + Seconds(300)));
}

TEST(DhcpSchedule, RetryNeverExceedsConfiguredPollInterval)
{
    Schedule schedule(Seconds(2), start);
    schedule.completed(start, false);
    EXPECT_TRUE(schedule.ready(start + Seconds(2)));
    schedule.completed(start + Seconds(2), false);
    EXPECT_TRUE(schedule.ready(start + Seconds(4)));
}

class DhcpTrackerTest : public ::testing::Test {
protected:
    son::DhcpLeaseTracker tracker;
    son::DhcpLeaseTracker::MacSet present;
    std::unordered_map<sMacAddr, std::string> current;
    const sMacAddr mac = tlvf::mac_from_string("04:11:19:20:d4:02");

    std::vector<sMacAddr> removed(bool complete)
    {
        return tracker.removed(present, complete, [this](const sMacAddr &address) {
            const auto found = current.find(address);
            return found == current.end() ? std::string{} : found->second;
        });
    }

    void SetUp() override
    {
        current[mac] = "192.168.1.2";
        tracker.remember(mac, current.at(mac));
    }
};

TEST_F(DhcpTrackerTest, FailedFamilyReadPreservesAddressUntilCompleteRead)
{
    EXPECT_TRUE(removed(false).empty());
    const auto missing = removed(true);
    ASSERT_EQ(1U, missing.size());
    EXPECT_EQ(mac, missing.front());
    tracker.cleared(mac, true);
    EXPECT_TRUE(removed(true).empty());
}

TEST_F(DhcpTrackerTest, PresentLeaseIsRetainedAndRenewalReplacesTrackedAddress)
{
    present.insert(mac);
    EXPECT_TRUE(removed(true).empty());
    current[mac] = "192.168.1.3";
    tracker.remember(mac, current.at(mac));
    present.clear();
    EXPECT_EQ(1U, removed(true).size());
}

TEST_F(DhcpTrackerTest, AddressesSuppliedByAnotherProducerAreNotCleared)
{
    current[mac] = "192.168.1.99";
    EXPECT_TRUE(removed(true).empty());
    current[mac] = "192.168.1.2";
    EXPECT_TRUE(removed(true).empty());
}

TEST_F(DhcpTrackerTest, DeletedStationsArePruned)
{
    current.erase(mac);
    EXPECT_TRUE(removed(true).empty());
}

TEST_F(DhcpTrackerTest, FailedDmClearIsRetriedAfterCachedValueBecomesUnknown)
{
    ASSERT_EQ(1U, removed(true).size());
    current[mac] = "0";
    tracker.cleared(mac, false);
    EXPECT_TRUE(removed(false).empty());
    EXPECT_EQ(1U, removed(true).size());
    tracker.cleared(mac, true);
    EXPECT_TRUE(removed(true).empty());
}
} // namespace
