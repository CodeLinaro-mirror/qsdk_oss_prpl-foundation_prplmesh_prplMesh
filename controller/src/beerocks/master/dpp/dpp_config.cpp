/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "controller_dpp_protocol.h"
#include "dpp_internal.h"

#include <bcl/beerocks_string_utils.h>
#include <mapf/common/encryption.h>

#include <json-c/json.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>

namespace son {
namespace controller_dpp {

bool add_discovery_ssid(json_object *discovery_obj, const std::string &ssid_hex, std::string &error)
{
    if (!discovery_obj) {
        error = "Missing discovery object";
        return false;
    }
    if (!is_hex_string(ssid_hex)) {
        error = "Invalid SSID hex";
        return false;
    }

    auto ssid_bytes = beerocks::string_utils::hex_to_bytes<std::vector<uint8_t>>(ssid_hex);
    if (ssid_bytes.empty()) {
        error = "Missing SSID";
        return false;
    }

    const bool printable_ascii = std::all_of(ssid_bytes.begin(), ssid_bytes.end(),
                                             [](uint8_t ch) { return ch >= 0x20 && ch <= 0x7e; });

    if (printable_ascii) {
        std::string ssid(ssid_bytes.begin(), ssid_bytes.end());
        json_object_object_add(discovery_obj, "ssid",
                               json_object_new_string_len(ssid.c_str(), ssid.size()));
        return true;
    }

    std::string ssid64 = base64url_encode(ssid_bytes.data(), ssid_bytes.size());
    if (ssid64.empty()) {
        error = "Failed encoding SSID";
        return false;
    }

    json_object_object_add(discovery_obj, "ssid64", json_object_new_string(ssid64.c_str()));
    return true;
}

bool build_dpp_configuration_object(DppKey *csign_key, const std::string &pp_key_hex,
                                    const std::string &connector_payload_json,
                                    const std::string &ssid_hex,
                                    const std::string &expected_net_role,
                                    const std::string &expected_group_id,
                                    const DppConfigurationObjectOptions &options,
                                    std::string &config_object_json, std::string &error)
{
    config_object_json.clear();
    error.clear();

    if (!csign_key) {
        error = "Missing C-sign key";
        return false;
    }
    if (connector_payload_json.empty()) {
        error = "Missing connector payload";
        return false;
    }

    DppConnectorPayload payload;
    if (!parse_dpp_connector_payload(connector_payload_json, payload, error)) {
        return false;
    }

    std::string normalized_role;
    if (!normalize_dpp_netrole(payload.net_role, normalized_role)) {
        error = "Unsupported connector netRole";
        return false;
    }

    std::string normalized_expected_role;
    if (!expected_net_role.empty() &&
        (!normalize_dpp_netrole(expected_net_role, normalized_expected_role) ||
         normalized_role != normalized_expected_role)) {
        error = "Connector netRole mismatch";
        return false;
    }

    if (!expected_group_id.empty() && payload.group_id != expected_group_id) {
        error = "Connector groupId mismatch";
        return false;
    }

    std::string connector;
    std::string csign_pub_hex;
    std::string kid;
    if (!build_signed_connector_from_payload(csign_key, connector_payload_json, connector,
                                             csign_pub_hex, kid, error)) {
        return false;
    }

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> csign_pub(nullptr, EVP_PKEY_free);
    if (!parse_ec_pubkey_hex(csign_pub_hex, csign_pub)) {
        error = "Invalid C-sign public key";
        return false;
    }

    json_object *root = json_object_new_object();
    json_object *cred = json_object_new_object();

    const bool map_role = normalized_role.rfind("map", 0) == 0;
    json_object_object_add(root, "wi-fi_tech", json_object_new_string(map_role ? "map" : "infra"));
    if (options.include_net_role) {
        const auto &role = options.net_role.empty() ? normalized_role : options.net_role;
        json_object_object_add(root, "netRole", json_object_new_string(role.c_str()));
    }
    if (options.include_df_counter_threshold) {
        json_object_object_add(root, "dfCounterThreshold",
                               json_object_new_int(options.df_counter_threshold));
    }

    if (options.include_discovery) {
        json_object *discovery = json_object_new_object();
        if (!add_discovery_ssid(discovery, ssid_hex, error)) {
            json_object_put(root);
            json_object_put(discovery);
            json_object_put(cred);
            return false;
        }
        json_object_object_add(root, "discovery", discovery);
    }

    const auto &akm = options.akm.empty() ? std::string("dpp") : options.akm;
    json_object_object_add(cred, "akm", json_object_new_string(akm.c_str()));
    json_object_object_add(cred, "signedConnector", json_object_new_string(connector.c_str()));
    if (!add_public_jwk(cred, "csign", csign_pub.get(), kid, error)) {
        json_object_put(root);
        json_object_put(cred);
        return false;
    }

    if (!pp_key_hex.empty()) {
        std::string pp_pub_hex;
        if (!derive_pubkey_hex_from_private(pp_key_hex, pp_pub_hex)) {
            json_object_put(root);
            json_object_put(cred);
            error = "Invalid ppKey";
            return false;
        }

        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pp_pub(nullptr, EVP_PKEY_free);
        if (!parse_ec_pubkey_hex(pp_pub_hex, pp_pub) ||
            !add_public_jwk(cred, "ppKey", pp_pub.get(), {}, error)) {
            json_object_put(root);
            json_object_put(cred);
            if (error.empty()) {
                error = "Invalid ppKey";
            }
            return false;
        }
    }

    if (!options.psk_hex.empty()) {
        json_object_object_add(cred, "psk_hex", json_object_new_string(options.psk_hex.c_str()));
    } else if (!options.passphrase.empty()) {
        json_object_object_add(cred, "pass", json_object_new_string(options.passphrase.c_str()));
    }
    if (!options.security_ies_hex.empty()) {
        json_object_object_add(cred, "SecurityIEs",
                               json_object_new_string(options.security_ies_hex.c_str()));
    }
    if (options.ssid_advertisement_valid) {
        json_object_object_add(cred, "SSIDAdvertisement",
                               json_object_new_boolean(options.ssid_advertisement));
    }

    json_object_object_add(root, "cred", cred);
    config_object_json = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    json_object_put(root);
    return !config_object_json.empty();
}

bool build_dpp_configuration_object(DppKey *csign_key, const std::string &pp_key_hex,
                                    const std::string &connector_payload_json,
                                    const std::string &ssid_hex,
                                    const std::string &expected_net_role,
                                    const std::string &expected_group_id,
                                    std::string &config_object_json, std::string &error)
{
    DppConfigurationObjectOptions options;
    return build_dpp_configuration_object(csign_key, pp_key_hex, connector_payload_json, ssid_hex,
                                          expected_net_role, expected_group_id, options,
                                          config_object_json, error);
}

bool parse_dpp_config_request_objects(const std::string &json_blob,
                                      std::vector<std::string> &net_roles,
                                      std::string *backhaul_akm, bool *bsta_max_links,
                                      std::unordered_set<std::string> *bsta_ruids)
{
    net_roles.clear();
    if (backhaul_akm) {
        backhaul_akm->clear();
    }
    if (bsta_max_links) {
        *bsta_max_links = false;
    }
    if (bsta_ruids) {
        bsta_ruids->clear();
    }

    if (json_blob.empty()) {
        return true;
    }

    json_object *root = json_tokener_parse(json_blob.c_str());
    if (!root) {
        return false;
    }

    auto parse_obj = [&](json_object *obj) {
        if (!obj || json_object_get_type(obj) != json_type_object) {
            return;
        }

        json_object *net_role_obj = nullptr;
        if (json_object_object_get_ex(obj, "netRole", &net_role_obj) &&
            json_object_get_type(net_role_obj) == json_type_string) {
            net_roles.emplace_back(json_object_get_string(net_role_obj));
        }

        json_object *bsta_list = nullptr;
        if (!json_object_object_get_ex(obj, "bSTAList", &bsta_list)) {
            return;
        }

        auto parse_bsta = [&](json_object *bsta_obj) {
            if (!bsta_obj || json_object_get_type(bsta_obj) != json_type_object) {
                return;
            }
            json_object *bsta_role = nullptr;
            if (json_object_object_get_ex(bsta_obj, "netRole", &bsta_role) &&
                json_object_get_type(bsta_role) == json_type_string) {
                net_roles.emplace_back(json_object_get_string(bsta_role));
            }

            json_object *akm_obj = nullptr;
            if (backhaul_akm && backhaul_akm->empty() &&
                json_object_object_get_ex(bsta_obj, "akm", &akm_obj) &&
                json_object_get_type(akm_obj) == json_type_string) {
                *backhaul_akm = json_object_get_string(akm_obj);
            }

            json_object *max_links = nullptr;
            if (bsta_max_links &&
                json_object_object_get_ex(bsta_obj, "bSTA_Maximum_Links", &max_links) &&
                json_object_get_type(max_links) == json_type_int) {
                if (json_object_get_int(max_links) > 0) {
                    *bsta_max_links = true;
                }
            }

            json_object *radio_list = nullptr;
            if (bsta_ruids && json_object_object_get_ex(bsta_obj, "RadioList", &radio_list) &&
                json_object_get_type(radio_list) == json_type_array) {
                const int len = json_object_array_length(radio_list);
                for (int i = 0; i < len; ++i) {
                    auto *radio = json_object_array_get_idx(radio_list, i);
                    if (!radio || json_object_get_type(radio) != json_type_object) {
                        continue;
                    }
                    json_object *ruid_obj = nullptr;
                    if (json_object_object_get_ex(radio, "RUID", &ruid_obj) &&
                        json_object_get_type(ruid_obj) == json_type_string) {
                        bsta_ruids->insert(json_object_get_string(ruid_obj));
                    }
                }
            }
        };

        if (json_object_get_type(bsta_list) == json_type_array) {
            const int len = json_object_array_length(bsta_list);
            for (int i = 0; i < len; ++i) {
                parse_bsta(json_object_array_get_idx(bsta_list, i));
            }
        } else {
            parse_bsta(bsta_list);
        }
    };

    if (json_object_get_type(root) == json_type_array) {
        const int len = json_object_array_length(root);
        for (int i = 0; i < len; ++i) {
            parse_obj(json_object_array_get_idx(root, i));
        }
    } else {
        parse_obj(root);
    }

    json_object_put(root);

    std::sort(net_roles.begin(), net_roles.end());
    net_roles.erase(std::unique(net_roles.begin(), net_roles.end()), net_roles.end());
    return true;
}

} // namespace controller_dpp
} // namespace son
