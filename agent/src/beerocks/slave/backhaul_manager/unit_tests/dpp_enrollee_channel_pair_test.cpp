/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "../dpp_enrollee_channel_pair.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

using namespace beerocks::dpp_enrollee;
using beerocks::eFreqType;
using Dfs = beerocks_message::eDfsState;

namespace {

struct sCase {
    const char *name;
    eFreqType band;
    ChannelDfsMap channels;
    const char *chan_list; // "" = NO_LIST
    size_t count;
};

const ChannelDfsMap k_24g_full = {{1, Dfs::NOT_DFS}, {6, Dfs::NOT_DFS}, {11, Dfs::NOT_DFS}};
const ChannelDfsMap k_5g_full  = {
    {36, Dfs::NOT_DFS},  {40, Dfs::NOT_DFS},    {44, Dfs::NOT_DFS},  {48, Dfs::NOT_DFS},
    {52, Dfs::USABLE},   {100, Dfs::AVAILABLE}, {144, Dfs::USABLE},  {149, Dfs::NOT_DFS},
    {153, Dfs::NOT_DFS}, {157, Dfs::NOT_DFS},   {161, Dfs::NOT_DFS}, {165, Dfs::NOT_DFS},
    {169, Dfs::NOT_DFS}, {173, Dfs::NOT_DFS},   {177, Dfs::NOT_DFS}};
const char *k_5g_full_list = "115/36,40,44,48,124/149,153,157,161,125/165,169,173,177";

} // namespace
TEST(DppEnrolleeChannelList, SelectionTable)
{
    const std::vector<sCase> cases = {
        {"24 empty", eFreqType::FREQ_24G, {}, "81/6", 1},
        {"24 full", eFreqType::FREQ_24G, k_24g_full, "81/1,6,11", 3},
        {"24 only 13", eFreqType::FREQ_24G, {{13, Dfs::NOT_DFS}}, "81/13", 1},
        {"24 no 1-13", eFreqType::FREQ_24G, {{14, Dfs::NOT_DFS}}, "", 0},
        {"5 empty", eFreqType::FREQ_5G, {}, "", 0},
        {"5 full", eFreqType::FREQ_5G, k_5g_full, k_5g_full_list, 12},
        {"5 low", eFreqType::FREQ_5G, {{36, Dfs::NOT_DFS}, {44, Dfs::NOT_DFS}}, "115/36,44", 2},
        {"5 high", eFreqType::FREQ_5G, {{157, Dfs::NOT_DFS}, {149, Dfs::NOT_DFS}}, "124/149,157", 2},
        {"5 165 only", eFreqType::FREQ_5G, {{165, Dfs::NOT_DFS}}, "125/165", 1},
        {"5 DFS on 149", eFreqType::FREQ_5G, {{149, Dfs::USABLE}, {44, Dfs::NOT_DFS}}, "115/44", 1},
        {"5 radar 44", eFreqType::FREQ_5G, {{44, Dfs::UNAVAILABLE}, {153, Dfs::NOT_DFS}}, "124/153", 1},
        {"5 DFS only", eFreqType::FREQ_5G, {{52, Dfs::USABLE}, {100, Dfs::AVAILABLE}}, "", 0},
        {"5 UNII-4 only", eFreqType::FREQ_5G, {{169, Dfs::NOT_DFS}, {173, Dfs::NOT_DFS}}, "125/169,173", 2},
        {"5.8", eFreqType::FREQ_58G, {{149, Dfs::NOT_DFS}, {165, Dfs::NOT_DFS}}, "124/149,125/165", 2},
        {"6 uses 5 GHz numbers", eFreqType::FREQ_6G, {{5, Dfs::NOT_DFS}, {149, Dfs::NOT_DFS}}, "", 0},
        {"6 empty", eFreqType::FREQ_6G, {}, "", 0},
        {"dual band", eFreqType::FREQ_24G_5G, k_5g_full, "", 0},
        {"unknown", eFreqType::FREQ_UNKNOWN, k_24g_full, "", 0},
        {"disabled", eFreqType::FREQ_DISABLED, k_24g_full, "", 0},
    };
    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        const auto result   = select_bootstrap_channel_list(c.band, c.channels);
        const bool has_list = std::string(c.chan_list) != "";
        EXPECT_EQ(result.decision, has_list ? eListDecision::LIST : eListDecision::NO_LIST)
            << to_string(result.decision);
        EXPECT_EQ(result.chan_list, c.chan_list);
        EXPECT_EQ(result.channel_count, c.count);
        EXPECT_EQ(count_chan_list_channels(result.chan_list), c.count);
        EXPECT_NE(std::string(result.reason), "");
    }
}
TEST(DppEnrolleeChannelList, EachEndPointUsesOnlyItsOwnRadio)
{
    // Box layout: wlan2 = 2.4 GHz, wlan1 = 5 GHz, wlan0 = 6 GHz.
    const ChannelDfsMap radio_24 = {{1, Dfs::NOT_DFS}, {11, Dfs::NOT_DFS}};
    const ChannelDfsMap radio_6  = {{1, Dfs::NOT_DFS}, {5, Dfs::NOT_DFS}, {149, Dfs::NOT_DFS}};

    EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_24G, radio_24).chan_list, "81/1,11");
    EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_5G, k_5g_full).chan_list,
              k_5g_full_list);
    // 149 here is a 6 GHz channel.
    EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_6G, radio_6).decision,
              eListDecision::NO_LIST);
}

