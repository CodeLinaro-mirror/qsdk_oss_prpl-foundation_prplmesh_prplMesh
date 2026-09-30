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

bool normalize_dpp_netrole(const std::string &input, std::string &normalized)
{
    if (beerocks::string_utils::case_insensitive_compare(input, "sta")) {
        normalized = "sta";
        return true;
    }
    if (beerocks::string_utils::case_insensitive_compare(input, "ap")) {
        normalized = "ap";
        return true;
    }
    if (beerocks::string_utils::case_insensitive_compare(input, "mapBackhaulSta")) {
        normalized = "mapBackhaulSta";
        return true;
    }
    if (beerocks::string_utils::case_insensitive_compare(input, "mapBackhaulBss")) {
        normalized = "mapBackhaulBss";
        return true;
    }
    if (beerocks::string_utils::case_insensitive_compare(input, "mapAgent")) {
        normalized = "mapAgent";
        return true;
    }
    if (beerocks::string_utils::case_insensitive_compare(input, "mapController")) {
        normalized = "mapController";
        return true;
    }
    if (beerocks::string_utils::case_insensitive_compare(input, "configurator")) {
        normalized = "configurator";
        return true;
    }
    return false;
}

bool parse_dpp_connector_payload(const std::string &payload_json, DppConnectorPayload &out,
                                 std::string &error)
{
    out               = {};
    json_object *root = json_tokener_parse(payload_json.c_str());
    if (!root) {
        error = "Invalid connector payload JSON";
        return false;
    }

    json_object *groups_obj = nullptr;
    if (!json_object_object_get_ex(root, "groups", &groups_obj)) {
        json_object_put(root);
        error = "Missing connector groups";
        return false;
    }

    auto parse_group = [&](json_object *group_obj) -> bool {
        if (!group_obj || json_object_get_type(group_obj) != json_type_object) {
            return false;
        }
        json_object *group_id_obj = nullptr;
        json_object *role_obj     = nullptr;
        if (!json_object_object_get_ex(group_obj, "groupId", &group_id_obj) ||
            json_object_get_type(group_id_obj) != json_type_string ||
            !json_object_object_get_ex(group_obj, "netRole", &role_obj) ||
            json_object_get_type(role_obj) != json_type_string) {
            return false;
        }
        out.group_id = json_object_get_string(group_id_obj);
        out.net_role = json_object_get_string(role_obj);
        return !out.net_role.empty() && !out.group_id.empty();
    };

    bool group_found = false;
    if (json_object_get_type(groups_obj) == json_type_array) {
        const int len = json_object_array_length(groups_obj);
        for (int i = 0; i < len; ++i) {
            if (parse_group(json_object_array_get_idx(groups_obj, i))) {
                group_found = true;
                break;
            }
        }
    } else {
        group_found = parse_group(groups_obj);
    }

    if (!group_found) {
        json_object_put(root);
        error = "Missing connector groupId/netRole";
        return false;
    }

    json_object *netaccess_obj = nullptr;
    if (!json_object_object_get_ex(root, "netAccessKey", &netaccess_obj) ||
        json_object_get_type(netaccess_obj) != json_type_object) {
        json_object_put(root);
        error = "Missing connector netAccessKey";
        return false;
    }

    if (!jwk_pubkey_to_der_hex(netaccess_obj, out.netaccess_jwk_der_hex)) {
        json_object_put(root);
        error = "Invalid connector netAccessKey";
        return false;
    }

    json_object *expiry_obj = nullptr;
    if (json_object_object_get_ex(root, "expiry", &expiry_obj) &&
        json_object_get_type(expiry_obj) == json_type_string) {
        const std::string expiry_str = json_object_get_string(expiry_obj);
        if (!parse_rfc3339_utc(expiry_str, out.expiry)) {
            json_object_put(root);
            error = "Invalid connector expiry format";
            return false;
        }
    }

    json_object_put(root);
    return true;
}

