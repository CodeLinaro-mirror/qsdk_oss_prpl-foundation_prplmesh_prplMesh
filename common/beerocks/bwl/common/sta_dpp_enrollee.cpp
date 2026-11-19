/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bwl/sta_dpp_enrollee.h>

#include <algorithm>
#include <limits>
#include <sstream>

namespace bwl {
namespace sta_dpp {

namespace {

const char *const kCurve = "P-256";

bool fail(sBootstrapResult &result, const char *reason)
{
    result       = sBootstrapResult();
    result.error = reason;
    return false;
}

// MHz of a "freq=" value; 0 when empty, not all digits or longer than 5 digits.
uint32_t parse_freq(const std::string &value)
{
    if (value.empty() || value.size() > 5) {
        return 0;
    }
    uint32_t freq = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') {
            return 0;
        }
        freq = freq * 10 + static_cast<uint32_t>(c - '0');
    }
    return freq;
}

} // namespace

bool sBootstrapRegistration::observe_attachment(uint64_t attachment_generation,
                                                const std::string &list)
{
    const bool changed = chan_list != list;
    if (changed) {
        id = 0;
        uri.clear();
    }
    if (changed || generation != attachment_generation) {
        validation_required = true;
    }
    generation = attachment_generation;
    chan_list  = list;
    return changed;
}

eBootstrapValidation validate_bootstrap(EndpointDppBus &bus, uint32_t id, const std::string &uri,
                                        std::string &reason)
{
    reason.clear();
    if (!id || uri.compare(0, 4, "DPP:") != 0) {
        reason = "invalid-cache";
        return eBootstrapValidation::INVALID;
    }
    std::vector<sGeneratedQr> entries;
    if (!bus.read_generated_qr(entries)) {
        reason = "metadata-unavailable";
        return eBootstrapValidation::UNAVAILABLE;
    }
    bool found           = false;
    uint32_t recorded_id = 0;
    for (const auto &entry : entries) {
        if (entry.uri != uri)
            continue;
        if (found && entry.id != recorded_id) {
            reason = "metadata-ambiguous";
            return eBootstrapValidation::UNAVAILABLE;
        }
        found       = true;
        recorded_id = entry.id;
    }
    if (!found || recorded_id != id) {
        reason = !found ? "row-missing" : (!recorded_id ? "id-reset" : "id-changed");
        return eBootstrapValidation::INVALID;
    }
    return eBootstrapValidation::VALID;
}

bool register_bootstrap(EndpointDppBus &bus, const std::string &private_key_hex,
                        const std::string &chan_list, sBootstrapResult &result)
{
    result = sBootstrapResult();
    if (private_key_hex.empty()) {
        return fail(result, "no private key given");
    }

    std::string uri;
    if (!bus.call_bootstrap_gen(kCurve, chan_list, private_key_hex, uri)) {
        return fail(result, "dpp_bootstrap_gen call failed");
    }
    if (uri.empty()) {
        return fail(result, "dpp_bootstrap_gen returned an empty URI");
    }
    if (uri.compare(0, 4, "DPP:") != 0) {
        return fail(result, "dpp_bootstrap_gen returned a non DPP URI");
    }

    // Read the ID after the call. pWHM updates the entry whose URI matches.
    std::vector<sGeneratedQr> entries;
    if (!bus.read_generated_qr(entries)) {
        return fail(result, "failed to read GeneratedQR");
    }

    bool found  = false;
    uint32_t id = 0;
    for (const auto &entry : entries) {
        if (entry.uri != uri) {
            continue;
        }
        if (found && entry.id != id) {
            return fail(result, "GeneratedQR has the URI with different IDs");
        }
        found = true;
        id    = entry.id;
    }
    if (!found) {
        return fail(result, "GeneratedQR has no entry for the returned URI");
    }
    if (id == 0) {
        return fail(result, "GeneratedQR entry has ID 0");
    }

    result.ok  = true;
    result.id  = id;
    result.uri = uri;
    return true;
}