TEST(DppEnrolleeChannelList, SameInputGivesSameOutput)
{
    const auto first = select_bootstrap_channel_list(eFreqType::FREQ_5G, k_5g_full);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_5G, k_5g_full).chan_list,
                  first.chan_list);
    }
}

TEST(DppEnrolleeChannelList, NeverListsDfsChannels)
{
    ChannelDfsMap all_dfs;
    for (const uint8_t ch : {36, 40, 44, 48, 52, 100, 144, 149, 153, 157, 161, 165}) {
        all_dfs[ch] = Dfs::AVAILABLE;
    }
    EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_5G, all_dfs).decision,
              eListDecision::NO_LIST);
}

TEST(DppEnrolleeChannelList, BuildsDfsMapFromRadioChannelList)
{
    struct sFakeChannelInfo {
        Dfs dfs_state;
    };
    const std::unordered_map<uint8_t, sFakeChannelInfo> channels_list = {
        {44, {Dfs::NOT_DFS}}, {52, {Dfs::USABLE}}, {149, {Dfs::NOT_DFS}}};
    const auto map = make_channel_dfs_map(channels_list);
    ASSERT_EQ(map.size(), 3u);
    EXPECT_EQ(map.at(52), Dfs::USABLE);
    EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_5G, map).chan_list, "115/44,124/149");
}

TEST(DppEnrolleeChannelList, NoListMeansEmptyArgument)
{
    const auto result = select_bootstrap_channel_list(eFreqType::FREQ_6G, {});
    EXPECT_TRUE(result.chan_list.empty());
    EXPECT_EQ(result.channel_count, 0u);
    EXPECT_STREQ(to_string(result.decision), "NO_LIST");
    EXPECT_EQ(count_chan_list_channels(""), 0u);
    EXPECT_EQ(count_chan_list_channels("81/6"), 1u);
}

TEST(DppEnrolleeChannelList, IncludesUnii4WhenNonDfs)
{
    const ChannelDfsMap unii4 = {
        {165, Dfs::NOT_DFS}, {169, Dfs::NOT_DFS}, {173, Dfs::NOT_DFS}, {177, Dfs::NOT_DFS}};
    const auto result = select_bootstrap_channel_list(eFreqType::FREQ_5G, unii4);
    EXPECT_EQ(result.chan_list, "125/165,169,173,177");
    EXPECT_EQ(result.channel_count, 4u);
}

TEST(DppEnrolleeChannelList, SkipsUnii4WhenNotNonDfs)
{
    const ChannelDfsMap unii4 = {{169, Dfs::USABLE}, {173, Dfs::AVAILABLE}};
    EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_5G, unii4).decision,
              eListDecision::NO_LIST);
}

TEST(DppEnrolleeChannelList, ListsOnlyExactClassMembers)
{
    // No 20 MHz member of 115/124/125; 52-144 stay out even when labelled NOT_DFS.
    ChannelDfsMap channels;
    for (const uint8_t ch : {37, 38, 52, 100, 144, 151, 166}) {
        channels[ch] = Dfs::NOT_DFS;
    }
    EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_5G, channels).decision,
              eListDecision::NO_LIST);
    channels[40]  = Dfs::NOT_DFS;
    channels[153] = Dfs::NOT_DFS;
    EXPECT_EQ(select_bootstrap_channel_list(eFreqType::FREQ_5G, channels).chan_list,
              "115/40,124/153");
}
TEST(DppEnrolleeChannelList, TwoFourOnlyClass81)
{
    const ChannelDfsMap channels = {
        {0, Dfs::NOT_DFS}, {1, Dfs::NOT_DFS}, {13, Dfs::NOT_DFS}, {14, Dfs::NOT_DFS}};
    const auto result = select_bootstrap_channel_list(eFreqType::FREQ_24G, channels);
    EXPECT_EQ(result.chan_list, "81/1,13");
    EXPECT_EQ(result.channel_count, 2u);
}

