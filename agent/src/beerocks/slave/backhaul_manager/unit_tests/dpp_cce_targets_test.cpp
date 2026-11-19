/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../dpp_cce_targets.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

using namespace beerocks::dpp_enrollee;
using beerocks::eFreqType;
using Dfs   = beerocks_message::eDfsState;
using Clock = std::chrono::steady_clock;
using std::chrono::seconds;

namespace {

const Clock::time_point k_t0 = Clock::time_point() + std::chrono::hours(1);

const std::vector<int> k_cooldown = {600, 1200, 1800}; // fallback steps
const std::vector<int> k_backoff  = {120, 240, 480};   // target backoff steps
const seconds k_fresh(120);
const size_t k_max_freqs = 8;

// 52 USABLE and 100 AVAILABLE are DFS; 64 is not listed.
const ChannelDfsMap k_5g = {
    {36, Dfs::NOT_DFS},  {40, Dfs::NOT_DFS},    {44, Dfs::NOT_DFS},  {48, Dfs::NOT_DFS},
    {52, Dfs::USABLE},   {100, Dfs::AVAILABLE}, {149, Dfs::NOT_DFS}, {153, Dfs::NOT_DFS},
    {157, Dfs::NOT_DFS}, {161, Dfs::NOT_DFS},   {165, Dfs::NOT_DFS},
};

sCceScanEntry cce(uint8_t channel, int32_t seen_ms_ago, eFreqType band = eFreqType::FREQ_UNKNOWN)
{
    sCceScanEntry entry;
    entry.bssid       = "02:00:00:00:00:" + std::to_string(channel);
    entry.channel     = channel;
    entry.band        = band;
    entry.cce_known   = true;
    entry.cce_present = true;
    entry.seen_ms_ago = seen_ms_ago;
    return entry;
}

sCceTarget update(sCceTargetState &state, const std::vector<sCceScanEntry> &entries,
                  Clock::time_point now, size_t max_freqs = k_max_freqs)
{
    return cce_target_update(state, entries, eFreqType::FREQ_5G, k_5g, now, k_fresh, max_freqs);
}

} // namespace

TEST(DppCceChannelToFreq, BandsAndLimits)
{
    EXPECT_EQ(channel_to_freq(1, eFreqType::FREQ_24G), 2412u);
    EXPECT_EQ(channel_to_freq(13, eFreqType::FREQ_24G), 2472u);
    EXPECT_EQ(channel_to_freq(14, eFreqType::FREQ_24G), 2484u);
    EXPECT_EQ(channel_to_freq(0, eFreqType::FREQ_24G), 0u);
    EXPECT_EQ(channel_to_freq(36, eFreqType::FREQ_24G), 0u);
    EXPECT_EQ(channel_to_freq(36, eFreqType::FREQ_5G), 5180u);
    EXPECT_EQ(channel_to_freq(165, eFreqType::FREQ_5G), 5825u);
    EXPECT_EQ(channel_to_freq(177, eFreqType::FREQ_58G), 5885u);
    EXPECT_EQ(channel_to_freq(6, eFreqType::FREQ_5G), 0u);
    EXPECT_EQ(channel_to_freq(2, eFreqType::FREQ_6G), 5935u);
    EXPECT_EQ(channel_to_freq(1, eFreqType::FREQ_6G), 5955u);
    EXPECT_EQ(channel_to_freq(233, eFreqType::FREQ_6G), 7115u);
    EXPECT_EQ(channel_to_freq(6, eFreqType::FREQ_UNKNOWN), 0u);
}

TEST(DppCceFreqsString, SpaceSeparated)
{
    EXPECT_EQ(freqs_string({}), "");
    EXPECT_EQ(freqs_string({5180}), "5180");
    EXPECT_EQ(freqs_string({2437, 5180, 5745}), "2437 5180 5745");
}

TEST(DppCceWatchCadence, FastWindowThenSlow)
{
    const seconds fast(60), slow(120), window(600);
    sWatchCadence cadence;
    EXPECT_EQ(watch_interval(cadence, k_t0, fast, slow), slow); // never boosted

    watch_cadence_boost(cadence, k_t0, window); // READY
    EXPECT_EQ(watch_interval(cadence, k_t0, fast, slow), fast);
    EXPECT_EQ(watch_interval(cadence, k_t0 + seconds(599), fast, slow), fast);
    EXPECT_EQ(watch_interval(cadence, k_t0 + seconds(600), fast, slow), slow);

    watch_cadence_boost(cadence, k_t0 + seconds(700), window); // rising edge
    EXPECT_EQ(watch_interval(cadence, k_t0 + seconds(1299), fast, slow), fast);
    EXPECT_EQ(watch_interval(cadence, k_t0 + seconds(1300), fast, slow), slow);
}

