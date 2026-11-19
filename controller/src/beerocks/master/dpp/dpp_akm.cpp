/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_akm.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace son {
namespace controller_dpp {
namespace {

struct AkmSelector {
    const char *selector;
    const char *family;
};

// AKM suite selectors (lower case) and the DPP AKM family each one belongs to
// (Wi-Fi Easy Connect v3.0 section 4.3.5, IEEE 802.11 AKM suite selectors).
const AkmSelector k_akm_selectors[] = {
    {"506f9a02", "dpp"},   // DPP
    {"000fac01", "dot1x"}, // 802.1X
    {"000fac03", "dot1x"}, // FT-802.1X
    {"000fac05", "dot1x"}, // 802.1X-SHA256
    {"000fac02", "psk"},   // PSK
    {"000fac04", "psk"},   // FT-PSK
    {"000fac06", "psk"},   // PSK-SHA256
    {"000fac08", "sae"},   // SAE
    {"000fac09", "sae"},   // FT-SAE
    {"000fac18", "sae"},   // SAE-EXT-KEY (AKM 24)
    {"000fac19", "sae"},   // FT-SAE-EXT-KEY (AKM 25)
    // Not an assigned suite type: Wi-Fi EasyMesh v6.1 Figure 9 writes SAE-EXT-KEY
    // (AKM 24) in decimal. Accepted for interop; see dpp_akm_has_easymesh_decimal_alias().
    {"000fac24", "sae"},
};

const char k_easymesh_decimal_sae_ext_key[] = "000fac24";

std::string trim_lower(const std::string &value)
{
    static const char k_whitespace[] = " \t\r\n";
    const auto first                 = value.find_first_not_of(k_whitespace);
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(k_whitespace);
    std::string out = value.substr(first, last - first + 1);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

} // namespace

std::string dpp_akm_token_family(const std::string &token)
{
    std::string lowered = trim_lower(token);
    if (lowered == "dpp" || lowered == "psk" || lowered == "sae" || lowered == "dot1x") {
        return lowered;
    }
    for (const auto &entry : k_akm_selectors) {
        if (lowered == entry.selector) {
            return entry.family;
        }
    }
    return lowered;
}

bool dpp_akm_has_easymesh_decimal_alias(const std::string &akm)
{
    std::stringstream ss(akm);
    std::string token;
    while (std::getline(ss, token, '+')) {
        if (trim_lower(token) == k_easymesh_decimal_sae_ext_key) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> dpp_akm_families(const std::string &akm)
{
    std::vector<std::string> families;
    std::stringstream ss(akm);
    std::string token;
    while (std::getline(ss, token, '+')) {
        auto family = dpp_akm_token_family(token);
        if (!family.empty()) {
            families.push_back(std::move(family));
        }
    }
    std::sort(families.begin(), families.end());
    families.erase(std::unique(families.begin(), families.end()), families.end());
    return families;
}

bool dpp_akm_is_compatible(const std::string &configured_akm, const std::string &requested_akm)
{
    // Empty request = no Enrollee preference (not a constraint).
    if (requested_akm.empty()) {
        return true;
    }
    const auto configured = dpp_akm_families(configured_akm);
    const auto requested  = dpp_akm_families(requested_akm);
    // Non-empty request that yields no tokens (e.g. "++") is incompatible.
    if (requested.empty()) {
        return false;
    }
    // Empty configured AKM is not a constraint (caller may fall back to "dpp").
    if (configured.empty()) {
        return true;
    }
    return std::any_of(configured.begin(), configured.end(),
                       [&requested](const std::string &family) {
                           return std::binary_search(requested.begin(), requested.end(), family);
                       });
}

std::string dpp_akm_to_symbolic(const std::string &akm)
{
    const auto families = dpp_akm_families(akm);
    const auto has      = [&families](const char *family) {
        return std::binary_search(families.begin(), families.end(), std::string(family));
    };
    const bool dpp = has("dpp");
    const bool psk = has("psk");
    const bool sae = has("sae");

    if (dpp && psk && sae) {
        return "dpp+psk+sae";
    }
    if (dpp && sae) {
        return "dpp+sae";
    }
    if (dpp) {
        return "dpp";
    }
    if (psk && sae) {
        return "psk+sae";
    }
    if (sae) {
        return "sae";
    }
    if (psk) {
        return "psk";
    }
    if (has("dot1x")) {
        return "dot1x";
    }
    return {};
}

} // namespace controller_dpp
} // namespace son