eEnrolleeEvent parse_enrollee_event(const std::string &line, uint32_t *freq_mhz)
{
    if (freq_mhz) {
        *freq_mhz = 0;
    }
    size_t start = 0;
    if (!line.empty() && line[0] == '<') {
        start = line.find('>');
        if (start == std::string::npos) {
            return eEnrolleeEvent::NONE;
        }
        ++start;
    }
    std::istringstream tokens(line.substr(start));
    std::string opcode;
    if (!(tokens >> opcode)) {
        return eEnrolleeEvent::NONE;
    }
    if (opcode == "DPP-RX") {
        // Only an Authentication Request (frame type 0) that the supplicant processes.
        bool auth_request = false;
        std::string token;
        while (tokens >> token) {
            if (token.compare(0, 7, "ignore=") == 0) {
                return eEnrolleeEvent::NONE;
            }
            auth_request = auth_request || token == "type=0";
        }
        return auth_request ? eEnrolleeEvent::AUTH_REQUEST_RX : eEnrolleeEvent::NONE;
    }
    if (opcode == "DPP-TX") {
        // Only a Presence Announcement (frame type 13): one per chirp channel. freq= is the
        // channel it was sent on.
        bool announcement = false;
        uint32_t freq     = 0;
        std::string token;
        while (tokens >> token) {
            if (token == "type=13") {
                announcement = true;
            } else if (token.compare(0, 5, "freq=") == 0) {
                freq = parse_freq(token.substr(5));
            }
        }
        if (!announcement) {
            return eEnrolleeEvent::NONE;
        }
        if (freq_mhz) {
            *freq_mhz = freq;
        }
        return eEnrolleeEvent::CHIRP_TX;
    }
    if (opcode == "DPP-CHIRP-STOPPED") {
        return eEnrolleeEvent::CHIRP_STOPPED;
    }
    if (opcode == "DPP-AUTH-SUCCESS") {
        return eEnrolleeEvent::AUTH_SUCCESS;
    }
    if (opcode == "DPP-CONF-RECEIVED") {
        return eEnrolleeEvent::CONF_RECEIVED;
    }
    if (opcode == "DPP-CONF-FAILED") {
        return eEnrolleeEvent::FAILED;
    }
    if (opcode == "DPP-FAIL") {
        // Not every DPP-FAIL is a failed exchange. wpas_dpp_rx_auth_req() reports DPP-RX
        // type=0 before its own key check, then drops a request for another bootstrapping
        // key (or PKEX peer) with "... - ignore message", and one received during a running
        // exchange with "... - ignore new one" (that exchange goes on).
        std::string text;
        std::getline(tokens, text);
        if (text.find(" - ignore new one") != std::string::npos) {
            return eEnrolleeEvent::NONE;
        }
        if (text.find(" - ignore message") != std::string::npos) {
            return eEnrolleeEvent::AUTH_REQUEST_IGNORED;
        }
        return eEnrolleeEvent::FAILED;
    }
    return eEnrolleeEvent::NONE;
}

void apply_cce_scan_keys(const sCceScanKeys &keys, sScanResult &entry)
{
    entry.dpp_cce_known   = keys.has_cce_known && keys.cce_known && keys.has_cce;
    entry.dpp_cce_present = entry.dpp_cce_known && keys.cce;
    entry.seen_ms_ago     = -1;
    if (keys.has_seen && keys.seen_ms_ago >= 0) {
        const int64_t max_age = std::numeric_limits<int32_t>::max();
        entry.seen_ms_ago     = static_cast<int32_t>(std::min(keys.seen_ms_ago, max_age));
    }
}

const char *cce_scan_request_name(eCceScanRequest result)
{
    switch (result) {
    case eCceScanRequest::STARTED:
        return "started";
    case eCceScanRequest::HAL_SCAN_ACTIVE:
        return "hal-scan-active";
    case eCceScanRequest::PWHM_SCAN_ACTIVE:
        return "scan-in-progress";
    case eCceScanRequest::READINESS_UNKNOWN:
        return "readiness-unknown";
    case eCceScanRequest::START_FAILED:
        return "start-failed";
    }
    return "unknown";
}

eCceScanRequest request_cce_scan(CceScanBus &bus, bool hal_scan_active)
{
    if (hal_scan_active) {
        return eCceScanRequest::HAL_SCAN_ACTIVE;
    }
    bool in_progress = true;
    if (!bus.read_scan_in_progress(in_progress)) {
        return eCceScanRequest::READINESS_UNKNOWN;
    }
    if (in_progress) {
        return eCceScanRequest::PWHM_SCAN_ACTIVE;
    }
    if (!bus.start_scan(kCceScanReason)) {
        return eCceScanRequest::START_FAILED;
    }
    return eCceScanRequest::STARTED;
}

} // namespace sta_dpp
} // namespace bwl
