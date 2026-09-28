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
 * @brief Parse the Controller C-sign private key previously loaded into db from the DPP keystore.
 *
 * Requires db::load_dpp_controller_csign_key_from_store() (or equivalent cache fill) first.
 * Uses parse_ec_private_key_hex() from controller_dpp_protocol (0004).
 *
 * @param[in]  database Controller database with cached csign_key hex
 * @param[out] out      Parsed EC private key on success
 * @param[out] error    Human-readable failure reason
 * @return true on success
 */
bool load_controller_csign_key(db &database, DppKeyPtr &out, std::string &error);

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_CSIGN_H_