TEST(DppCceWatchCadence, BoostNeverShortensWindow)
{
    sWatchCadence cadence;
    watch_cadence_boost(cadence, k_t0, seconds(600));
    watch_cadence_boost(cadence, k_t0 + seconds(10), seconds(60));
    EXPECT_EQ(cadence.fast_until, k_t0 + seconds(600));
}
TEST(DppCceWatchCadence, FirstScanReadAfterScanTime)
{
    // First scan (none pending): read after the scan time.
    EXPECT_EQ(watch_next_read(false, true, seconds(60), seconds(10)), seconds(10));
    // A scan of the last round is pending: full interval, fast or slow.
    EXPECT_EQ(watch_next_read(true, true, seconds(60), seconds(10)), seconds(60));
    EXPECT_EQ(watch_next_read(true, true, seconds(120), seconds(10)), seconds(120));
    // No scan started: nothing to read early.
    EXPECT_EQ(watch_next_read(false, false, seconds(60), seconds(10)), seconds(60));
    EXPECT_EQ(watch_next_read(true, false, seconds(60), seconds(10)), seconds(60));
    // Never later than the interval.
    EXPECT_EQ(watch_next_read(false, true, seconds(5), seconds(10)), seconds(5));
}
TEST(DppCceFallbackTimer, FirstAfterReadyThenCooldownSteps)
{
    sFallbackTimer timer;
    EXPECT_FALSE(fallback_timer_due(timer, k_t0 + seconds(100000))); // not armed

    fallback_timer_arm(timer, k_t0, seconds(180));
    EXPECT_FALSE(fallback_timer_due(timer, k_t0 + seconds(179)));
    EXPECT_TRUE(fallback_timer_due(timer, k_t0 + seconds(180)));

    auto now = k_t0 + seconds(200);
    fallback_timer_pass_failed(timer, now, k_cooldown, seconds(0));
    EXPECT_EQ(timer.due, now + seconds(600));
    EXPECT_FALSE(fallback_timer_due(timer, now + seconds(599)));

    now += seconds(600);
    fallback_timer_pass_failed(timer, now, k_cooldown, seconds(30));
    EXPECT_EQ(timer.due, now + seconds(1230));

    now += seconds(1230);
    fallback_timer_pass_failed(timer, now, k_cooldown, seconds(60));
    EXPECT_EQ(timer.due, now + seconds(1860));

    now += seconds(1860);
    fallback_timer_pass_failed(timer, now, k_cooldown, seconds(-5)); // last step repeats
    EXPECT_EQ(timer.due, now + seconds(1800));
    EXPECT_EQ(timer.failed, 4u);

    fallback_timer_arm(timer, now, seconds(180)); // new READY resets the count
    EXPECT_EQ(timer.failed, 0u);
    EXPECT_EQ(timer.due, now + seconds(180));
}

TEST(DppCceTarget, ReasonNames)
{
    EXPECT_STREQ(to_string(eCceTargetReason::NONE), "none");
    EXPECT_STREQ(to_string(eCceTargetReason::FRESH), "FRESH");
    EXPECT_STREQ(to_string(eCceTargetReason::RISING), "RISING");
}

TEST(DppCceTarget, FirstReadIsFreshNotRising)
{
    sCceTargetState state;
    const auto target = update(state, {cce(149, 5000), cce(36, 1000)}, k_t0);
    EXPECT_EQ(target.reason, eCceTargetReason::FRESH);
    EXPECT_TRUE(target.ready);
    EXPECT_EQ(target.freqs, (std::vector<uint32_t>{5180, 5745}));
    EXPECT_EQ(target.bssids, 2u);
    EXPECT_EQ(target.unsafe, 0u);
    EXPECT_EQ(target.dropped, 0u);
    EXPECT_TRUE(state.baseline);
}