bool build_signed_connector_from_payload(DppKey *csign_key, const std::string &payload_json,
                                         std::string &connector, std::string &csign_pub_hex,
                                         std::string &kid, std::string &error)
{
    connector.clear();
    kid.clear();
    error.clear();

    if (!csign_key) {
        error = "Missing C-sign key";
        return false;
    }

    DppConnectorPayload payload;
    if (!parse_dpp_connector_payload(payload_json, payload, error)) {
        return false;
    }

    if (csign_pub_hex.empty()) {
        int len = i2d_PUBKEY(csign_key, nullptr);
        if (len <= 0) {
            error = "Failed encoding C-sign public key";
            return false;
        }

        std::vector<uint8_t> der(static_cast<size_t>(len));
        unsigned char *ptr = der.data();
        if (i2d_PUBKEY(csign_key, &ptr) != len) {
            error = "Failed encoding C-sign public key";
            return false;
        }
        csign_pub_hex = bytes_to_hex_string(der.data(), der.size());
    }

    if (!compute_csign_kid(csign_pub_hex, kid)) {
        error = "Failed computing connector kid";
        return false;
    }

    std::string alg;
    const EVP_MD *md    = nullptr;
    size_t sig_part_len = 0;
    if (!ec_key_info(csign_key, alg, md, sig_part_len)) {
        error = "Unsupported C-sign key";
        return false;
    }

    json_object *header = json_object_new_object();
    json_object_object_add(header, "typ", json_object_new_string("dppCon"));
    json_object_object_add(header, "alg", json_object_new_string(alg.c_str()));
    json_object_object_add(header, "kid", json_object_new_string(kid.c_str()));

    const char *header_str = json_object_to_json_string_ext(header, JSON_C_TO_STRING_PLAIN);
    std::string header_b64 =
        base64url_encode(reinterpret_cast<const uint8_t *>(header_str), std::strlen(header_str));
    std::string payload_b64 = base64url_encode(
        reinterpret_cast<const uint8_t *>(payload_json.data()), payload_json.size());
    json_object_put(header);

    if (header_b64.empty() || payload_b64.empty()) {
        error = "Failed encoding connector";
        return false;
    }

    std::string signature_raw;
    if (!jws_sign(csign_key, md, sig_part_len, header_b64 + "." + payload_b64, signature_raw,
                  error)) {
        return false;
    }

    std::string sig_b64 = base64url_encode(reinterpret_cast<const uint8_t *>(signature_raw.data()),
                                           signature_raw.size());
    if (sig_b64.empty()) {
        error = "Failed encoding connector signature";
        return false;
    }

    connector = header_b64 + "." + payload_b64 + "." + sig_b64;
    return true;
}

bool extract_dpp_netrole(const std::string &connector, std::string &net_role)
{
    net_role.clear();
    auto first_dot = connector.find('.');
    if (first_dot == std::string::npos) {
        return false;
    }
    auto second_dot = connector.find('.', first_dot + 1);
    if (second_dot == std::string::npos || second_dot <= first_dot + 1) {
        return false;
    }

    auto payload_b64 = connector.substr(first_dot + 1, second_dot - first_dot - 1);
    std::string payload;
    if (!beerocks::string_utils::base64_url_decode(payload_b64, payload)) {
        return false;
    }

    json_object *root = json_tokener_parse(payload.c_str());
    if (!root) {
        return false;
    }

    json_object *groups_obj = nullptr;
    if (!json_object_object_get_ex(root, "groups", &groups_obj)) {
        json_object_put(root);
        return false;
    }

    auto extract_role_from_group = [&](json_object *group_obj) -> bool {
        if (!group_obj || json_object_get_type(group_obj) != json_type_object) {
            return false;
        }
        json_object *role_obj = nullptr;
        if (!json_object_object_get_ex(group_obj, "netRole", &role_obj) ||
            json_object_get_type(role_obj) != json_type_string) {
            return false;
        }
        net_role = json_object_get_string(role_obj);
        return !net_role.empty();
    };

    bool found = false;
    if (json_object_get_type(groups_obj) == json_type_array) {
        const int len = json_object_array_length(groups_obj);
        for (int i = 0; i < len; ++i) {
            if (extract_role_from_group(json_object_array_get_idx(groups_obj, i))) {
                found = true;
                break;
            }
        }
    } else {
        found = extract_role_from_group(groups_obj);
    }

    json_object_put(root);
    return found;
}

