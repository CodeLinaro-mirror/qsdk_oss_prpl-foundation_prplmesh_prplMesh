/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BWL_STA_DPP_ENROLLEE_H_
#define _BWL_STA_DPP_ENROLLEE_H_

#include <bwl/sta_wlan_hal_types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bwl {
namespace sta_dpp {

/** Read-only snapshot for role-driven bootstrap registration. */
struct sPreparationState {
    bool endpoint_enabled = false;
    bool dpp_enabled      = false;
    std::string role;
};

/** One GeneratedQR entry of the endpoint. */
struct sGeneratedQr {
    uint32_t id = 0;
    std::string uri;
};

/** Validation never treats an unavailable read as a confirmed loss. */
enum class eBootstrapValidation { VALID, INVALID, UNAVAILABLE };

/** Owned registration survives HAL reattachment, but not a channel-policy change. */
struct sBootstrapRegistration {
    uint64_t generation = 0;
    std::string chan_list; // URI channel list ("115/36,40,124/149"); empty = none
    uint32_t id = 0;
    std::string uri;
    bool validation_required = true;

    // Returns true only when the requested channel list changed.
    bool observe_attachment(uint64_t attachment_generation, const std::string &list);
};

/** Result of registering the enrollee bootstrap key. */
struct sBootstrapResult {
    bool ok     = false;
    uint32_t id = 0;   ///< Bootstrap ID read from GeneratedQR after the call.
    std::string uri;   ///< URI returned by the call.
    std::string error; ///< Reason on failure. Never contains the key.
};

/** Enrollee progress reported by the EndPoint supplicant. Never carries credentials. */
enum class eEnrolleeEvent {
    NONE,                 ///< Not an Enrollee progress line, or an ignored frame.
    AUTH_REQUEST_RX,      ///< DPP-RX type=0: Auth Request, reported before the own key check.
    AUTH_REQUEST_IGNORED, ///< DPP-FAIL "... - ignore message": the Auth Request was not for us.
    CHIRP_TX,             ///< DPP-TX type=13: Presence Announcement sent, once per chirp channel.
    CHIRP_STOPPED,        ///< DPP-CHIRP-STOPPED: pass done, or stopped by a received Auth Request.
    AUTH_SUCCESS,         ///< DPP-AUTH-SUCCESS
    CONF_RECEIVED,        ///< DPP-CONF-RECEIVED
    FAILED                ///< DPP-CONF-FAILED or any other DPP-FAIL
};

/** Payload of sta_wlan_hal::Event::Dpp_Enrollee_Event. */
struct sEnrolleeEvent {
    eEnrolleeEvent type = eEnrolleeEvent::NONE;
    uint32_t freq       = 0; ///< CHIRP_TX: MHz from DPP-TX freq=, 0 when absent or malformed.
};

/**
 * Classify one EndPoint wpaCtrlEvents line, e.g. "<3>DPP-RX src=.. freq=2437 type=0".
 * The "<N>" level prefix is optional. DPP-RX lines with an "ignore=" token are NONE.
 * DPP-TX is CHIRP_TX only for type=13; other transmitted frames are NONE. For CHIRP_TX,
 * *freq_mhz (when given) is the freq= value in MHz, 0 when absent or malformed; it is 0 for
 * every other result.
 * DPP-FAIL is FAILED unless the supplicant only dropped a received frame: "... - ignore message"
 * (an Auth Request for another bootstrapping key or PKEX peer) is AUTH_REQUEST_IGNORED and
 * "... - ignore new one" (an Auth Request during a running exchange, which goes on) is NONE.
 */
eEnrolleeEvent parse_enrollee_event(const std::string &line, uint32_t *freq_mhz = nullptr);

/**
 * Bus access used by the logic below. WHM implements it over Ambiorix; tests use a mock.
 * It never writes DPP Enable or Role: those are owned by the operator, not the Agent.
 */
class EndpointDppBus {
public:
    virtual ~EndpointDppBus() {}
    virtual bool read_dpp_intent(bool &enable, std::string &role)                 = 0;
    virtual bool call_bootstrap_gen(const std::string &curve, const std::string &chan_list,
                                    const std::string &key_hex, std::string &uri) = 0;
    virtual bool read_generated_qr(std::vector<sGeneratedQr> &entries)            = 0;
};

/**
 * Validate a previously registered ID against GeneratedQR. Never generates an entry.
 * pWHM resets every GeneratedQR ID to 0 when it starts wpa_supplicant and blocks DPP calls
 * until that is done, so the row with our URI still holding our ID means the entry is
 * still in the running supplicant. A read failure or ambiguous rows are UNAVAILABLE.
 */
eBootstrapValidation validate_bootstrap(EndpointDppBus &bus, uint32_t id, const std::string &uri,
                                        std::string &reason);

/**
 * Register the enrollee private key (curve P-256).
 * chan_list is the URI channel list in wpa_supplicant "chan=" form ("115/36,40,124/149");
 * pass an empty string for no list (the URI then has no C: field).
 * The ID is read from GeneratedQR after the call by exact URI match (no instance number).
 * Fails on: empty key, call error, empty or non "DPP:" URI, GeneratedQR read error,
 * no match, matches with different IDs, or ID 0.
 */
bool register_bootstrap(EndpointDppBus &bus, const std::string &private_key_hex,
                        const std::string &chan_list, sBootstrapResult &result);

/** DPP CCE keys of one pWHM scan entry; has_* is false when the key is missing or unreadable. */
struct sCceScanKeys {
    bool has_cce        = false;
    bool cce            = false; ///< DPPCCE
    bool has_cce_known  = false;
    bool cce_known      = false; ///< DPPCCEKnown
    bool has_seen       = false;
    int64_t seen_ms_ago = -1; ///< SeenMsAgo, -1 unknown
};

/**
 * Fill the CCE fields of a scan entry. The entry is known only when DPPCCEKnown is true and
 * DPPCCE is readable; present only when known and DPPCCE is true. A missing or negative age is
 * -1; a larger age than int32_t holds is clamped. Missing keys (older pWHM) mean unknown.
 */
void apply_cce_scan_keys(const sCceScanKeys &keys, sScanResult &entry);

/** pWHM startScan reason of watcher scans, counted in ScanStats.ScanReason. */
const char *const kCceScanReason = "DppCceWatch";

/** Outcome of one watcher scan request. Only STARTED called startScan successfully. */
enum class eCceScanRequest {
    STARTED,
    HAL_SCAN_ACTIVE,   ///< The HAL's own scan runs; the bus was not touched.
    PWHM_SCAN_ACTIVE,  ///< pWHM ScanResults.ScanInProgress is true.
    READINESS_UNKNOWN, ///< ScanInProgress unreadable.
    START_FAILED       ///< startScan call failed.
};
const char *cce_scan_request_name(eCceScanRequest result);

/** Radio scan access for the watcher. WHM implements it over Ambiorix; tests use a mock. */
class CceScanBus {
public:
    virtual ~CceScanBus() {}
    virtual bool read_scan_in_progress(bool &in_progress) = 0;
    virtual bool start_scan(const std::string &reason)    = 0;
};

/**
 * Non-preempting watcher scan request. Refuses while the HAL's own scan is active (no bus
 * access) or pWHM reports a scan in progress or cannot tell; a refusal starts nothing. Never
 * stops a scan (the bus has no stop) and does not track the scan it starts.
 */
eCceScanRequest request_cce_scan(CceScanBus &bus, bool hal_scan_active);

} // namespace sta_dpp
} // namespace bwl

#endif // _BWL_STA_DPP_ENROLLEE_H_
