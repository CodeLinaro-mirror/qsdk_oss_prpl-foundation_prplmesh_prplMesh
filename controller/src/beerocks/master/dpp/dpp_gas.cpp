/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_gas.h"

#include <iterator>

namespace son {
namespace controller_dpp {

namespace {

constexpr uint8_t k_gas_initial_request  = 0x0A;
constexpr uint8_t k_gas_initial_response = 0x0B;
constexpr uint8_t k_adv_proto_element_id = 0x6C; // Advertisement Protocol

// WFA DPP Advertisement Protocol Tuple body (Query Response Info length + OUI + type).
constexpr uint8_t k_dpp_adv_proto_body[] = {0x7f, 0xdd, 0x05, 0x50, 0x6f, 0x9a, 0x1a, 0x01};

bool parse_gas_initial_request(const std::vector<uint8_t> &encap, uint8_t &dialog_token,
                               size_t &query_offset, size_t &query_len)
{
    size_t pos = 0;
    if (!encap.empty() && encap[0] == k_gas_initial_request) {
        pos = 1;
    }

    // Dialog Token (1) + Advertisement Protocol element (2 + len) + Query Request Length (2).
    if (pos + 1 + 2 + 2 > encap.size()) {
        return false;
    }

    dialog_token = encap[pos++];

    if (encap[pos] != k_adv_proto_element_id) {
        return false;
    }
    const uint8_t adv_len = encap[pos + 1];
    pos += 2;
    if (pos + adv_len + 2 > encap.size()) {
        return false;
    }
    pos += adv_len;

    query_len = static_cast<size_t>(encap[pos] | (static_cast<uint16_t>(encap[pos + 1]) << 8));
    pos += 2;
    if (pos + query_len > encap.size()) {
        return false;
    }

    query_offset = pos;
    return true;
}

} // namespace

bool extract_dpp_query_from_gas_encap(const std::vector<uint8_t> &encap,
                                      std::vector<uint8_t> &out_dpp_attrs, uint8_t *dialog_token)
{
    out_dpp_attrs.clear();
    if (encap.empty()) {
        return false;
    }

    uint8_t token       = 0;
    size_t query_offset = 0;
    size_t query_len    = 0;
    if (parse_gas_initial_request(encap, token, query_offset, query_len)) {
        if (dialog_token) {
            *dialog_token = token;
        }
        out_dpp_attrs.assign(encap.begin() + static_cast<std::ptrdiff_t>(query_offset),
                             encap.begin() + static_cast<std::ptrdiff_t>(query_offset + query_len));
        return !out_dpp_attrs.empty();
    }

    // Legacy / malformed fallback: optional Action byte, then treat the rest as attrs.
    // Do not invent a query-length from Dialog Token + Element ID (previous bug).
    size_t start = (encap[0] == k_gas_initial_request) ? 1 : 0;
    if (start >= encap.size()) {
        return false;
    }
    if (dialog_token) {
        *dialog_token = 0;
    }
    out_dpp_attrs.assign(encap.begin() + static_cast<std::ptrdiff_t>(start), encap.end());
    return !out_dpp_attrs.empty();
}

std::vector<uint8_t> wrap_dpp_response_in_gas(const std::vector<uint8_t> &dpp_resp_attrs,
                                              uint8_t dialog_token)
{
    // GAS Initial Response:
    // Action(0x0B) | Dialog Token | Status(2) | Comeback Delay(2) |
    // Advertisement Protocol IE | Query Response Length(2) | Query Response
    std::vector<uint8_t> out;
    out.push_back(k_gas_initial_response);
    out.push_back(dialog_token);
    out.push_back(0x00); // Status Code
    out.push_back(0x00);
    out.push_back(0x00); // GAS Comeback Delay
    out.push_back(0x00);
    out.push_back(k_adv_proto_element_id);
    out.push_back(static_cast<uint8_t>(sizeof(k_dpp_adv_proto_body)));
    out.insert(out.end(), std::begin(k_dpp_adv_proto_body), std::end(k_dpp_adv_proto_body));
    const uint16_t len = static_cast<uint16_t>(dpp_resp_attrs.size());
    out.push_back(static_cast<uint8_t>(len & 0xff));
    out.push_back(static_cast<uint8_t>(len >> 8));
    out.insert(out.end(), dpp_resp_attrs.begin(), dpp_resp_attrs.end());
    return out;
}

} // namespace controller_dpp
} // namespace son