bool build_signed_connector(DppKey *csign_key, const std::string &netaccess_key_hex,
                            const std::string &net_role, const std::string &group_id,
                            uint32_t expiry, std::string &connector, std::string &csign_pub_hex,
                            std::string &netaccess_pub_hex, std::string &error)
{
    connector.clear();
    error.clear();

    if (!csign_key) {
        error = "Missing C-sign key";
        return false;
    }
    if (netaccess_key_hex.empty()) {
        error = "Missing netAccessKey";
        return false;
    }

    if (csign_pub_hex.empty()) {
        int len = i2d_PUBKEY(csign_key, nullptr);
        if (len <= 0) {
            error = "Failed encoding C-sign public key";
            return false;
        }
        std::vector<uint8_t> der(static_cast<size_t>(len));
        unsigned char *ptr = der.data();
        if (i2d_PUBKEY(csign_key, &ptr) != len) {
            error = "Failed encoding C-sign public key";
            return false;
        }
        csign_pub_hex = bytes_to_hex_string(der.data(), der.size());
    }

    if (netaccess_pub_hex.empty()) {
        if (!derive_pubkey_hex_from_private(netaccess_key_hex, netaccess_pub_hex)) {
            error = "Failed deriving netAccessKey public key";
            return false;
        }
    }

    std::string kid;
    if (!compute_csign_kid(csign_pub_hex, kid)) {
        error = "Failed computing connector kid";
        return false;
    }

    std::string alg;
    const EVP_MD *md    = nullptr;
    size_t sig_part_len = 0;
    if (!ec_key_info(csign_key, alg, md, sig_part_len)) {
        error = "Unsupported C-sign key";
        return false;
    }

    json_object *header = json_object_new_object();
    json_object_object_add(header, "typ", json_object_new_string("dppCon"));
    json_object_object_add(header, "alg", json_object_new_string(alg.c_str()));
    json_object_object_add(header, "kid", json_object_new_string(kid.c_str()));

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> netaccess_key(nullptr, EVP_PKEY_free);
    if (!parse_ec_pubkey_hex(netaccess_pub_hex, netaccess_key)) {
        json_object_put(header);
        error = "Invalid netAccessKey";
        return false;
    }

    json_object *netaccess_jwk = nullptr;
    if (!ec_key_to_jwk(netaccess_key.get(), &netaccess_jwk, error)) {
        json_object_put(header);
        return false;
    }

    json_object *payload = json_object_new_object();
    json_object *groups  = json_object_new_array();
    json_object *group   = json_object_new_object();
    json_object_object_add(group, "groupId",
                           json_object_new_string(group_id.empty() ? "*" : group_id.c_str()));
    json_object_object_add(group, "netRole", json_object_new_string(net_role.c_str()));
    json_object_array_add(groups, group);

    json_object_object_add(payload, "groups", groups);
    json_object_object_add(payload, "netAccessKey", netaccess_jwk);
    json_object_object_add(payload, "version", json_object_new_int(3));
    if (expiry != 0) {
        std::time_t when = static_cast<std::time_t>(expiry);
        std::tm tm       = *std::gmtime(&when);
        char buf[32]     = {};
        if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm)) {
            json_object_object_add(payload, "expiry", json_object_new_string(buf));
        }
    }

    const char *header_str  = json_object_to_json_string_ext(header, JSON_C_TO_STRING_PLAIN);
    const char *payload_str = json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN);
    std::string header_b64 =
        base64url_encode(reinterpret_cast<const uint8_t *>(header_str), std::strlen(header_str));
    std::string payload_b64 =
        base64url_encode(reinterpret_cast<const uint8_t *>(payload_str), std::strlen(payload_str));
    json_object_put(header);
    json_object_put(payload);

    if (header_b64.empty() || payload_b64.empty()) {
        error = "Failed encoding connector";
        return false;
    }

    std::string signature_raw;
    if (!jws_sign(csign_key, md, sig_part_len, header_b64 + "." + payload_b64, signature_raw,
                  error)) {
        return false;
    }

    std::string sig_b64 = base64url_encode(reinterpret_cast<const uint8_t *>(signature_raw.data()),
                                           signature_raw.size());
    if (sig_b64.empty()) {
        error = "Failed encoding connector signature";
        return false;
    }

    connector = header_b64 + "." + payload_b64 + "." + sig_b64;
    return true;
}