TEST(DppEnrolleeChannelList, FullInventoryStaysWithinLimits)
{
    // hostap ignores a URI channel list of more than 30 channels; pWHM chanStr is 256 bytes.
    ChannelDfsMap radio_24;
    for (int ch = 1; ch <= 14; ++ch) {
        radio_24[static_cast<uint8_t>(ch)] = Dfs::NOT_DFS;
    }
    ChannelDfsMap radio_5;
    for (int ch = 32; ch <= 144; ch += 4) {
        radio_5[static_cast<uint8_t>(ch)] = Dfs::NOT_DFS;
    }
    for (int ch = 149; ch <= 177; ch += 4) {
        radio_5[static_cast<uint8_t>(ch)] = Dfs::NOT_DFS;
    }
    const auto r24 = select_bootstrap_channel_list(eFreqType::FREQ_24G, radio_24);
    const auto r5  = select_bootstrap_channel_list(eFreqType::FREQ_5G, radio_5);
    EXPECT_EQ(r24.chan_list, "81/1,2,3,4,5,6,7,8,9,10,11,12,13");
    EXPECT_EQ(r24.channel_count, 13u);
    EXPECT_EQ(r5.chan_list, "115/36,40,44,48,124/149,153,157,161,125/165,169,173,177");
    EXPECT_EQ(r5.channel_count, 12u);
    for (const auto &r : {r24, r5}) {
        EXPECT_LE(r.channel_count, 30u);
        EXPECT_LT(r.chan_list.size(), 256u);
    }
}
namespace {
using Clock = std::chrono::steady_clock;
using std::chrono::seconds;

// BackhaulManager values: start guard 60 s + 6 s per URI channel, 15 s per chirp, 300 s cap.
const Clock::time_point kStart{};
const Clock::time_point kStartGuard = kStart + seconds(60 + 6 * 9); // 9 URI channels: 114 s
const seconds kProgress(15);
const seconds kMaxPass(300);

Clock::time_point at(int secs) { return kStart + seconds(secs); }
} // namespace

TEST(DppEnrolleeChirpGuard, EarlyChirpKeepsStartGuard)
{
    EXPECT_EQ(extend_chirp_pass_deadline(kStartGuard, kStart, at(10), kProgress, kMaxPass),
              kStartGuard);
}

TEST(DppEnrolleeChirpGuard, LateChirpExtendsGuard)
{
    EXPECT_EQ(extend_chirp_pass_deadline(kStartGuard, kStart, at(104), kProgress, kMaxPass),
              at(119));
}
TEST(DppEnrolleeChirpGuard, CceChannelsAfterUriChannelsKeepPassAlive)
{
    // Box C pass: scan, 9 URI/default channels every 7 s, then 7 DFS channels of APs with a CCE
    // every 10 s. The last chirp (128 s) comes after the start guard (114 s).
    std::vector<int> chirps;
    for (int t = 5; t <= 61; t += 7) {
        chirps.push_back(t);
    }
    for (int t = 68; t <= 128; t += 10) {
        chirps.push_back(t);
    }
    ASSERT_GT(at(chirps.back()), kStartGuard);
    auto guard = kStartGuard;
    for (const int t : chirps) {
        ASSERT_LT(at(t), guard) << "pass ended before the chirp at " << t << " s";
        guard = extend_chirp_pass_deadline(guard, kStart, at(t), kProgress, kMaxPass);
    }
    EXPECT_EQ(guard, at(143));
}

TEST(DppEnrolleeChirpGuard, CapLimitsExtension)
{
    EXPECT_EQ(extend_chirp_pass_deadline(at(295), kStart, at(290), kProgress, kMaxPass), at(300));
    EXPECT_EQ(extend_chirp_pass_deadline(at(300), kStart, at(400), kProgress, kMaxPass), at(300));
}

TEST(DppEnrolleeChirpGuard, NeverShortensGuard)
{
    // A chirp never pulls the guard in; a stalled supplicant still ends at the start guard.
    EXPECT_EQ(extend_chirp_pass_deadline(at(320), kStart, at(10), kProgress, kMaxPass), at(320));
    const auto guard = extend_chirp_pass_deadline(kStartGuard, kStart, at(61), kProgress, kMaxPass);
    EXPECT_EQ(guard, kStartGuard);
}

namespace {
const Clock::time_point kUntil = at(100);
const seconds kHoldGrace(12);
} // namespace

TEST(DppEnrolleeChirpHold, ExchangeHoldsUntilDeadline)
{
    EXPECT_TRUE(chirp_holds_restart(eChirpHoldPhase::EXCHANGE, true, at(99), kUntil));
    EXPECT_FALSE(chirp_holds_restart(eChirpHoldPhase::EXCHANGE, true, at(100), kUntil));
    // As before this change, the exchange hold does not depend on the Enrollee flag.
    EXPECT_TRUE(chirp_holds_restart(eChirpHoldPhase::EXCHANGE, false, at(99), kUntil));
}

TEST(DppEnrolleeChirpHold, PassGapAndWatchHoldOnlyForUnconfiguredEnrollee)
{
    for (const auto phase : {eChirpHoldPhase::PASS, eChirpHoldPhase::GAP, eChirpHoldPhase::WATCH}) {
        SCOPED_TRACE(to_string(phase));
        EXPECT_TRUE(chirp_holds_restart(phase, true, at(0), kUntil));
        EXPECT_TRUE(chirp_holds_restart(phase, true, at(99), kUntil));
        EXPECT_FALSE(chirp_holds_restart(phase, true, at(100), kUntil));
        EXPECT_FALSE(chirp_holds_restart(phase, true, at(101), kUntil));
        // A configured Agent (WPS or a stored profile) restarts as before.
        EXPECT_FALSE(chirp_holds_restart(phase, false, at(0), kUntil));
    }
}