TEST(DppCceTarget, NoCceIsNone)
{
    sCceTargetState state;
    auto absent        = cce(36, 1000);
    absent.cce_present = false;
    auto unknown       = cce(40, 1000);
    unknown.cce_known  = false; // no metadata, not counted
    const auto target  = update(state, {absent, unknown}, k_t0);
    EXPECT_EQ(target.reason, eCceTargetReason::NONE);
    EXPECT_FALSE(target.ready);
    EXPECT_TRUE(target.freqs.empty());
    EXPECT_EQ(target.bssids, 0u);
    EXPECT_EQ(target.unsafe, 0u);
}
TEST(DppCceTarget, NewFrequencyIsRisingAndClearsBackoff)
{
    sCceTargetState state;
    EXPECT_TRUE(update(state, {cce(36, 1000)}, k_t0).ready);
    cce_target_failed(state, k_t0, k_backoff);

    auto target = update(state, {cce(36, 1000)}, k_t0 + seconds(10));
    EXPECT_EQ(target.reason, eCceTargetReason::FRESH);
    EXPECT_FALSE(target.ready);
    EXPECT_EQ(target.next_allowed, k_t0 + seconds(120));

    target = update(state, {cce(36, 1000), cce(149, 1000)}, k_t0 + seconds(20));
    EXPECT_EQ(target.reason, eCceTargetReason::RISING);
    EXPECT_TRUE(target.ready);
    EXPECT_EQ(state.backoff_step, 0u);

    target = update(state, {cce(36, 1000), cce(149, 1000)}, k_t0 + seconds(30));
    EXPECT_EQ(target.reason, eCceTargetReason::FRESH);
}

TEST(DppCceTarget, LostThenBackIsRising)
{
    sCceTargetState state;
    update(state, {cce(36, 1000)}, k_t0);
    EXPECT_EQ(update(state, {}, k_t0 + seconds(60)).reason, eCceTargetReason::NONE);
    EXPECT_EQ(update(state, {cce(36, 1000)}, k_t0 + seconds(120)).reason, eCceTargetReason::RISING);
}

TEST(DppCceTarget, SteadyCceBacksOff120To480)
{
    sCceTargetState state;
    auto now = k_t0;
    for (const int step : {120, 240, 480, 480}) {
        EXPECT_TRUE(update(state, {cce(36, 1000)}, now).ready);
        cce_target_failed(state, now, k_backoff);
        EXPECT_FALSE(update(state, {cce(36, 1000)}, now + seconds(step - 1)).ready);
        now += seconds(step);
    }
    EXPECT_EQ(state.backoff_step, 4u);
}

TEST(DppCceTarget, UnsafeEntriesCountedNeverScoped)
{
    sCceTargetState state;
    const auto target = update(state,
                               {cce(52, 1000), cce(100, -1), cce(64, 1000),
                                cce(6, 1000, eFreqType::FREQ_24G), cce(0, 1000)},
                               k_t0);
    EXPECT_EQ(target.reason, eCceTargetReason::NONE);
    EXPECT_TRUE(target.freqs.empty());
    EXPECT_EQ(target.unsafe, 5u);
    EXPECT_EQ(target.bssids, 0u);
}
TEST(DppCceTarget, FreshnessAndUnknownAge)
{
    sCceTargetState state;
    const auto target = update(
        state, {cce(36, 120000), cce(40, 120001), cce(44, -1), cce(149, 1, eFreqType::FREQ_58G)},
        k_t0);
    EXPECT_EQ(target.freqs, (std::vector<uint32_t>{5180, 5745}));
    EXPECT_EQ(target.bssids, 2u);
    EXPECT_EQ(target.unsafe, 0u);
}

TEST(DppCceTarget, CapKeepsYoungestFrequencies)
{
    sCceTargetState state;
    const auto target =
        update(state,
               {cce(36, 9000), cce(40, 1000), cce(44, 2000), cce(48, 3000), cce(149, 4000),
                cce(153, 5000), cce(157, 6000), cce(161, 7000), cce(165, 8000), cce(40, 9500)},
               k_t0);
    EXPECT_EQ(target.freqs,
              (std::vector<uint32_t>{5200, 5220, 5240, 5745, 5765, 5785, 5805, 5825}));
    EXPECT_EQ(target.dropped, 1u);
    EXPECT_EQ(target.bssids, 10u);

    sCceTargetState tie;
    const auto one = update(tie, {cce(40, 500), cce(36, 500)}, k_t0, 1);
    EXPECT_EQ(one.freqs, (std::vector<uint32_t>{5180}));
    EXPECT_EQ(one.dropped, 1u);
}
