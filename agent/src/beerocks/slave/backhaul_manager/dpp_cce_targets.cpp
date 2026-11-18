/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_cce_targets.h"

#include <algorithm>
#include <map>
#include <utility>

namespace beerocks {
namespace dpp_enrollee {

namespace {

eFreqType band_group_of(eFreqType band)
{
    return band == eFreqType::FREQ_58G ? eFreqType::FREQ_5G : band;
}

} // namespace

uint32_t channel_to_freq(uint8_t channel, eFreqType band)
{
    switch (band) {
    case eFreqType::FREQ_24G:
        if (channel == 14) {
            return 2484;
        }
        return (channel >= 1 && channel <= 13) ? 2407 + 5u * channel : 0;
    case eFreqType::FREQ_5G:
    case eFreqType::FREQ_58G:
        return (channel >= 32 && channel <= 177) ? 5000 + 5u * channel : 0;
    case eFreqType::FREQ_6G:
        if (channel == 2) {
            return 5935;
        }
        return (channel >= 1 && channel <= 233) ? 5950 + 5u * channel : 0;
    default:
        return 0;
    }
}

std::string freqs_string(const std::vector<uint32_t> &freqs)
{
    std::string out;
    for (const auto freq : freqs) {
        if (!out.empty()) {
            out += ' ';
        }
        out += std::to_string(freq);
    }
    return out;
}

void watch_cadence_boost(sWatchCadence &cadence, std::chrono::steady_clock::time_point now,
                         std::chrono::seconds window)
{
    cadence.fast_until = std::max(cadence.fast_until, now + window);
}

std::chrono::seconds watch_interval(const sWatchCadence &cadence,
                                    std::chrono::steady_clock::time_point now,
                                    std::chrono::seconds fast, std::chrono::seconds slow)
{
    return now < cadence.fast_until ? fast : slow;
}

std::chrono::seconds watch_next_read(bool scan_pending, bool scan_started,
                                     std::chrono::seconds interval, std::chrono::seconds first_read)
{
    return (scan_started && !scan_pending) ? std::min(interval, first_read) : interval;
}

void fallback_timer_arm(sFallbackTimer &timer, std::chrono::steady_clock::time_point ready,
                        std::chrono::seconds first)
{
    timer.armed  = true;
    timer.due    = ready + first;
    timer.failed = 0;
}

void fallback_timer_pass_failed(sFallbackTimer &timer, std::chrono::steady_clock::time_point now,
                                const std::vector<int> &steps, std::chrono::seconds jitter)
{
    const auto step = std::chrono::seconds(chirp_retry_cooldown_seconds(steps, timer.failed));
    timer.armed     = true;
    timer.due       = now + step + std::max(jitter, std::chrono::seconds::zero());
    ++timer.failed;
}

bool fallback_timer_due(const sFallbackTimer &timer, std::chrono::steady_clock::time_point now)
{
    return timer.armed && now >= timer.due;
}

const char *to_string(eCceTargetReason reason)
{
    switch (reason) {
    case eCceTargetReason::NONE:
        return "none";
    case eCceTargetReason::FRESH:
        return "FRESH";
    case eCceTargetReason::RISING:
        return "RISING";
    }
    return "unknown";
}

sCceTarget cce_target_update(sCceTargetState &state, const std::vector<sCceScanEntry> &entries,
                             eFreqType band, const ChannelDfsMap &channels,
                             std::chrono::steady_clock::time_point now,
                             std::chrono::seconds freshness, size_t max_freqs)
{
    sCceTarget target;
    std::map<uint32_t, int32_t> youngest; // fresh safe frequency -> youngest age (ms)
    for (const auto &entry : entries) {
        if (!entry.cce_known || !entry.cce_present) {
            continue; // no CCE, or no CCE metadata (D2)
        }
        const bool same_band = entry.band == eFreqType::FREQ_UNKNOWN ||
                               band_group_of(entry.band) == band_group_of(band);
        const uint32_t freq  = same_band ? channel_to_freq(entry.channel, band) : 0;
        if (freq == 0 || !chirp_channel_safe(freq, band, channels)) {
            ++target.unsafe; // never scoped in: DFS, unlisted or other band
            continue;
        }
        if (entry.seen_ms_ago < 0 || std::chrono::milliseconds(entry.seen_ms_ago) > freshness) {
            continue;
        }
        ++target.bssids;
        const auto it = youngest.find(freq);
        if (it == youngest.end() || entry.seen_ms_ago < it->second) {
            youngest[freq] = entry.seen_ms_ago;
        }
    }

    std::set<uint32_t> fresh;
    for (const auto &item : youngest) {
        fresh.insert(item.first);
    }
    bool rising = false;
    if (state.baseline) {
        for (const auto freq : fresh) {
            if (state.fresh_freqs.count(freq) == 0) {
                rising = true;
                break;
            }
        }
    }
    state.baseline    = true;
    state.fresh_freqs = fresh;
    if (rising) {
        state.backoff_step = 0; // D17: a rising edge retries at once
        state.next_allowed = now;
    }

    // Cap: keep the youngest frequencies (ties: lower frequency), then sort ascending.
    std::vector<std::pair<int32_t, uint32_t>> by_age;
    for (const auto &item : youngest) {
        by_age.emplace_back(item.second, item.first);
    }
    std::sort(by_age.begin(), by_age.end());
    for (const auto &item : by_age) {
        if (target.freqs.size() < max_freqs) {
            target.freqs.push_back(item.second);
        } else {
            ++target.dropped;
        }
    }
    std::sort(target.freqs.begin(), target.freqs.end());

    target.reason       = fresh.empty() ? eCceTargetReason::NONE
                          : rising      ? eCceTargetReason::RISING
                                        : eCceTargetReason::FRESH;
    target.next_allowed = state.next_allowed;
    target.ready        = !target.freqs.empty() && now >= state.next_allowed;
    return target;
}

void cce_target_failed(sCceTargetState &state, std::chrono::steady_clock::time_point now,
                       const std::vector<int> &steps)
{
    const auto step = std::chrono::seconds(chirp_retry_cooldown_seconds(steps, state.backoff_step));
    state.next_allowed = now + step;
    ++state.backoff_step;
}

} // namespace dpp_enrollee
} // namespace beerocks