bool validate_signed_connector(const std::string &connector, DppKey *csign_key,
                               std::string &group_id, std::string &net_role, uint32_t &expiry,
                               std::string &error)
{
    group_id.clear();
    net_role.clear();
    expiry = 0;
    error.clear();

    if (connector.empty()) {
        error = "Missing connector";
        return false;
    }
    if (!csign_key) {
        error = "Missing C-sign public key";
        return false;
    }

    std::string header_b64;
    std::string payload_b64;
    std::string sig_b64;
    if (!split_jws(connector, header_b64, payload_b64, sig_b64, error)) {
        return false;
    }

    std::string header_json;
    if (!beerocks::string_utils::base64_url_decode(header_b64, header_json)) {
        error = "Invalid connector header encoding";
        return false;
    }

    std::string alg;
    std::string typ;
    std::string kid;
    if (!parse_jws_header(header_json, alg, typ, kid, error)) {
        return false;
    }

    if (!typ.empty() && typ != "dppCon") {
        error = "Unexpected connector typ";
        return false;
    }
    if (kid.empty()) {
        error = "Missing connector kid";
        return false;
    }

    std::string payload_json;
    if (!beerocks::string_utils::base64_url_decode(payload_b64, payload_json)) {
        error = "Invalid connector payload encoding";
        return false;
    }

    std::string signature_raw;
    if (!beerocks::string_utils::base64_url_decode(sig_b64, signature_raw)) {
        error = "Invalid connector signature encoding";
        return false;
    }

    std::string expected_alg;
    const EVP_MD *md    = nullptr;
    size_t sig_part_len = 0;
    if (!ec_key_info(csign_key, expected_alg, md, sig_part_len)) {
        error = "Unsupported C-sign key type";
        return false;
    }
    if (alg != expected_alg) {
        error = "Connector alg does not match C-sign key";
        return false;
    }

    std::string csign_pub_hex;
    int pub_len = i2d_PUBKEY(csign_key, nullptr);
    if (pub_len > 0) {
        std::vector<uint8_t> der(static_cast<size_t>(pub_len));
        unsigned char *ptr = der.data();
        if (i2d_PUBKEY(csign_key, &ptr) == pub_len) {
            csign_pub_hex = bytes_to_hex_string(der.data(), der.size());
        }
    }

    std::string expected_kid;
    if (!csign_pub_hex.empty() && !compute_csign_kid(csign_pub_hex, expected_kid)) {
        error = "Failed computing connector kid";
        return false;
    }
    if (!expected_kid.empty() && kid != expected_kid) {
        error = "Connector kid does not match C-sign key";
        return false;
    }

    if (!verify_jws_signature(csign_key, md, sig_part_len, header_b64 + "." + payload_b64,
                              signature_raw, error)) {
        return false;
    }

    DppConnectorPayload payload;
    if (!parse_dpp_connector_payload(payload_json, payload, error)) {
        return false;
    }

    group_id = payload.group_id;
    if (!normalize_dpp_netrole(payload.net_role, net_role)) {
        error = "Unsupported netRole in connector";
        return false;
    }
    expiry = payload.expiry;

    if (expiry != 0) {
        const auto now = static_cast<uint32_t>(std::time(nullptr));
        if (now >= expiry) {
            error = "Connector has expired";
            return false;
        }
    }

    return true;
}

bool extract_dpp_connector_from_frame(const std::vector<uint8_t> &frame, uint8_t &frame_type,
                                      std::string &connector)
{
    connector.clear();
    size_t attrs_offset = 0;
    if (!find_dpp_public_action_attributes(frame.data(), frame.size(), frame_type, attrs_offset)) {
        return false;
    }

    constexpr uint16_t k_dpp_attr_connector = 0x100d;
    if (attrs_offset >= frame.size()) {
        return false;
    }

    return extract_dpp_attribute(frame.data() + attrs_offset, frame.size() - attrs_offset,
                                 k_dpp_attr_connector, connector);
}

bool connector_has_netrole(const std::string &connector, const std::string &expected_role)
{
    std::string net_role;
    if (!extract_dpp_netrole(connector, net_role)) {
        return false;
    }
    return beerocks::string_utils::case_insensitive_compare(net_role, expected_role);
}

} // namespace controller_dpp
} // namespace son