TEST(DppEnrolleeChirpHold, NoneNeverHolds)
{
    EXPECT_FALSE(chirp_holds_restart(eChirpHoldPhase::NONE, true, at(0), kUntil));
    EXPECT_FALSE(chirp_holds_restart(eChirpHoldPhase::NONE, false, at(0), kUntil));
}
TEST(DppEnrolleeChirpHold, UnsetUntilNeverHolds)
{
    // hold_until is cleared for the cycle backoff.
    EXPECT_FALSE(chirp_holds_restart(eChirpHoldPhase::GAP, true, at(1), Clock::time_point{}));
}

TEST(DppEnrolleeChirpHold, WatchHoldEndsAfterRoundPlusGrace)
{
    // Nov-18 lab: WAIT_WPS expired in WATCH between two 60 s rounds and the restart dropped
    // the scan results of the round. The hold covers the next round plus the grace.
    const auto round_deadline = at(60);
    EXPECT_TRUE(
        chirp_holds_restart(eChirpHoldPhase::WATCH, true, at(60), round_deadline + kHoldGrace));
    EXPECT_TRUE(
        chirp_holds_restart(eChirpHoldPhase::WATCH, true, at(71), round_deadline + kHoldGrace));
    EXPECT_FALSE(
        chirp_holds_restart(eChirpHoldPhase::WATCH, true, at(72), round_deadline + kHoldGrace));
}

TEST(DppEnrolleeChirpHold, LimitAfterMaxHold)
{
    const seconds max_hold(1800);
    const auto expired_at = at(120);
    EXPECT_FALSE(chirp_hold_limit_reached(at(120), expired_at, max_hold));
    EXPECT_FALSE(chirp_hold_limit_reached(at(1920), expired_at, max_hold));
    EXPECT_TRUE(chirp_hold_limit_reached(at(1921), expired_at, max_hold));
}

TEST(DppEnrolleeChirpHold, RestartWaitsForRunningPass)
{
    // Box B 2.4 GHz pass: 11 URI channels and one chirp every 7 s. The WAIT_WPS timeout
    // expires mid pass; the pass guard plus grace holds the restart until the pass ends.
    auto guard = kStart + seconds(60 + 6 * 11);
    for (int t = 5; t <= 215; t += 7) {
        guard = extend_chirp_pass_deadline(guard, kStart, at(t), kProgress, kMaxPass);
        EXPECT_TRUE(chirp_holds_restart(eChirpHoldPhase::PASS, true, at(t + 6), guard + kHoldGrace))
            << "t=" << t;
    }
    EXPECT_EQ(guard, at(230));
    EXPECT_TRUE(chirp_holds_restart(eChirpHoldPhase::PASS, true, at(241), guard + kHoldGrace));
    EXPECT_FALSE(chirp_holds_restart(eChirpHoldPhase::PASS, true, at(242), guard + kHoldGrace));
}

TEST(DppEnrolleeChirpHold, PhaseNames)
{
    EXPECT_STREQ(to_string(eChirpHoldPhase::NONE), "none");
    EXPECT_STREQ(to_string(eChirpHoldPhase::PASS), "pass");
    EXPECT_STREQ(to_string(eChirpHoldPhase::GAP), "gap");
    EXPECT_STREQ(to_string(eChirpHoldPhase::EXCHANGE), "exchange");
    EXPECT_STREQ(to_string(eChirpHoldPhase::WATCH), "watch");
}

namespace {
const seconds kBandSwitch(2);
const seconds kPause(30);
using Bands = std::vector<eFreqType>;
using Order = std::vector<size_t>;
} // namespace

TEST(DppEnrolleeChirpOrder, AutoRanksFiveThenTwoFourThenSix)
{
    const auto rank = [](eFreqType band) { return chirp_band_rank(band, eFreqType::FREQ_AUTO); };
    EXPECT_LT(rank(eFreqType::FREQ_5G), rank(eFreqType::FREQ_24G));
    EXPECT_LT(rank(eFreqType::FREQ_24G), rank(eFreqType::FREQ_6G));
    EXPECT_LT(rank(eFreqType::FREQ_6G), rank(eFreqType::FREQ_UNKNOWN));
    EXPECT_EQ(rank(eFreqType::FREQ_58G), rank(eFreqType::FREQ_5G));
}

