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
 * Proxied Encap GAS frames often carry action=0x0A (GAS Initial Request) plus
 * query length and DPP attributes. Existing unwrap_configuration_request()
 * expects the DPP attribute region (or a Public Action frame).
 */
bool extract_dpp_query_from_gas_encap(const std::vector<uint8_t> &encap,
                                      std::vector<uint8_t> &out_dpp_attrs);

/**
 * @brief Wrap a DPP Configuration Response attribute blob in GAS Initial Response (0x0B).
 */
std::vector<uint8_t> wrap_dpp_response_in_gas(const std::vector<uint8_t> &dpp_resp_attrs);

} // namespace controller_dpp
} // namespace son

#endif
