/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "controller_dpp_csign.h"

#include "db/db.h"

namespace son {
namespace controller_dpp {

bool load_controller_csign_key(const db &database, DppKeyPtr &out, std::string &error)
{
    out.reset(nullptr);
    error.clear();

    std::string key_hex;
    if (!database.get_dpp_controller_csign_key_hex(key_hex)) {
        // Fall back to reading the keystore file directly (same layout as startup load).
        db::DppStoreSections sections;
        if (!database.get_dpp_store_sections(sections)) {
            error = "Failed reading DPP keystore";
            return false;
        }
        auto sec_it = sections.find("dpp_controller_keys");
        if (sec_it == sections.end()) {
            error = "Missing [dpp_controller_keys] in DPP keystore";
            return false;
        }
        auto key_it = sec_it->second.find("csign_key");
        if (key_it == sec_it->second.end() || key_it->second.empty()) {
            error = "Missing csign_key in DPP keystore";
            return false;
        }
        key_hex = key_it->second;
    }

    if (!parse_ec_private_key_hex(key_hex, out)) {
        error = "Invalid C-sign private key (expected EC private key DER hex)";
        return false;
    }

    return true;
}

} // namespace controller_dpp
} // namespace son
