/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_CCE_TARGETS_H_
#define _DPP_CCE_TARGETS_H_

#include "dpp_enrollee_channel_pair.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

/*
 * CCE-targeted chirp (Nov 17 revision of the CCE watch plan): pure decisions, no I/O. The
 * Agent state machine feeds scan reads and pass results in and acts on the output.
 * - Watch cadence (D16): fast scan rounds for a window after READY or an event, else slow.
 * - Fallback timer (D14): first URI-channel pass a fixed time after READY, then at the retry
 *   cooldown steps; independent of the watch cadence.
 * - CCE target (D13, D17): one target per bSTA band, rebuilt from each scan read (no stored
 *   BSSID table); a failed target backs off; a rising edge retries at once.
 * - Scope (C7, D18): URI frequencies for the fallback freq_list, and the frequencies a scoped
 *   pass may send DPP-TX on.
 */

namespace beerocks {
namespace dpp_enrollee {

/** Center frequency (MHz) of 20 MHz channel in band (5.8 GHz counts as 5 GHz); 0 if none. */
uint32_t channel_to_freq(uint8_t channel, eFreqType band);

/** Frequencies separated by single spaces ("5180 5745"), the SET freq_list form; "" if none. */
std::string freqs_string(const std::vector<uint32_t> &freqs);

/** D16: when the watch runs fast scan rounds. */
struct sWatchCadence {
    std::chrono::steady_clock::time_point fast_until{};
};

/** D16: fast rounds until at least now + window (READY, rising edge, failed target cycle,
 *  fallback pass end). Never shortens a running fast window. */
void watch_cadence_boost(sWatchCadence &cadence, std::chrono::steady_clock::time_point now,
                         std::chrono::seconds window);

/** D16: interval to the next watch round started at now: fast inside the window, else slow. */
std::chrono::seconds watch_interval(const sWatchCadence &cadence,
                                    std::chrono::steady_clock::time_point now,
                                    std::chrono::seconds fast, std::chrono::seconds slow);

/**
 * D16: time from a watch round to the next one, which reads the scans of this round. A round
 * that started a scan while none was pending from the previous round reads it after
 * first_read (the scan time) instead of a full interval, so the first CCE read after READY or
 * after a round without a scan does not wait one round.
 */
std::chrono::seconds watch_next_read(bool scan_pending, bool scan_started,
                                     std::chrono::seconds interval,
                                     std::chrono::seconds first_read);

/** D14: URI-channel fallback timer. */
struct sFallbackTimer {
    bool armed = false;
    std::chrono::steady_clock::time_point due{};
    size_t failed = 0; ///< Fallback passes that ended without onboarding.
};

/** D14: arms the timer at bootstrap READY: first fallback at ready + first; resets the count. */
void fallback_timer_arm(sFallbackTimer &timer, std::chrono::steady_clock::time_point ready,
                        std::chrono::seconds first);

/**
 * D14: a fallback pass ended at now without onboarding. The next one is due at
 * now + steps[failed] (the last step repeats) + jitter, and the count increases. jitter is the
 * caller's random 0..max value (never negative), so tests stay deterministic.
 */
void fallback_timer_pass_failed(sFallbackTimer &timer, std::chrono::steady_clock::time_point now,
                                const std::vector<int> &steps, std::chrono::seconds jitter);

/** D14: true when the timer is armed and due at now. */
bool fallback_timer_due(const sFallbackTimer &timer, std::chrono::steady_clock::time_point now);

/** D13/D17: state of the CCE target of one bSTA band, kept between scan reads. */
struct sCceTargetState {
    bool baseline = false;          ///< A read was taken; the first one never counts as rising.
    std::set<uint32_t> fresh_freqs; ///< Fresh safe CCE frequencies of the last read.
    size_t backoff_step = 0;        ///< Failed target cycles since the last rising edge.
    std::chrono::steady_clock::time_point next_allowed{}; ///< No target pass before this.
};

enum class eCceTargetReason {
    NONE,  ///< No fresh safe CCE on the band.
    FRESH, ///< Fresh safe CCE, no new frequency (includes the first read).
    RISING ///< A fresh safe CCE frequency that the previous read did not have.
};

const char *to_string(eCceTargetReason reason);

/** D13: the CCE target of one bSTA band after a scan read. */
struct sCceTarget {
    eCceTargetReason reason = eCceTargetReason::NONE;
    bool ready              = false; ///< reason is not NONE and the backoff has passed.
    std::vector<uint32_t> freqs;     ///< Fresh safe CCE frequencies, ascending, at most max_freqs.
    size_t dropped = 0;              ///< Fresh safe frequencies left out by the cap.
    size_t bssids  = 0;              ///< Fresh CCE entries on safe channels.
    size_t unsafe  = 0;              ///< CCE entries on unsafe channels: never in freqs.
    std::chrono::steady_clock::time_point next_allowed{}; ///< Backoff end when not ready.
};

/**
 * D13/D17: rebuilds the target of one bSTA of band from one scan read. An entry counts when its
 * CCE is known and present. Safe channels follow chirp_channel_safe() (DFS, unlisted or other
 * band: unsafe, counted only). A safe entry seen at most freshness ago (known age) adds its
 * frequency. Over max_freqs, the youngest frequencies are kept. A frequency that the previous
 * read did not have is a rising edge (not on the first read): it clears the backoff.
 */
sCceTarget cce_target_update(sCceTargetState &state, const std::vector<sCceScanEntry> &entries,
                             eFreqType band, const ChannelDfsMap &channels,
                             std::chrono::steady_clock::time_point now,
                             std::chrono::seconds freshness, size_t max_freqs);

/**
 * D17: a target cycle ended at now without onboarding. No target pass before
 * now + steps[backoff_step] (the last step repeats, 120 -> 240 -> 480 s); the step increases.
 */
void cce_target_failed(sCceTargetState &state, std::chrono::steady_clock::time_point now,
                       const std::vector<int> &steps);

} // namespace dpp_enrollee
} // namespace beerocks

#endif // _DPP_CCE_TARGETS_H_
