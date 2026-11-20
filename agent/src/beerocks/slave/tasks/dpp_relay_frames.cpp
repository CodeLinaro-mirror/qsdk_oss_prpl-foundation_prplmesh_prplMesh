/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_relay_frames.h"

#include <tlvf/WEC/eWecDppAttributes.h>

#include <cstring>

namespace beerocks {
namespace dpp_relay {

bool find_bootstrap_key_hash(const uint8_t *attrs, size_t attrs_len,
                             uint8_t (&hash)[DPP_BOOTSTRAP_KEY_HASH_LEN])
{
    if (!attrs) {
        return false;
    }
    // Compare remaining lengths only; never form a pointer past the buffer.
    size_t off = 0;
    while (attrs_len - off >= 4) {
        const uint16_t attr_id  = static_cast<uint16_t>(attrs[off] | (attrs[off + 1] << 8));
        const uint16_t attr_len = static_cast<uint16_t>(attrs[off + 2] | (attrs[off + 3] << 8));
        off += 4;
        if (attr_len > attrs_len - off) {
            return false;
        }
        if (attr_id == WEC::eWecDppAttributes::ATTR_RESPONDER_BOOTSTRAPPING_KEY_HASH &&
            attr_len == DPP_BOOTSTRAP_KEY_HASH_LEN) {
            std::memcpy(hash, attrs + off, DPP_BOOTSTRAP_KEY_HASH_LEN);
            return true;
        }
        off += attr_len;
    }
    return false;
}

bool is_uplink_only_frame(const uint8_t *frame, size_t frame_len)
{
    if (!frame || frame_len == 0) {
        return false;
    }
    if (frame[0] == WLAN_PA_GAS_INITIAL_REQ || frame[0] == WLAN_PA_GAS_COMEBACK_REQ) {
        return true;
    }
    size_t pa_off = 0;
    if (frame[0] == WLAN_ACTION_PUBLIC && frame_len > 1 && frame[1] == WLAN_PA_VENDOR_SPECIFIC) {
        pa_off = 2;
    } else if (frame[0] == WLAN_PA_VENDOR_SPECIFIC) {
        pa_off = 1;
    }
    if (pa_off == 0 || frame_len <= pa_off + DPP_SUBTYPE_OFFSET) {
        return false;
    }
    const uint8_t subtype = frame[pa_off + DPP_SUBTYPE_OFFSET];
    return subtype == DPP_PA_AUTHENTICATION_RESP || subtype == DPP_PA_CONFIGURATION_RESULT;
}

} // namespace dpp_relay
} // namespace beerocks