TEST(DppEnrolleeChirpOrder, PreferredBandChirpsFirst)
{
    // Registration order of Box B: the 2.4 GHz bSTA first.
    const Bands bands = {eFreqType::FREQ_24G, eFreqType::FREQ_5G, eFreqType::FREQ_6G};
    EXPECT_EQ(chirp_band_order(bands, eFreqType::FREQ_AUTO), (Order{1, 0, 2}));
    EXPECT_EQ(chirp_band_order(bands, eFreqType::FREQ_24G), (Order{0, 1, 2}));
    EXPECT_EQ(chirp_band_order(bands, eFreqType::FREQ_6G), (Order{2, 1, 0}));
    // An unknown preference does not name a band and orders like AUTO.
    EXPECT_EQ(chirp_band_order(bands, eFreqType::FREQ_UNKNOWN), (Order{1, 0, 2}));
}

TEST(DppEnrolleeChirpOrder, EqualRanksKeepRegistrationOrder)
{
    const Bands bands = {eFreqType::FREQ_24G, eFreqType::FREQ_5G, eFreqType::FREQ_58G,
                         eFreqType::FREQ_5G};
    EXPECT_EQ(chirp_band_order(bands, eFreqType::FREQ_AUTO), (Order{1, 2, 3, 0}));
    EXPECT_TRUE(chirp_band_order({}, eFreqType::FREQ_AUTO).empty());
}

namespace {
const std::vector<std::string> kAB = {"wlA", "wlB"};
} // namespace

TEST(DppEnrolleeChirpIteration, RefusedStartsWaitForThePause)
{
    // G10: A ends at t=0, B refuses at t=2 and t=12: A chirps again at t=30, not t=22.
    sChirpIteration it;
    chirp_iteration_pass_end(it, "wlA", at(0));
    auto pick = chirp_iteration_pick(it, kAB, "", at(2), kPause);
    ASSERT_TRUE(pick.ready);
    EXPECT_EQ(pick.index, 1u);
    EXPECT_FALSE(chirp_iteration_exhausted(it, kAB));
    chirp_iteration_skip(it, "wlB"); // second refused start at t=12
    EXPECT_TRUE(chirp_iteration_exhausted(it, kAB));
    pick = chirp_iteration_pick(it, kAB, "", at(22), kPause);
    EXPECT_FALSE(pick.ready);
    EXPECT_EQ(pick.not_before, at(30));
    pick = chirp_iteration_pick(it, kAB, "", at(30), kPause);
    ASSERT_TRUE(pick.ready);
    EXPECT_EQ(pick.index, 0u);
    EXPECT_FALSE(chirp_iteration_exhausted(it, kAB));
}

TEST(DppEnrolleeChirpIteration, ExhaustedListPausesAfterTheLastPassEnd)
{
    sChirpIteration it;
    chirp_iteration_pass_end(it, "wlA", at(0));
    EXPECT_EQ(chirp_iteration_pick(it, kAB, "", at(2), kPause).index, 1u);
    chirp_iteration_pass_end(it, "wlB", at(62));
    EXPECT_TRUE(chirp_iteration_exhausted(it, kAB));
    auto pick = chirp_iteration_pick(it, kAB, "", at(64), kPause);
    EXPECT_FALSE(pick.ready);
    EXPECT_EQ(pick.not_before, at(92));
    pick = chirp_iteration_pick(it, kAB, "", at(92), kPause);
    ASSERT_TRUE(pick.ready);
    EXPECT_EQ(pick.index, 0u);
}

TEST(DppEnrolleeChirpIteration, SingleBstaPausesBetweenPasses)
{
    const std::vector<std::string> ifaces = {"wlA"};
    sChirpIteration it;
    EXPECT_TRUE(chirp_iteration_pick(it, ifaces, "", at(0), kPause).ready);
    chirp_iteration_pass_end(it, "wlA", at(60));
    EXPECT_TRUE(chirp_iteration_exhausted(it, ifaces));
    auto pick = chirp_iteration_pick(it, ifaces, "", at(62), kPause);
    EXPECT_FALSE(pick.ready);
    EXPECT_EQ(pick.not_before, at(90));
    EXPECT_TRUE(chirp_iteration_pick(it, ifaces, "", at(90), kPause).ready);
}
TEST(DppEnrolleeChirpIteration, BstaEligibleDuringThePauseWaits)
{
    sChirpIteration it;
    chirp_iteration_pass_end(it, "wlA", at(0));
    // B left after A ended: the shrunk list is exhausted and pauses.
    auto pick = chirp_iteration_pick(it, {"wlA"}, "", at(2), kPause);
    EXPECT_FALSE(pick.ready);
    EXPECT_EQ(pick.not_before, at(30));
    // B is back during the pause: it waits for the pause too.
    pick = chirp_iteration_pick(it, kAB, "", at(5), kPause);
    EXPECT_FALSE(pick.ready);
    EXPECT_EQ(pick.not_before, at(30));
    pick = chirp_iteration_pick(it, kAB, "", at(30), kPause);
    ASSERT_TRUE(pick.ready);
    EXPECT_EQ(pick.index, 0u);
}

