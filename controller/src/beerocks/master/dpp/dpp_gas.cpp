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

bool extract_dpp_query_from_gas_encap(const std::vector<uint8_t> &encap,
                                      std::vector<uint8_t> &out_dpp_attrs)
{
    if (encap.empty()) {
        return false;
    }

    size_t pos = 0;
    if (encap[0] == 0x0A) { // GAS Initial Request
        pos = 1;
    }

    if (pos + 2 > encap.size()) {
        return false;
    }

    const uint16_t query_len =
        static_cast<uint16_t>(encap[pos] | (encap[pos + 1] << 8));
    pos += 2;

    if (pos + query_len > encap.size()) {
        // Fallback: treat remaining bytes after optional action as attrs.
        out_dpp_attrs.assign(encap.begin() + (encap[0] == 0x0A ? 1 : 0), encap.end());
        return !out_dpp_attrs.empty();
    }

    out_dpp_attrs.assign(encap.begin() + pos, encap.begin() + pos + query_len);
    return true;
}

std::vector<uint8_t> wrap_dpp_response_in_gas(const std::vector<uint8_t> &dpp_resp_attrs)
{
    std::vector<uint8_t> out;
    out.push_back(0x0B); // GAS Initial Response
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x00);
    const uint8_t adv_proto[] = {0x7f, 0xdd, 0x05, 0x50, 0x6f, 0x9a, 0x1a, 0x01};
    out.insert(out.end(), std::begin(adv_proto), std::end(adv_proto));
    const uint16_t len = static_cast<uint16_t>(dpp_resp_attrs.size());
    out.push_back(static_cast<uint8_t>(len & 0xff));
    out.push_back(static_cast<uint8_t>(len >> 8));
    out.insert(out.end(), dpp_resp_attrs.begin(), dpp_resp_attrs.end());
    return out;
}

} // namespace controller_dpp
} // namespace son
