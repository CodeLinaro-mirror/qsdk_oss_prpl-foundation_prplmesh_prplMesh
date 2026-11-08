/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "controller_dpp_csign.h"

#include "db/db.h"
#include "dpp/dpp_internal.h"

#include <bcl/beerocks_os_utils.h>
#include <bcl/beerocks_string_utils.h>
#include <openssl/obj_mac.h>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace son {
namespace controller_dpp {

namespace {

const std::string k_dpp_controller_keys_section = "dpp_controller_keys";
const std::string k_dpp_csign_key               = "csign_key";

// Best-effort cleanup of the temp file; ENOENT means there is nothing to clean up.
void remove_tmp_file(const std::string &tmp_path, std::string &error)
{
    if (std::remove(tmp_path.c_str()) != 0 && errno != ENOENT) {
        error += "; also failed to remove " + tmp_path;
    }
}

// Reads the whole file; a missing file reads as empty.
bool read_store_file(const std::string &path, std::string &content)
{
    content.clear();
    if (!beerocks::os_utils::file_exists(path)) {
        return true;
    }
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return !file.bad();
}

// Same pattern as base_wlan_hal_dummy.cpp: write "<path>.tmp", close, then rename over path.
bool write_store_file(const std::string &path, const std::string &content, std::string &error)
{
    const std::string tmp_path = path + ".tmp";
    std::ofstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
    file << content;
    file.close();
    if (!file) {
        error = "failed writing " + tmp_path;
        remove_tmp_file(tmp_path, error);
        return false;
    }
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        error = "failed to rename " + tmp_path + " to " + path;
        remove_tmp_file(tmp_path, error);
        return false;
    }
    return true;
}

// Fills empty "csign_key=" lines inside [dpp_controller_keys]; if there are none, adds the
// section at the end. Header/key rules match db.cpp parse_dpp_store_file().
std::string with_csign_key(const std::string &content, const std::string &key_hex)
{
    std::istringstream in(content);
    std::string out;
    std::string line;
    std::string section;
    bool filled = false;
    while (std::getline(in, line)) {
        std::string trimmed = line;
        beerocks::string_utils::trim(trimmed);
        const auto pos = trimmed.find('=');
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed.substr(1, trimmed.size() - 2);
        } else if (section == "dpp_controller_keys" && pos != std::string::npos) {
            std::string key   = trimmed.substr(0, pos);
            std::string value = trimmed.substr(pos + 1);
            beerocks::string_utils::trim(key);
            beerocks::string_utils::trim(value);
            if (key == "csign_key" && value.empty()) {
                line   = "csign_key=" + key_hex;
                filled = true;
            }
        }
        out += line + "\n";
    }
    if (!filled) {
        out += out.empty() ? "" : "\n";
        out += "[dpp_controller_keys]\ncsign_key=" + key_hex + "\n";
    }
    return out;
}

} // namespace

bool load_dpp_configurator_csign_key(const db &database, DppKeyPtr &out, std::string &error)
{
    out.reset(nullptr);
    error.clear();

    std::string key_hex;
    if (!database.get_dpp_configurator_csign_key_hex(key_hex)) {
        // Fall back to reading the keystore file directly (same layout as startup load).
        db::DppStoreSections sections;
        if (!database.get_dpp_store_sections(sections)) {
            error = "Failed reading DPP keystore";
            return false;
        }
        auto sec_it = sections.find(k_dpp_controller_keys_section);
        if (sec_it == sections.end()) {
            error = "Missing [dpp_controller_keys] in DPP keystore";
            return false;
        }
        auto key_it = sec_it->second.find(k_dpp_csign_key);
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

bool generate_dpp_configurator_csign_key(const db &database, bool &generated, std::string &error)
{
    generated = false;
    error.clear();

    // Missing/empty file just means "no key yet".
    db::DppStoreSections sections;
    database.get_dpp_store_sections(sections);
    if (!sections["dpp_controller_keys"]["csign_key"].empty()) {
        return true;
    }

    const auto path = db::get_dpp_store_path();
    std::string content;
    if (!read_store_file(path, content)) {
        error = "cannot read DPP keystore " + path;
        return false;
    }

    DppKeyPtr key;
    DppKeyPtr decoded;
    std::string key_hex;
    if (!generate_ec_key(NID_X9_62_prime256v1, key) ||
        !encode_ec_private_key_hex(key.get(), key_hex) ||
        !parse_ec_private_key_hex(key_hex, decoded)) {
        error = "P-256 C-sign key generation failed";
        return false;
    }

    // All other lines are kept as they are.
    if (!write_store_file(path, with_csign_key(content, key_hex), error)) {
        return false;
    }

    sections.clear();
    database.get_dpp_store_sections(sections);
    if (sections["dpp_controller_keys"]["csign_key"] != key_hex) {
        error = "csign_key read back from " + path + " does not match the generated key";
        return false;
    }

    generated = true;
    return true;
}

} // namespace controller_dpp
} // namespace son
