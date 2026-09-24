/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */


#include "controller_dpp_protocol.h"
#include "dpp_internal.h"

#include <bcl/beerocks_string_utils.h>
#include <mapf/common/encryption.h>

#include <json-c/json.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>

namespace son {
namespace controller_dpp {

void append_le16(std::vector<uint8_t> &out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void append_dpp_attr(std::vector<uint8_t> &out, uint16_t attr_id, const uint8_t *data,
                     size_t len)
{
    append_le16(out, attr_id);
    append_le16(out, static_cast<uint16_t>(len));
    if (data && len > 0) {
        out.insert(out.end(), data, data + len);
    }
}

void append_dpp_attr_u8(std::vector<uint8_t> &out, uint16_t attr_id, uint8_t value)
{
    append_dpp_attr(out, attr_id, &value, 1);
}

bool get_dpp_attr(const uint8_t *attrs, size_t len, uint16_t attr_id,
                  DppAttributeView &attr)
{
    attr = {};
    if (!attrs) {
        return false;
    }

    size_t offset = 0;
    while (offset + 4 <= len) {
        const uint16_t id =
            static_cast<uint16_t>(attrs[offset] | (attrs[offset + 1] << 8));
        const uint16_t attr_len =
            static_cast<uint16_t>(attrs[offset + 2] | (attrs[offset + 3] << 8));
        const auto *header = attrs + offset;
        offset += 4;
        if (offset + attr_len > len) {
            return false;
        }

        if (id == attr_id) {
            attr.data   = attrs + offset;
            attr.len    = attr_len;
            attr.header = header;
            return true;
        }
        offset += attr_len;
    }

    return false;
}

bool split_dpp_public_action_frame(const std::vector<uint8_t> &frame, uint8_t expected_type,
                                   const uint8_t *&dpp_header, const uint8_t *&attrs,
                                   size_t &attrs_len)
{
    dpp_header = nullptr;
    attrs      = nullptr;
    attrs_len  = 0;
    if (frame.size() < 6) {
        return false;
    }

    size_t pos = 0;
    if (frame.size() >= 8 && frame[0] == k_dpp_public_action_category &&
        frame[1] == k_dpp_public_action_vendor) {
        pos = 2;
    }

    if (frame.size() < pos + 6 || frame[pos] != k_dpp_oui[0] ||
        frame[pos + 1] != k_dpp_oui[1] || frame[pos + 2] != k_dpp_oui[2] ||
        frame[pos + 3] != k_dpp_oui_type || frame[pos + 4] != k_dpp_crypto_suite ||
        frame[pos + 5] != expected_type) {
        return false;
    }

    dpp_header = frame.data() + pos;
    attrs      = frame.data() + pos + 6;
    attrs_len  = frame.size() - pos - 6;
    return true;
}

void build_dpp_public_action_prefix(uint8_t frame_type, std::vector<uint8_t> &frame)
{
    frame.clear();
    frame.push_back(k_dpp_public_action_category);
    frame.push_back(k_dpp_public_action_vendor);
    frame.insert(frame.end(), k_dpp_oui, k_dpp_oui + sizeof(k_dpp_oui));
    frame.push_back(k_dpp_oui_type);
    frame.push_back(k_dpp_crypto_suite);
    frame.push_back(frame_type);
}

bool parse_required_attr(const uint8_t *attrs, size_t attrs_len, uint16_t attr_id,
                         size_t expected_len, DppAttributeView &attr)
{
    return get_dpp_attr(attrs, attrs_len, attr_id, attr) && attr.data &&
           (expected_len == 0 || attr.len == expected_len);
}

bool extract_dpp_attribute(const uint8_t *attrs, size_t len, uint16_t attr_id, std::string &out)
{
    out.clear();
    if (!attrs || len < 4) {
        return false;
    }

    size_t offset = 0;
    while (offset + 4 <= len) {
        const uint16_t id       = uint16_t(attrs[offset]) | (uint16_t(attrs[offset + 1]) << 8);
        const uint16_t attr_len = uint16_t(attrs[offset + 2]) | (uint16_t(attrs[offset + 3]) << 8);
        offset += 4;
        if (offset + attr_len > len) {
            return false;
        }
        if (id == attr_id) {
            out.assign(reinterpret_cast<const char *>(attrs + offset), attr_len);
            return true;
        }
        offset += attr_len;
    }

    return false;
}

bool find_dpp_public_action_attributes(const uint8_t *frame, size_t len, uint8_t &frame_type,
                                       size_t &attrs_offset)
{
    frame_type   = 0;
    attrs_offset = 0;
    if (!frame || len < 6) {
        return false;
    }

    size_t pos = 0;
    if (len >= 8 && frame[0] == 0x04 && frame[1] == 0x09) {
        pos = 2;
    }

    if (len < pos + 6) {
        return false;
    }

    if (frame[pos] != 0x50 || frame[pos + 1] != 0x6f || frame[pos + 2] != 0x9a ||
        frame[pos + 3] != 0x1a) {
        return false;
    }

    frame_type   = frame[pos + 5];
    attrs_offset = pos + 6;
    return attrs_offset <= len;
}

} // namespace controller_dpp
} // namespace son
