/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_RELAY_FRAMES_H_
#define _DPP_RELAY_FRAMES_H_

#include <cstddef>
#include <cstdint>

/*
 * DPP-over-TCP relay frame helpers (PPM-4191): pure parsing, no I/O.
 */

namespace beerocks {
namespace dpp_relay {

/** IEEE 802.11 Category Public Action. */
constexpr uint8_t WLAN_ACTION_PUBLIC = 0x04;
/** Public Action field values; also the hostapd TCP frame type byte. */
constexpr uint8_t WLAN_PA_VENDOR_SPECIFIC   = 0x09;
constexpr uint8_t WLAN_PA_GAS_INITIAL_REQ   = 0x0A;
constexpr uint8_t WLAN_PA_GAS_INITIAL_RESP  = 0x0B;
constexpr uint8_t WLAN_PA_GAS_COMEBACK_REQ  = 0x0C;
constexpr uint8_t WLAN_PA_GAS_COMEBACK_RESP = 0x0D;

/** DPP Public Action body: OUI(3) + OUI Type(1) + Crypto Suite(1) + DPP Frame Type(1). */
constexpr size_t DPP_HDR_LEN        = 6;
constexpr size_t DPP_SUBTYPE_OFFSET = 5;

/** DPP Frame Type values (Easy Connect Table 31). */
constexpr uint8_t DPP_PA_AUTHENTICATION_REQ    = 0x00;
constexpr uint8_t DPP_PA_AUTHENTICATION_RESP   = 0x01;
constexpr uint8_t DPP_PA_AUTHENTICATION_CONF   = 0x02;
constexpr uint8_t DPP_PA_CONFIGURATION_RESULT  = 0x0B;
constexpr uint8_t DPP_PA_PRESENCE_ANNOUNCEMENT = 0x0D;

constexpr size_t DPP_BOOTSTRAP_KEY_HASH_LEN = 32;

/**
 * @brief Find the Responder Bootstrapping Key Hash in DPP attributes
 * ([ID LE16][Length LE16][Value] entries).
 *
 * @return false if absent or an attribute overruns attrs_len.
 */
bool find_bootstrap_key_hash(const uint8_t *attrs, size_t attrs_len,
                             uint8_t (&hash)[DPP_BOOTSTRAP_KEY_HASH_LEN]);

/**
 * @brief True for an encapsulated frame that only travels Enrollee -> Controller:
 * GAS Initial/Comeback Request, DPP Authentication Response or Configuration Result.
 *
 * @param frame Encapsulated frame from the Action field (DPP: 0x09 or 0x04 0x09).
 */
bool is_uplink_only_frame(const uint8_t *frame, size_t frame_len);

} // namespace dpp_relay
} // namespace beerocks

#endif // _DPP_RELAY_FRAMES_H_