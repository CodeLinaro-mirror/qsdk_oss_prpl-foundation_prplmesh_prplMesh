/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _CONTROLLER_DPP_CSIGN_H_
#define _CONTROLLER_DPP_CSIGN_H_

#include "controller_dpp_protocol.h"

#include <string>

namespace son {

class db;

namespace controller_dpp {

/**
 * @brief Parse the DPP Configurator C-sign private key loaded into db from the DPP keystore.
 *
 * Requires db::load_dpp_configurator_csign_key_from_store() (or equivalent cache fill) first.
 * Uses parse_ec_private_key_hex() from controller_dpp_protocol (0004).
 *
 * @param[in]  database Controller database with cached csign_key hex
 * @param[out] out      Parsed EC private key on success
 * @param[out] error    Human-readable failure reason
 * @return true on success
 */
bool load_dpp_configurator_csign_key(const db &database, DppKeyPtr &out, std::string &error);

/**
 * @brief Generate a P-256 DPP Configurator C-sign key into the DPP keystore if none is stored.
 *
 * Does nothing when [dpp_controller_keys] already has a non-empty csign_key (an existing key
 * is never replaced). Otherwise fills the empty csign_key= line in [dpp_controller_keys]
 * (or adds the section at the end), writes <file>.tmp and renames it over the keystore file,
 * then checks the key reads back.
 * Call before db::load_dpp_configurator_csign_key_from_store().
 *
 * @param[in]  database  Controller database (keystore path and parser)
 * @param[out] generated true if a new key was written
 * @param[out] error     Failure reason (never contains key material)
 * @return true if a key is present or was generated
 */
bool generate_dpp_configurator_csign_key(const db &database, bool &generated, std::string &error);

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_CSIGN_H_
