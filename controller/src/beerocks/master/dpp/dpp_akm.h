/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _CONTROLLER_DPP_AKM_H_
#define _CONTROLLER_DPP_AKM_H_

#include <string>
#include <vector>

namespace son {
namespace controller_dpp {

/*
 * DPP Configuration Request / Configuration Object "akm" helpers.
 *
 * Wi-Fi Easy Connect v3.0 section 4.3.5 allows the AKM to be a symbolic name
 * (psk, sae, psk+sae, dpp, dpp+sae, dpp+psk+sae, dot1x) or, from protocol
 * version 2, a '+'-separated list of AKM suite selectors such as
 * "506F9A02+000FAC08" (Wi-Fi EasyMesh v6.1 Figure 9 bSTAList example).
 * These helpers reduce both spellings to AKM families ("dpp", "psk", "sae",
 * "dot1x") so the Controller can compare and publish them consistently.
 */

/**
 * @brief Map one AKM token (symbolic name or suite selector) to its family.
 *
 * Matching is case-insensitive and ignores surrounding whitespace.
 * @return "dpp", "psk", "sae" or "dot1x"; any other token trimmed and lower-cased.
 */
std::string dpp_akm_token_family(const std::string &token);

/**
 * @brief Sorted, de-duplicated AKM families of a '+'-separated AKM string.
 */
std::vector<std::string> dpp_akm_families(const std::string &akm);

/**
 * @brief True when the configured BSS AKM shares at least one AKM family with
 * the AKM requested by the Enrollee. An empty requested string (or empty
 * configured AKM) is not a constraint; a non-empty request with no tokens
 * (e.g. "++") is incompatible.
 */
bool dpp_akm_is_compatible(const std::string &configured_akm, const std::string &requested_akm);

/**
 * @brief Symbolic AKM for an AKM string (symbolic or selector list), using the
 * precedence of hostapd dpp_akm_from_str(): dpp+psk+sae, dpp+sae, dpp,
 * psk+sae, sae, psk, dot1x.
 * @return Empty string when no known AKM family is present.
 */
std::string dpp_akm_to_symbolic(const std::string &akm);

/** True if the AKM string has 000FAC24, the EasyMesh v6.1 decimal form of SAE-EXT-KEY
 *  (000FAC18) that dpp_akm_token_family() maps to "sae". Used to log the alias. */
bool dpp_akm_has_easymesh_decimal_alias(const std::string &akm);

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_AKM_H_