TEST(DppEnrolleeChirpIteration, PreferredBstaFirstWhenOpen)
{
    sChirpIteration it;
    EXPECT_EQ(chirp_iteration_pick(it, kAB, "wlB", at(0), kPause).index, 1u);
    EXPECT_EQ(chirp_iteration_pick(it, kAB, "wlX", at(0), kPause).index, 0u);
    // B answered at t=0 and failed: the next iteration starts on B.
    chirp_iteration_pass_end(it, "wlB", at(0));
    EXPECT_EQ(chirp_iteration_pick(it, kAB, "wlB", at(2), kPause).index, 0u);
    auto pick = chirp_iteration_pick(it, kAB, "wlB", at(30), kPause);
    ASSERT_TRUE(pick.ready);
    EXPECT_EQ(pick.index, 1u);
    // The band order may change between iterations; the tracker follows the names.
    const std::vector<std::string> ba = {"wlB", "wlA"};
    chirp_iteration_pass_end(it, "wlB", at(40));
    EXPECT_EQ(chirp_iteration_pick(it, ba, "", at(42), kPause).index, 1u);
}

TEST(DppEnrolleeChirpIteration, RefusedStartsOnlyRetryAllBstas)
{
    // Nothing was sent: no pause, the bSTAs are tried again.
    sChirpIteration it;
    chirp_iteration_skip(it, "wlA");
    chirp_iteration_skip(it, "wlB");
    EXPECT_TRUE(chirp_iteration_exhausted(it, kAB));
    auto pick = chirp_iteration_pick(it, kAB, "", at(20), kPause);
    ASSERT_TRUE(pick.ready);
    EXPECT_EQ(pick.index, 0u);
    EXPECT_FALSE(chirp_iteration_exhausted(it, kAB));
}

TEST(DppEnrolleeChirpIteration, NewIterationAfterALongGap)
{
    sChirpIteration it;
    chirp_iteration_pass_end(it, "wlA", at(0));
    chirp_iteration_skip(it, "wlB");
    auto pick = chirp_iteration_pick(it, kAB, "", at(400), kPause);
    ASSERT_TRUE(pick.ready);
    EXPECT_EQ(pick.index, 0u);
    chirp_iteration_pass_end(it, "wlA", at(460));
    EXPECT_EQ(chirp_iteration_pick(it, kAB, "", at(462), kPause).index, 1u);
    EXPECT_FALSE(chirp_iteration_pick(it, {}, "", at(462), kPause).ready);
}

TEST(DppEnrolleeChirpOrder, GapAfterPass)
{
    // A clean pass inside an iteration switches band; the last band of an iteration pauses.
    for (const auto end : {eChirpPassEnd::COMPLETED, eChirpPassEnd::WATCHDOG}) {
        SCOPED_TRACE(to_string(end));
        EXPECT_EQ(chirp_gap_after_pass(end, false, kBandSwitch, kPause), kBandSwitch);
        EXPECT_EQ(chirp_gap_after_pass(end, true, kBandSwitch, kPause), kPause);
    }
    // EC 6.2.3: the procedure resumes after a failed exchange, after the full pause.
    for (const auto end : {eChirpPassEnd::EXCHANGE_TIMEOUT, eChirpPassEnd::FAILED}) {
        SCOPED_TRACE(to_string(end));
        EXPECT_EQ(chirp_gap_after_pass(end, false, kBandSwitch, kPause), kPause);
        EXPECT_EQ(chirp_gap_after_pass(end, true, kBandSwitch, kPause), kPause);
    }
}

TEST(DppEnrolleeChirpOrder, TwoBandCycleSwitchesBandThenPauses)
{
    // Box B: the 2.4 GHz bSTA registers first, the configurator listens on 5 GHz only.
    const Bands bands                    = {eFreqType::FREQ_24G, eFreqType::FREQ_5G};
    const std::vector<std::string> names = {"wl24", "wl5"};
    const auto order                     = chirp_band_order(bands, eFreqType::FREQ_AUTO);
    std::vector<std::string> ifaces;
    for (const auto index : order) {
        ifaces.push_back(names[index]);
    }
    sChirpIteration it;
    auto now = at(0);
    Bands chirped;
    std::vector<seconds> gaps;
    for (int pass = 0; pass < 4; ++pass) {
        const auto pick = chirp_iteration_pick(it, ifaces, "", now, kPause);
        ASSERT_TRUE(pick.ready);
        chirped.push_back(bands[order[pick.index]]);
        now += seconds(60);
        chirp_iteration_pass_end(it, ifaces[pick.index], now);
        if (pass < 3) { // the backoff follows pass 4
            gaps.push_back(chirp_gap_after_pass(eChirpPassEnd::COMPLETED,
                                                chirp_iteration_exhausted(it, ifaces), kBandSwitch,
                                                kPause));
            now += gaps.back();
        }
    }
    EXPECT_EQ(chirped, (Bands{eFreqType::FREQ_5G, eFreqType::FREQ_24G, eFreqType::FREQ_5G,
                              eFreqType::FREQ_24G}));
    EXPECT_EQ(gaps, (std::vector<seconds>{kBandSwitch, kPause, kBandSwitch}));
}

