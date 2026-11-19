/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_enrollee_channel_pair.h"

#include <algorithm>
#include <utility>

namespace beerocks {
namespace dpp_enrollee {

namespace {

/** Global operating class (IEEE 802.11 Annex E, 20 MHz) and its exact channel set. */
struct sClassRange {
    uint8_t operating_class;
    uint8_t first;
    uint8_t last;
    uint8_t step; ///< Members are first, first + step, ... up to last.
};

constexpr sClassRange k_24g_range = {81, 1, 13, 1};
// Non-DFS 5 GHz classes only; DFS classes 118/121 are left out on purpose (DFS chirp guard).
constexpr sClassRange k_5g_ranges[]  = {{115, 36, 48, 4}, {124, 149, 161, 4}, {125, 165, 177, 4}};
constexpr const char *k_24g_fallback = "81/6";

// Appends the listed member channels of one class; the class prefix is written once.
void append_class(sChannelListSelection &out, const sClassRange &range,
                  const ChannelDfsMap &channels, bool non_dfs_only)
{
    bool class_written = false;
    for (const auto &entry : channels) { // std::map: ascending channel order
        if (entry.first < range.first || entry.first > range.last ||
            (entry.first - range.first) % range.step != 0) {
            continue;
        }
        if (non_dfs_only && entry.second != beerocks_message::eDfsState::NOT_DFS) {
            continue;
        }
        if (!out.chan_list.empty()) {
            out.chan_list += ',';
        }
        if (!class_written) {
            out.chan_list += std::to_string(range.operating_class) + '/';
            class_written = true;
        }
        out.chan_list += std::to_string(entry.first);
        out.channel_count++;
    }
}

sChannelListSelection finish(sChannelListSelection out, const char *list_reason,
                             const char *no_list_reason)
{
    out.decision = out.chan_list.empty() ? eListDecision::NO_LIST : eListDecision::LIST;
    out.reason   = out.chan_list.empty() ? no_list_reason : list_reason;
    return out;
}

} // namespace

const char *to_string(eListDecision decision)
{
    switch (decision) {
    case eListDecision::LIST:
        return "LIST";
    case eListDecision::NO_LIST:
        return "NO_LIST";
    }
    return "UNKNOWN";
}

size_t count_chan_list_channels(const std::string &chan_list)
{
    if (chan_list.empty()) {
        return 0;
    }
    return static_cast<size_t>(std::count(chan_list.begin(), chan_list.end(), ',')) + 1;
}

sChannelListSelection select_bootstrap_channel_list(eFreqType band, const ChannelDfsMap &channels)
{
    // TODO: FEAT-729 - if the EndPoint.DPP channel list DM value is set, list only those
    // channels (still possible, class member, non-DFS on 5 GHz; none left -> no C:).
    // If unset/empty, use the logic below.
    sChannelListSelection out;
    switch (band) {
    case eFreqType::FREQ_24G:
        if (channels.empty()) {
            out.chan_list     = k_24g_fallback;
            out.channel_count = 1;
            return finish(std::move(out), "2.4 GHz, empty list: 81/6", "");
        }
        append_class(out, k_24g_range, channels, false);
        return finish(std::move(out), "2.4 GHz, listed channels",
                      "2.4 GHz, no channel 1-13 listed");
    case eFreqType::FREQ_5G:
    case eFreqType::FREQ_58G:
        for (const auto &range : k_5g_ranges) {
            append_class(out, range, channels, true);
        }
        return finish(std::move(out), "5 GHz, listed non-DFS channels",
                      "5 GHz, no non-DFS channel listed");
    default:
        // 6 GHz chirp is out of scope, so no channel list is advertised.
        return finish(std::move(out), "", "6 GHz or unknown band: no list");
    }
}

std::chrono::steady_clock::time_point
extend_chirp_pass_deadline(std::chrono::steady_clock::time_point deadline,
                           std::chrono::steady_clock::time_point pass_start,
                           std::chrono::steady_clock::time_point chirp_sent,
                           std::chrono::seconds progress, std::chrono::seconds max_pass)
{
    const auto by_progress = chirp_sent + progress;
    const auto by_cap      = pass_start + max_pass;
    return std::max(deadline, std::min(by_progress, by_cap));
}

const char *to_string(eChirpHoldPhase phase)
{
    switch (phase) {
    case eChirpHoldPhase::NONE:
        return "none";
    case eChirpHoldPhase::PASS:
        return "pass";
    case eChirpHoldPhase::GAP:
        return "gap";
    case eChirpHoldPhase::EXCHANGE:
        return "exchange";
    case eChirpHoldPhase::WATCH:
        return "watch";
    }
    return "unknown";
}

bool chirp_holds_restart(eChirpHoldPhase phase, bool agent_unconfigured,
                         std::chrono::steady_clock::time_point now,
                         std::chrono::steady_clock::time_point until)
{
    switch (phase) {
    case eChirpHoldPhase::EXCHANGE:
        return now < until;
    case eChirpHoldPhase::PASS:
    case eChirpHoldPhase::GAP:
    case eChirpHoldPhase::WATCH:
        return agent_unconfigured && now < until;
    case eChirpHoldPhase::NONE:
        break;
    }
    return false;
}

bool chirp_hold_limit_reached(std::chrono::steady_clock::time_point now,
                              std::chrono::steady_clock::time_point expired_at,
                              std::chrono::seconds max_hold)
{
    return now > expired_at + max_hold;
}

int chirp_band_rank(eFreqType band, eFreqType preferred)
{
    const bool one_band = preferred == eFreqType::FREQ_24G || preferred == eFreqType::FREQ_5G ||
                          preferred == eFreqType::FREQ_58G || preferred == eFreqType::FREQ_6G;
    if (one_band && band == preferred) {
        return 0;
    }
    switch (band) {
    case eFreqType::FREQ_5G:
    case eFreqType::FREQ_58G:
        return 1;
    case eFreqType::FREQ_24G:
        return 2;
    case eFreqType::FREQ_6G:
        return 3; // no EC preferred PA channel: reached through URI and CCE channels only
    default:
        return 4;
    }
}

std::vector<size_t> chirp_band_order(const std::vector<eFreqType> &bands, eFreqType preferred)
{
    std::vector<size_t> order(bands.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return chirp_band_rank(bands[a], preferred) < chirp_band_rank(bands[b], preferred);
    });
    return order;
}

sChirpPick chirp_iteration_pick(sChirpIteration &iteration, const std::vector<std::string> &ifaces,
                                const std::string &preferred,
                                std::chrono::steady_clock::time_point now,
                                std::chrono::seconds pause)
{
    sChirpPick pick;
    if (!iteration.passed.empty() && now >= iteration.last_pass_end + pause) {
        iteration.passed.clear();
        iteration.skipped.clear();
        iteration.paused = false;
    }
    if (ifaces.empty()) {
        pick.not_before = now;
        return pick;
    }
    const auto open = [&](const std::string &iface) {
        return !iteration.paused && iteration.passed.count(iface) == 0 &&
               iteration.skipped.count(iface) == 0;
    };
    const auto first = [&](bool only_open) {
        const auto it = std::find(ifaces.begin(), ifaces.end(), preferred);
        if (!preferred.empty() && it != ifaces.end() && (!only_open || open(*it))) {
            return static_cast<size_t>(it - ifaces.begin());
        }
        for (size_t i = 0; i < ifaces.size(); ++i) {
            if (!only_open || open(ifaces[i])) {
                return i;
            }
        }
        return ifaces.size();
    };
    pick.index = first(true);
    if (pick.index < ifaces.size()) {
        pick.ready = true;
        return pick;
    }
    if (!iteration.passed.empty()) {
        // Channel list exhausted: pause before repeating it.
        iteration.paused = true;
        pick.not_before  = iteration.last_pass_end + pause;
        return pick;
    }
    // Refused starts only: nothing was sent, try the bSTAs again.
    iteration.skipped.clear();
    pick.index = first(false);
    pick.ready = true;
    return pick;
}

void chirp_iteration_pass_end(sChirpIteration &iteration, const std::string &iface,
                              std::chrono::steady_clock::time_point now)
{
    iteration.passed.insert(iface);
    iteration.last_pass_end = std::max(iteration.last_pass_end, now);
}

void chirp_iteration_skip(sChirpIteration &iteration, const std::string &iface)
{
    iteration.skipped.insert(iface);
}

bool chirp_iteration_exhausted(const sChirpIteration &iteration,
                               const std::vector<std::string> &ifaces)
{
    return std::all_of(ifaces.begin(), ifaces.end(), [&](const std::string &iface) {
        return iteration.passed.count(iface) != 0 || iteration.skipped.count(iface) != 0;
    });
}

const char *to_string(eChirpPassEnd end)
{
    switch (end) {
    case eChirpPassEnd::COMPLETED:
        return "completed";
    case eChirpPassEnd::WATCHDOG:
        return "watchdog";
    case eChirpPassEnd::EXCHANGE_TIMEOUT:
        return "exchange-timeout";
    case eChirpPassEnd::FAILED:
        return "failed";
    }
    return "unknown";
}

std::chrono::seconds chirp_gap_after_pass(eChirpPassEnd end, bool iteration_complete,
                                          std::chrono::seconds band_switch,
                                          std::chrono::seconds pause)
{
    const bool clean_end = end == eChirpPassEnd::COMPLETED || end == eChirpPassEnd::WATCHDOG;
    return (clean_end && !iteration_complete) ? band_switch : pause;
}

std::string owned_chirp_iface(bool in_pass, const std::string &pass_iface,
                              bool auth_pending_after_pass, const std::string &auth_prev_iface)
{
    if (in_pass) {
        return pass_iface;
    }
    return auth_pending_after_pass ? auth_prev_iface : std::string();
}

void watch_round_record(sWatchRoundStats &stats, std::size_t eligible)
{
    ++stats.rounds;
    if (eligible == 0) {
        ++stats.no_eligible;
    }
}

void watch_round_record_request(sWatchRoundStats &stats, const std::string &sta_iface, bool started)
{
    auto &requests = stats.requests[sta_iface];
    ++requests.sent;
    if (started) {
        ++requests.started;
    }
}

std::string watch_round_requests_string(const sWatchRoundStats &stats)
{
    if (stats.requests.empty()) {
        return "-";
    }
    std::string out;
    for (const auto &entry : stats.requests) {
        if (!out.empty()) {
            out += ' ';
        }
        out += entry.first + ':' + std::to_string(entry.second.started) + '/' +
               std::to_string(entry.second.sent);
    }
    return out;
}

namespace {

// 20 MHz channel of freq_mhz in band; 0 when freq_mhz is no channel center of band.
uint8_t channel_of_freq(uint32_t freq_mhz, eFreqType band)
{
    switch (band) {
    case eFreqType::FREQ_24G:
        if (freq_mhz == 2484) {
            return 14;
        }
        if (freq_mhz >= 2412 && freq_mhz <= 2472 && (freq_mhz - 2407) % 5 == 0) {
            return static_cast<uint8_t>((freq_mhz - 2407) / 5);
        }
        return 0;
    case eFreqType::FREQ_5G:
    case eFreqType::FREQ_58G:
        // Channels 32..177, including UNII-4 (169..177).
        if (freq_mhz >= 5160 && freq_mhz <= 5885 && freq_mhz % 5 == 0) {
            return static_cast<uint8_t>((freq_mhz - 5000) / 5);
        }
        return 0;
    case eFreqType::FREQ_6G:
        if (freq_mhz == 5935) {
            return 2;
        }
        if (freq_mhz >= 5955 && freq_mhz <= 7115 && (freq_mhz - 5950) % 5 == 0) {
            return static_cast<uint8_t>((freq_mhz - 5950) / 5);
        }
        return 0;
    default:
        return 0;
    }
}

} // namespace

bool chirp_channel_safe(uint32_t freq_mhz, eFreqType band, const ChannelDfsMap &channels)
{
    const uint8_t channel = channel_of_freq(freq_mhz, band);
    if (channel == 0) {
        return false;
    }
    const auto it = channels.find(channel);
    return it != channels.end() && it->second == beerocks_message::eDfsState::NOT_DFS;
}

std::string chirp_safe_channels_string(const ChannelDfsMap &channels)
{
    std::string out;
    for (const auto &entry : channels) {
        if (entry.second != beerocks_message::eDfsState::NOT_DFS) {
            continue;
        }
        if (!out.empty()) {
            out += ',';
        }
        out += std::to_string(entry.first);
    }
    if (out.empty()) {
        out = "none";
    }
    return out;
}

int chirp_retry_cooldown_seconds(const std::vector<int> &steps, size_t failed_cycles)
{
    if (steps.empty()) {
        return 0;
    }
    return steps[std::min(failed_cycles, steps.size() - 1)];
}

} // namespace dpp_enrollee
} // namespace beerocks
