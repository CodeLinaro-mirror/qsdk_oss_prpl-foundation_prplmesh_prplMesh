/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_ENROLLEE_CHANNEL_PAIR_H_
#define _DPP_ENROLLEE_CHANNEL_PAIR_H_

#include <bcl/beerocks_defines.h>
#include <beerocks/tlvf/enums/eDfsState.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace beerocks {
namespace dpp_enrollee {

enum class eListDecision {
    LIST,   ///< Register with chan_list.
    NO_LIST ///< Register without a list: no chanlist argument, URI has no C:.
};

const char *to_string(eListDecision decision);

struct sChannelListSelection {
    eListDecision decision = eListDecision::NO_LIST;
    std::string chan_list;    ///< wpa_supplicant "chan=" form, e.g. "115/36,40,124/149". LIST only.
    size_t channel_count = 0; ///< Number of channels in chan_list.
    const char *reason   = ""; ///< Short text for logs.
};

/** Channel number -> DFS state of ONE radio (the radio of the EndPoint). */
using ChannelDfsMap = std::map<uint8_t, beerocks_message::eDfsState>;

/**
 * Chooses the URI channel list (C:) for one backhaul EndPoint.
 * band must be the band of the EndPoint's own radio; channel numbers alone do not identify
 * the band (6 GHz reuses 5 GHz numbers such as 149). Deterministic: same input, same output.
 *
 * wpa_supplicant drops disabled, no-IR and radar channels from its chirp list (feed patch z00),
 * so the list holds only channels the radio lists and that need no DFS; a DFS channel would
 * never be chirped on (Easy Connect 6.2.2 allows a list):
 * - 2.4 GHz: every listed channel 1..13 in class 81. Empty list (inventory not known): 81/6.
 *   A non-empty list without any of 1..13: NO_LIST.
 * - 5 GHz (FREQ_5G or FREQ_58G): listed NOT_DFS channels in 115 (36,40,44,48),
 *   124 (149,153,157,161) and 125 (165,169,173,177). pWHM drops non-DFS no-IR channels, so a
 *   listed UNII-4 channel is usable. 52-144 are in no class. None: NO_LIST.
 * - 6 GHz, multi-band or unknown: NO_LIST.
 * Only exact 20 MHz class members are listed (37 or 151 are skipped). Channels are in
 * ascending order; a class is written once before its first channel.
 */
sChannelListSelection select_bootstrap_channel_list(eFreqType band, const ChannelDfsMap &channels);

/** Number of channels in a "chan=" list ("115/36,40" -> 2, "" -> 0). */
size_t count_chan_list_channels(const std::string &chan_list);

/**
 * Whether a chirp sent on freq_mhz (DPP-TX freq=) is allowed on the EndPoint's radio.
 * Safe only when freq_mhz is a 20 MHz channel center of band and channels lists that channel
 * as NOT_DFS. Unsafe: freq 0 or not a channel of band, a channel the radio does not list, any
 * DFS state (USABLE, AVAILABLE, UNAVAILABLE), or band not FREQ_24G/5G/58G/6G. channels has no
 * no-IR or disabled flag: pWHM leaves disabled and non-DFS no-IR channels out of
 * Radio.PossibleChannels (unlisted, so unsafe) and lists DFS no-IR channels as DFS (unsafe).
 */
bool chirp_channel_safe(uint32_t freq_mhz, eFreqType band, const ChannelDfsMap &channels);

/**
 * The channels chirp_channel_safe() accepts in channels: the NOT_DFS entries in ascending order,
 * comma separated ("1,5,37"), or "none". For the chirp-safe channel log of a bSTA.
 */
std::string chirp_safe_channels_string(const ChannelDfsMap &channels);

/** One scan entry as the CCE watcher reads it (subset of bwl::sScanResult). */
struct sCceScanEntry {
    std::string bssid;
    uint8_t channel     = 0;
    eFreqType band      = eFreqType::FREQ_UNKNOWN; ///< FREQ_UNKNOWN: band of the scanning radio.
    bool cce_known      = false;                   ///< pWHM reported the CCE state.
    bool cce_present    = false;                   ///< The AP advertises a CCE.
    int32_t seen_ms_ago = -1;                      ///< Age at the read; -1 when unknown.
};

/**
 * Cooldown after failed_cycles retry cycles ended without success: steps[failed_cycles], the
 * last step repeats. 0 when steps is empty.
 */
int chirp_retry_cooldown_seconds(const std::vector<int> &steps, size_t failed_cycles);

/**
 * Chirp pass guard after the supplicant sent a chirp (DPP-TX type=13) at chirp_sent.
 * The supplicant chirps on the URI channels, its default channels and the channels of scanned
 * APs with a CCE, so the channel count of a pass is not known when it starts. Each chirp moves
 * the guard to chirp_sent + progress, never past pass_start + max_pass and never earlier than
 * the current guard.
 */
std::chrono::steady_clock::time_point
extend_chirp_pass_deadline(std::chrono::steady_clock::time_point deadline,
                           std::chrono::steady_clock::time_point pass_start,
                           std::chrono::steady_clock::time_point chirp_sent,
                           std::chrono::seconds progress, std::chrono::seconds max_pass);

/** Auto-chirp campaign phase that may hold the WAIT_WPS restart, see chirp_holds_restart(). */
enum class eChirpHoldPhase {
    NONE,     ///< Idle, cycle backoff, cooldown or done: never holds.
    PASS,     ///< A chirp pass runs; until = pass guard plus grace.
    GAP,      ///< Pause between two passes of one cycle; until = end of the pause plus grace.
    EXCHANGE, ///< DPP exchange after an answered chirp; until = exchange deadline.
    WATCH     ///< CCE watch with scans requested; until = next watch round plus grace.
};

const char *to_string(eChirpHoldPhase phase);

/**
 * Whether the auto-chirp campaign holds an expired WAIT_WPS timeout at now. The restart detaches
 * the bSTA HAL, so a running pass loses its chirp events and its pass guard ends it.
 * PASS, GAP and WATCH hold only while the Agent is an unconfigured Enrollee (agent_unconfigured);
 * EXCHANGE holds until its own deadline. Every phase ends at until, so a hold is bounded.
 */
bool chirp_holds_restart(eChirpHoldPhase phase, bool agent_unconfigured,
                         std::chrono::steady_clock::time_point now,
                         std::chrono::steady_clock::time_point until);

/** True once a timeout that expired at expired_at has been held for more than max_hold. */
bool chirp_hold_limit_reached(std::chrono::steady_clock::time_point now,
                              std::chrono::steady_clock::time_point expired_at,
                              std::chrono::seconds max_hold);

/**
 * Chirp order rank of a bSTA band; lower ranks chirp first. The preferred backhaul band comes
 * first when it names one band, then 5 GHz, 2.4 GHz, 6 GHz and other bands. A Multi-AP
 * backhaul BSS usually runs on 5 GHz, and a configurator may listen for presence announcements
 * on its BSS operating channels only (EC 6.2.4).
 */
int chirp_band_rank(beerocks::eFreqType band, beerocks::eFreqType preferred);

/** Indices into bands in chirp order: by chirp_band_rank(), equal ranks keep their order. */
std::vector<size_t> chirp_band_order(const std::vector<beerocks::eFreqType> &bands,
                                     beerocks::eFreqType preferred);

/**
 * EC 6.2.3 iteration of the Enrollee: each bSTA chirps its channel list once, then the Enrollee
 * pauses before it repeats. The list and the pause belong to the device, so the iteration covers
 * all bSTAs. Kept by bSTA name: survives refused starts, reordering, RESTART and re-attach.
 */
struct sChirpIteration {
    std::set<std::string> passed;  ///< bSTAs whose pass ended in this iteration (any reason).
    std::set<std::string> skipped; ///< bSTAs skipped after refused starts.
    std::chrono::steady_clock::time_point last_pass_end{};
    bool paused = false; ///< No bSTA left: nothing starts before last_pass_end + pause.
};

/** Next pass start: index into the bSTAs, or not before not_before. */
struct sChirpPick {
    bool ready   = false;
    size_t index = 0;
    std::chrono::steady_clock::time_point not_before{};
};

/**
 * Picks the bSTA of the next pass from ifaces (chirp order); preferred first when still open.
 * A new iteration begins once pause has passed since the last pass end. Without an open bSTA
 * after a pass end the list is exhausted: no bSTA starts before last_pass_end + pause, also one
 * that becomes eligible meanwhile. With refused starts only, the bSTAs are tried again.
 */
sChirpPick chirp_iteration_pick(sChirpIteration &iteration, const std::vector<std::string> &ifaces,
                                const std::string &preferred,
                                std::chrono::steady_clock::time_point now,
                                std::chrono::seconds pause);

/** Records the end of a pass on iface (stop, watchdog, failure, exchange end). */
void chirp_iteration_pass_end(sChirpIteration &iteration, const std::string &iface,
                              std::chrono::steady_clock::time_point now);

/** Skips iface for the rest of the iteration after refused starts; not a pass. */
void chirp_iteration_skip(sChirpIteration &iteration, const std::string &iface);

/** True when every bSTA of ifaces ended a pass or was skipped in the iteration. */
bool chirp_iteration_exhausted(const sChirpIteration &iteration,
                               const std::vector<std::string> &ifaces);

/** Why a chirp pass ended; to_string() gives the logged reason. */
enum class eChirpPassEnd {
    COMPLETED,        ///< The supplicant reported DPP-CHIRP-STOPPED.
    WATCHDOG,         ///< The pass guard expired.
    EXCHANGE_TIMEOUT, ///< A chirp-triggered exchange did not finish in time.
    FAILED            ///< The supplicant reported a DPP failure.
};

const char *to_string(eChirpPassEnd end);

/**
 * Gap before the next pass of a cycle. After a clean pass (completed or watchdog) inside an
 * iteration the next band starts after band_switch. The full pause follows the last band of an
 * iteration (EC 6.2.3: pause once all channels are exhausted) and a failed or timed out exchange.
 */
std::chrono::seconds chirp_gap_after_pass(eChirpPassEnd end, bool iteration_complete,
                                          std::chrono::seconds band_switch,
                                          std::chrono::seconds pause);

/**
 * bSTA whose supplicant chirp the Agent owns, "" when none. A running pass owns the chirp on its
 * bSTA (in_pass, pass_iface). An Auth Request that interrupted a pass keeps that ownership until
 * it is answered or dropped (auth_pending_after_pass, auth_prev_iface): wpa_supplicant may still
 * chirp on the pass bSTA. An exchange, a pause or a finished campaign owns no chirp.
 */
std::string owned_chirp_iface(bool in_pass, const std::string &pass_iface,
                              bool auth_pending_after_pass, const std::string &auth_prev_iface);

/** CCE scan requests of one bSTA in WATCH: sent, and started by pWHM (STARTED). */
struct sWatchScanRequests {
    unsigned sent    = 0;
    unsigned started = 0;
};

/**
 * WATCH round counters for the round log. Diagnostics only: no decision reads them.
 * The Agent keeps them for its lifetime, across RESTART and cooldowns.
 */
struct sWatchRoundStats {
    unsigned rounds      = 0; ///< WATCH rounds run.
    unsigned no_eligible = 0; ///< Rounds with no eligible bSTA (none listed).
    std::map<std::string, sWatchScanRequests> requests; ///< Keyed by sta_iface.
};

/** Counts one WATCH round; eligible is its number of eligible bSTAs. */
void watch_round_record(sWatchRoundStats &stats, std::size_t eligible);

/** Counts one CCE scan request on sta_iface; started is true when pWHM started the scan. */
void watch_round_record_request(sWatchRoundStats &stats, const std::string &sta_iface,
                                bool started);

/** Per-bSTA requests as "iface:started/sent" separated by spaces; "-" when there are none. */
std::string watch_round_requests_string(const sWatchRoundStats &stats);

/** Builds a ChannelDfsMap from a radio channel map whose values have a dfs_state member
 *  (for example AgentDB::sRadio::channels_list). */
template <typename ChannelInfoMap>
ChannelDfsMap make_channel_dfs_map(const ChannelInfoMap &channels_list)
{
    ChannelDfsMap out;
    for (const auto &entry : channels_list) {
        out[entry.first] = entry.second.dfs_state;
    }
    return out;
}

} // namespace dpp_enrollee
} // namespace beerocks

#endif // _DPP_ENROLLEE_CHANNEL_PAIR_H_