TEST(DppEnrolleeChirpOrder, PassEndNames)
{
    // The "pass end ... reason=" log values are unchanged.
    EXPECT_STREQ(to_string(eChirpPassEnd::COMPLETED), "completed");
    EXPECT_STREQ(to_string(eChirpPassEnd::WATCHDOG), "watchdog");
    EXPECT_STREQ(to_string(eChirpPassEnd::EXCHANGE_TIMEOUT), "exchange-timeout");
    EXPECT_STREQ(to_string(eChirpPassEnd::FAILED), "failed");
}

TEST(DppEnrolleeChirpOwner, OnlyAPassOwnsTheChirp)
{
    EXPECT_EQ(owned_chirp_iface(true, "wl0", false, ""), "wl0");
    // Auth Request during a pass: the pass bSTA, not the bSTA of the request.
    EXPECT_EQ(owned_chirp_iface(false, "wl1", true, "wl0"), "wl0");
    // Exchange, pause, idle, cooldown, watch or done: nothing to stop, also after a cleanup.
    EXPECT_EQ(owned_chirp_iface(false, "wl0", false, "wl0"), "");
    EXPECT_EQ(owned_chirp_iface(false, "", false, ""), "");
}

TEST(DppEnrolleeChirpChannel, ListedNonDfsChannelsAreSafe)
{
    EXPECT_TRUE(chirp_channel_safe(2412, eFreqType::FREQ_24G, k_24g_full));
    EXPECT_TRUE(chirp_channel_safe(2462, eFreqType::FREQ_24G, k_24g_full));
    EXPECT_TRUE(chirp_channel_safe(5180, eFreqType::FREQ_5G, k_5g_full));
    EXPECT_TRUE(chirp_channel_safe(5745, eFreqType::FREQ_5G, k_5g_full));
    EXPECT_TRUE(chirp_channel_safe(5885, eFreqType::FREQ_58G, k_5g_full)); // UNII-4 ch 177
    const ChannelDfsMap k_6g = {{2, Dfs::NOT_DFS}, {5, Dfs::NOT_DFS}, {37, Dfs::NOT_DFS}};
    EXPECT_TRUE(chirp_channel_safe(5935, eFreqType::FREQ_6G, k_6g));
    EXPECT_TRUE(chirp_channel_safe(5975, eFreqType::FREQ_6G, k_6g));
    EXPECT_TRUE(chirp_channel_safe(6135, eFreqType::FREQ_6G, k_6g));
    const ChannelDfsMap k_ch14 = {{14, Dfs::NOT_DFS}};
    EXPECT_TRUE(chirp_channel_safe(2484, eFreqType::FREQ_24G, k_ch14));
}

TEST(DppEnrolleeChirpChannel, CceSixGhzChannelsAreSafeOnlyWhenListedNotDfs)
{
    // 6 GHz CCE chirp channels: 6055, 6135, 6375, 6775, 6855 MHz = ch 21, 37, 85, 165, 181.
    const uint32_t k_cce_freqs[] = {6055, 6135, 6375, 6775, 6855};
    const ChannelDfsMap k_listed = {
        {21, Dfs::NOT_DFS}, {37, Dfs::NOT_DFS}, {85, Dfs::NOT_DFS}, {165, Dfs::NOT_DFS},
        {181, Dfs::NOT_DFS}};
    // A default AgentDB entry (USABLE): the state before the BWL set it for every channel.
    const ChannelDfsMap k_default = {
        {21, Dfs::USABLE}, {37, Dfs::USABLE}, {85, Dfs::USABLE}, {165, Dfs::USABLE}};
    for (const auto freq : k_cce_freqs) {
        EXPECT_TRUE(chirp_channel_safe(freq, eFreqType::FREQ_6G, k_listed)) << freq;
        EXPECT_FALSE(chirp_channel_safe(freq, eFreqType::FREQ_6G, k_default)) << freq;
        EXPECT_FALSE(chirp_channel_safe(freq, eFreqType::FREQ_6G, ChannelDfsMap{})) << freq;
    }
}

TEST(DppEnrolleeChirpChannel, SafeChannelsStringListsNotDfsChannelsInOrder)
{
    const ChannelDfsMap k_mixed = {{149, Dfs::NOT_DFS}, {37, Dfs::NOT_DFS}, {52, Dfs::USABLE},
                                   {100, Dfs::AVAILABLE}, {5, Dfs::NOT_DFS}};
    EXPECT_EQ(chirp_safe_channels_string(k_mixed), "5,37,149");
    EXPECT_EQ(chirp_safe_channels_string(ChannelDfsMap{{36, Dfs::NOT_DFS}}), "36");
    EXPECT_EQ(chirp_safe_channels_string(ChannelDfsMap{}), "none");
    const ChannelDfsMap k_all_dfs = {{52, Dfs::USABLE}, {116, Dfs::UNAVAILABLE}};
    EXPECT_EQ(chirp_safe_channels_string(k_all_dfs), "none");
}

