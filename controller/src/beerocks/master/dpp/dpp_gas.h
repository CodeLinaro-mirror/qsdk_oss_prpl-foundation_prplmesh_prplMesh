/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_GAS_H_
#define _DPP_GAS_H_

#include <cstdint>
#include <vector>

namespace son {
namespace controller_dpp {

/**
 * @brief Extract DPP attribute blob from a GAS Initial Request encapsulation.
 *
 * Expected layout (EasyMesh Proxied Encap / hostapd TCP):
 *   [Action 0x0A] Dialog Token | Adv Proto IE (0x6C...) | Query Len (LE16) | DPP attrs
 *
 * @param dialog_token Optional out; set when the Adv Proto layout parses cleanly.
 */
bool extract_dpp_query_from_gas_encap(const std::vector<uint8_t> &encap,
                                      std::vector<uint8_t> &out_dpp_attrs,
                                      uint8_t *dialog_token = nullptr);

/**
 * @brief Wrap a DPP Configuration Response attribute blob in GAS Initial Response (0x0B).
 */
std::vector<uint8_t> wrap_dpp_response_in_gas(const std::vector<uint8_t> &dpp_resp_attrs,
                                              uint8_t dialog_token = 0);

} // namespace controller_dpp
} // namespace son

#endif