TEST(DppEnrolleeChirpChannel, DfsChannelsAreUnsafeInEveryState)
{
    const ChannelDfsMap k_dfs = {
        {36, Dfs::NOT_DFS}, {52, Dfs::USABLE}, {100, Dfs::AVAILABLE}, {116, Dfs::UNAVAILABLE}};
    EXPECT_TRUE(chirp_channel_safe(5180, eFreqType::FREQ_5G, k_dfs));
    EXPECT_FALSE(chirp_channel_safe(5260, eFreqType::FREQ_5G, k_dfs)); // USABLE
    EXPECT_FALSE(chirp_channel_safe(5500, eFreqType::FREQ_5G, k_dfs)); // AVAILABLE, CAC done
    EXPECT_FALSE(chirp_channel_safe(5580, eFreqType::FREQ_5G, k_dfs)); // UNAVAILABLE
}

TEST(DppEnrolleeChirpChannel, UnknownFrequencyOrChannelIsUnsafe)
{
    EXPECT_FALSE(chirp_channel_safe(0, eFreqType::FREQ_5G, k_5g_full)); // freq= absent/malformed
    EXPECT_FALSE(chirp_channel_safe(99999, eFreqType::FREQ_5G, k_5g_full));
    EXPECT_FALSE(chirp_channel_safe(5182, eFreqType::FREQ_5G, k_5g_full));   // no channel center
    EXPECT_FALSE(chirp_channel_safe(5320, eFreqType::FREQ_5G, k_5g_full));   // 64 not listed
    EXPECT_FALSE(chirp_channel_safe(2417, eFreqType::FREQ_24G, k_24g_full)); // 2 not listed
    EXPECT_FALSE(chirp_channel_safe(5180, eFreqType::FREQ_5G, ChannelDfsMap{}));
}

TEST(DppEnrolleeChirpChannel, FrequencyOfAnotherBandIsUnsafe)
{
    EXPECT_FALSE(chirp_channel_safe(2412, eFreqType::FREQ_5G, k_5g_full));
    EXPECT_FALSE(chirp_channel_safe(5180, eFreqType::FREQ_24G, k_24g_full));
    // 6 GHz reuses 5 GHz numbers: 6695 MHz is 6 GHz ch 149, 5745 MHz is 5 GHz ch 149.
    const ChannelDfsMap k_149 = {{149, Dfs::NOT_DFS}};
    EXPECT_TRUE(chirp_channel_safe(6695, eFreqType::FREQ_6G, k_149));
    EXPECT_FALSE(chirp_channel_safe(6695, eFreqType::FREQ_5G, k_149));
    EXPECT_FALSE(chirp_channel_safe(5745, eFreqType::FREQ_6G, k_149));
    // Multi-band or unknown radio band: the channel number is ambiguous.
    EXPECT_FALSE(chirp_channel_safe(5180, eFreqType::FREQ_24G_5G, k_5g_full));
    EXPECT_FALSE(chirp_channel_safe(5180, eFreqType::FREQ_AUTO, k_5g_full));
    EXPECT_FALSE(chirp_channel_safe(5180, eFreqType::FREQ_UNKNOWN, k_5g_full));
}

TEST(DppEnrolleeCceScan, RetryCooldownLastStepRepeats)
{
    const std::vector<int> steps = {600, 1200, 1800};
    EXPECT_EQ(chirp_retry_cooldown_seconds(steps, 0), 600);
    EXPECT_EQ(chirp_retry_cooldown_seconds(steps, 1), 1200);
    EXPECT_EQ(chirp_retry_cooldown_seconds(steps, 2), 1800);
    EXPECT_EQ(chirp_retry_cooldown_seconds(steps, 3), 1800);
    EXPECT_EQ(chirp_retry_cooldown_seconds(steps, 100), 1800);
    EXPECT_EQ(chirp_retry_cooldown_seconds({}, 0), 0);
}

TEST(DppEnrolleeCceScan, WatchRoundCountersPerRoundAndBsta)
{
    // Review F5.
    sWatchRoundStats stats;
    EXPECT_EQ(watch_round_requests_string(stats), "-");

    watch_round_record(stats, 0); // no bSTA listed
    watch_round_record(stats, 2);
    watch_round_record_request(stats, "wlan1", true);
    watch_round_record_request(stats, "wlan0", false);
    watch_round_record(stats, 1);
    watch_round_record_request(stats, "wlan1", true);
    watch_round_record(stats, 0);

    EXPECT_EQ(stats.rounds, 4u);
    EXPECT_EQ(stats.no_eligible, 2u);
    EXPECT_EQ(stats.requests["wlan1"].sent, 2u);
    EXPECT_EQ(stats.requests["wlan1"].started, 2u);
    EXPECT_EQ(stats.requests["wlan0"].sent, 1u);
    EXPECT_EQ(stats.requests["wlan0"].started, 0u);
    // Sorted by iface, started/sent.
    EXPECT_EQ(watch_round_requests_string(stats), "wlan0:0/1 wlan1:2/2");
}
