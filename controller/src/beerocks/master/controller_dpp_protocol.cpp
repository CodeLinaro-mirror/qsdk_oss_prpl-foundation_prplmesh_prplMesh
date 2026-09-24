/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */


#include "controller_dpp_protocol.h"
#include "dpp/dpp_internal.h"

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

void DppConfiguratorSession::reset()
{
    m_peer_bootstrap_key.reset(nullptr);
    m_own_protocol_key.reset(nullptr);
    m_peer_protocol_key.reset(nullptr);
    m_peer_bootstrap_hash.clear();
    m_i_nonce.clear();
    m_r_nonce.clear();
    m_e_nonce.clear();
    m_mx.clear();
    m_nx.clear();
    m_k1.clear();
    m_k2.clear();
    m_bk.clear();
    m_ke.clear();
    m_md                     = nullptr;
    m_curve_nid              = 0;
    m_hash_len               = 0;
    m_nonce_len              = 0;
    m_coord_len              = 0;
    m_version                = k_dpp_default_version;
    m_authentication_started = false;
    m_authentication_success = false;
}

bool DppConfiguratorSession::start(const std::string &peer_bootstrap_public_key, uint8_t version,
                                   std::vector<uint8_t> &auth_request_frame,
                                   std::string &error)
{
    reset();
    auth_request_frame.clear();
    error.clear();

    std::string bootstrap_der;
    if (peer_bootstrap_public_key.empty() ||
        !beerocks::string_utils::base64_url_decode(peer_bootstrap_public_key, bootstrap_der)) {
        error = "Invalid DPP bootstrap public key";
        return false;
    }

    if (!parse_ec_pubkey_der(reinterpret_cast<const uint8_t *>(bootstrap_der.data()),
                             bootstrap_der.size(), m_peer_bootstrap_key)) {
        error = "Failed parsing DPP bootstrap public key";
        return false;
    }

    if (!curve_info_from_key(m_peer_bootstrap_key.get(), m_curve_nid, m_md, m_hash_len,
                             m_nonce_len, m_coord_len, error)) {
        reset();
        return false;
    }

    m_peer_bootstrap_hash.resize(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const uint8_t *>(bootstrap_der.data()), bootstrap_der.size(),
           m_peer_bootstrap_hash.data());

    if (!generate_ec_key(m_curve_nid, m_own_protocol_key)) {
        error = "Failed generating DPP initiator protocol key";
        reset();
        return false;
    }

    std::vector<uint8_t> initiator_protocol_key;
    if (!ec_public_xy(m_own_protocol_key.get(), m_coord_len, initiator_protocol_key)) {
        error = "Failed encoding DPP initiator protocol key";
        reset();
        return false;
    }

    if (!ecdh_secret(m_own_protocol_key.get(), m_peer_bootstrap_key.get(), m_coord_len, m_mx) ||
        !derive_intermediate_key(m_md, m_mx, "first intermediate key", m_hash_len, m_k1)) {
        error = "Failed deriving DPP k1";
        reset();
        return false;
    }

    m_i_nonce.resize(m_nonce_len);
    if (RAND_bytes(m_i_nonce.data(), static_cast<int>(m_i_nonce.size())) != 1) {
        error = "Failed generating DPP initiator nonce";
        reset();
        return false;
    }

    m_version = version > 0 ? version : k_dpp_default_version;

    std::vector<uint8_t> clear;
    append_dpp_attr(clear, k_dpp_attr_i_nonce, m_i_nonce.data(), m_i_nonce.size());
    append_dpp_attr_u8(clear, k_dpp_attr_i_capabilities, k_dpp_capab_configurator);

    build_dpp_public_action_prefix(k_dpp_authentication_request, auth_request_frame);
    const auto attr_start = auth_request_frame.size();
    append_dpp_attr(auth_request_frame, k_dpp_attr_r_bootstrap_hash, m_peer_bootstrap_hash.data(),
                    m_peer_bootstrap_hash.size());
    append_dpp_attr(auth_request_frame, k_dpp_attr_i_protocol_key,
                    initiator_protocol_key.data(), initiator_protocol_key.size());
    if (m_version > 1) {
        append_dpp_attr_u8(auth_request_frame, k_dpp_attr_protocol_version, m_version);
    }

    const std::vector<uint8_t> dpp_header(auth_request_frame.begin() + 2,
                                          auth_request_frame.begin() + 8);
    const std::vector<uint8_t> attrs_before(auth_request_frame.begin() + attr_start,
                                            auth_request_frame.end());
    std::vector<uint8_t> wrapped;
    if (!aes_siv_encrypt_vector(m_k1, clear, {dpp_header, attrs_before}, wrapped)) {
        error = "Failed wrapping DPP Authentication Request";
        reset();
        return false;
    }
    append_dpp_attr(auth_request_frame, k_dpp_attr_wrapped_data, wrapped.data(), wrapped.size());

    m_authentication_started = true;
    return true;
}

bool DppConfiguratorSession::handle_authentication_response(
    const std::vector<uint8_t> &frame, std::vector<uint8_t> &auth_confirm_frame,
    std::string &error)
{
    auth_confirm_frame.clear();
    error.clear();
    if (!m_authentication_started || !m_peer_bootstrap_key || !m_own_protocol_key) {
        error = "DPP authentication response without active controller session";
        return false;
    }

    const uint8_t *dpp_header = nullptr;
    const uint8_t *attrs      = nullptr;
    size_t attrs_len          = 0;
    if (!split_dpp_public_action_frame(frame, k_dpp_authentication_response, dpp_header, attrs,
                                       attrs_len)) {
        error = "Invalid DPP Authentication Response frame";
        return false;
    }

    DppAttributeView wrapped;
    if (!get_dpp_attr(attrs, attrs_len, k_dpp_attr_wrapped_data, wrapped) || !wrapped.data ||
        wrapped.len < 16 || !wrapped.header || wrapped.header < attrs) {
        error = "DPP Authentication Response missing Wrapped Data";
        return false;
    }

    const size_t attrs_before_len = static_cast<size_t>(wrapped.header - attrs);

    DppAttributeView status;
    if (!parse_required_attr(attrs, attrs_before_len, k_dpp_attr_status, 1, status)) {
        error = "DPP Authentication Response missing status";
        return false;
    }
    if (status.data[0] != k_dpp_status_ok) {
        error = "DPP Authentication Response returned non-OK status";
        return false;
    }

    DppAttributeView r_bootstrap_hash;
    if (!parse_required_attr(attrs, attrs_before_len, k_dpp_attr_r_bootstrap_hash,
                             m_peer_bootstrap_hash.size(), r_bootstrap_hash) ||
        !std::equal(m_peer_bootstrap_hash.begin(), m_peer_bootstrap_hash.end(),
                    r_bootstrap_hash.data)) {
        error = "DPP Authentication Response bootstrap hash mismatch";
        return false;
    }

    DppAttributeView i_bootstrap_hash;
    if (get_dpp_attr(attrs, attrs_before_len, k_dpp_attr_i_bootstrap_hash, i_bootstrap_hash)) {
        error = "DPP mutual authentication is not configured on the controller";
        return false;
    }

    DppAttributeView version;
    if (get_dpp_attr(attrs, attrs_before_len, k_dpp_attr_protocol_version, version) &&
        version.data && version.len >= 1 && version.data[0] > 0) {
        m_version = std::min<uint8_t>(m_version, version.data[0]);
    }

    DppAttributeView r_protocol_key;
    if (!parse_required_attr(attrs, attrs_before_len, k_dpp_attr_r_protocol_key,
                             2 * m_coord_len, r_protocol_key) ||
        !ec_public_from_xy(m_curve_nid, r_protocol_key.data, r_protocol_key.len,
                           m_peer_protocol_key)) {
        error = "Invalid DPP responder protocol key";
        return false;
    }

    if (!ecdh_secret(m_own_protocol_key.get(), m_peer_protocol_key.get(), m_coord_len, m_nx) ||
        !derive_intermediate_key(m_md, m_nx, "second intermediate key", m_hash_len, m_k2)) {
        error = "Failed deriving DPP k2";
        return false;
    }

    const std::vector<uint8_t> ad_header(dpp_header, dpp_header + 6);
    const std::vector<uint8_t> ad_attrs(attrs, attrs + attrs_before_len);
    std::vector<uint8_t> unwrapped;
    if (!aes_siv_decrypt_vector(m_k2, wrapped.data, wrapped.len, {ad_header, ad_attrs},
                                unwrapped)) {
        error = "Failed unwrapping DPP Authentication Response";
        return false;
    }

    DppAttributeView r_nonce;
    DppAttributeView i_nonce;
    DppAttributeView r_capab;
    DppAttributeView secondary_wrapped;
    if (!parse_required_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_r_nonce, m_nonce_len,
                             r_nonce) ||
        !parse_required_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_i_nonce, m_nonce_len,
                             i_nonce) ||
        !parse_required_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_r_capabilities, 1,
                             r_capab) ||
        !get_dpp_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_wrapped_data,
                      secondary_wrapped) ||
        !secondary_wrapped.data || secondary_wrapped.len < 16) {
        error = "DPP Authentication Response has incomplete wrapped attributes";
        return false;
    }
    if (!std::equal(m_i_nonce.begin(), m_i_nonce.end(), i_nonce.data)) {
        error = "DPP Authentication Response nonce mismatch";
        return false;
    }
    if ((r_capab.data[0] & k_dpp_role_mask) != k_dpp_capab_enrollee) {
        error = "DPP peer did not select Enrollee role";
        return false;
    }
    m_r_nonce.assign(r_nonce.data, r_nonce.data + r_nonce.len);

    if (!derive_bk_ke(m_md, m_i_nonce, m_r_nonce, m_mx, m_nx, m_hash_len, m_bk, m_ke)) {
        error = "Failed deriving DPP ke";
        return false;
    }

    std::vector<uint8_t> secondary;
    if (!aes_siv_decrypt_vector(m_ke, secondary_wrapped.data, secondary_wrapped.len, {},
                                secondary)) {
        error = "Failed unwrapping DPP responder authentication tag";
        return false;
    }

    DppAttributeView r_auth;
    if (!parse_required_attr(secondary.data(), secondary.size(), k_dpp_attr_r_auth_tag,
                             m_hash_len, r_auth)) {
        error = "DPP Authentication Response missing R-auth";
        return false;
    }

    std::vector<uint8_t> pi_x;
    std::vector<uint8_t> pr_x;
    std::vector<uint8_t> br_x;
    if (!ec_public_x(m_own_protocol_key.get(), m_coord_len, pi_x) ||
        !ec_public_x(m_peer_protocol_key.get(), m_coord_len, pr_x) ||
        !ec_public_x(m_peer_bootstrap_key.get(), m_coord_len, br_x)) {
        error = "Failed reading DPP authentication public keys";
        return false;
    }

    std::vector<uint8_t> expected_r_auth;
    if (!hash_vector(m_md,
                     {m_i_nonce, m_r_nonce, pi_x, pr_x, br_x, std::vector<uint8_t>{0}},
                     expected_r_auth) ||
        expected_r_auth.size() != r_auth.len ||
        !std::equal(expected_r_auth.begin(), expected_r_auth.end(), r_auth.data)) {
        error = "DPP responder authentication tag mismatch";
        return false;
    }

    std::vector<uint8_t> i_auth;
    if (!hash_vector(m_md,
                     {m_r_nonce, m_i_nonce, pr_x, pi_x, br_x, std::vector<uint8_t>{1}},
                     i_auth)) {
        error = "Failed computing DPP initiator authentication tag";
        return false;
    }

    std::vector<uint8_t> clear;
    append_dpp_attr(clear, k_dpp_attr_i_auth_tag, i_auth.data(), i_auth.size());

    build_dpp_public_action_prefix(k_dpp_authentication_confirm, auth_confirm_frame);
    const auto attr_start = auth_confirm_frame.size();
    append_dpp_attr_u8(auth_confirm_frame, k_dpp_attr_status, k_dpp_status_ok);
    append_dpp_attr(auth_confirm_frame, k_dpp_attr_r_bootstrap_hash, m_peer_bootstrap_hash.data(),
                    m_peer_bootstrap_hash.size());

    const std::vector<uint8_t> conf_header(auth_confirm_frame.begin() + 2,
                                           auth_confirm_frame.begin() + 8);
    const std::vector<uint8_t> attrs_before(auth_confirm_frame.begin() + attr_start,
                                            auth_confirm_frame.end());
    std::vector<uint8_t> confirm_wrapped;
    if (!aes_siv_encrypt_vector(m_ke, clear, {conf_header, attrs_before}, confirm_wrapped)) {
        error = "Failed wrapping DPP Authentication Confirm";
        return false;
    }
    append_dpp_attr(auth_confirm_frame, k_dpp_attr_wrapped_data, confirm_wrapped.data(),
                    confirm_wrapped.size());

    m_authentication_success = true;
    return true;
}

bool DppConfiguratorSession::unwrap_configuration_request(const std::vector<uint8_t> &frame,
                                                          std::string &request_object_json,
                                                          std::string &net_role,
                                                          std::string &error)
{
    request_object_json.clear();
    net_role.clear();
    error.clear();
    if (!m_authentication_success || m_ke.empty()) {
        error = "DPP Configuration Request without authenticated controller session";
        return false;
    }

    const uint8_t *attrs = frame.data();
    size_t attrs_len     = frame.size();
    uint8_t frame_type   = 0;
    size_t attrs_offset  = 0;
    if (find_dpp_public_action_attributes(frame.data(), frame.size(), frame_type, attrs_offset)) {
        attrs     = frame.data() + attrs_offset;
        attrs_len = frame.size() - attrs_offset;
    }

    DppAttributeView wrapped;
    if (!get_dpp_attr(attrs, attrs_len, k_dpp_attr_wrapped_data, wrapped) || !wrapped.data ||
        wrapped.len < 16) {
        error = "DPP Configuration Request missing Wrapped Data";
        return false;
    }

    std::vector<uint8_t> unwrapped;
    if (!aes_siv_decrypt_vector(m_ke, wrapped.data, wrapped.len, {}, unwrapped)) {
        error = "Failed unwrapping DPP Configuration Request";
        return false;
    }

    DppAttributeView e_nonce;
    DppAttributeView config_attrs;
    if (!parse_required_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_enrollee_nonce,
                             m_nonce_len, e_nonce) ||
        !get_dpp_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_config_attr_obj,
                      config_attrs) ||
        !config_attrs.data || config_attrs.len == 0) {
        error = "DPP Configuration Request has incomplete attributes";
        return false;
    }

    m_e_nonce.assign(e_nonce.data, e_nonce.data + e_nonce.len);
    request_object_json.assign(reinterpret_cast<const char *>(config_attrs.data),
                               config_attrs.len);

    json_object *root = json_tokener_parse(request_object_json.c_str());
    if (!root) {
        error = "Invalid DPP Configuration Request JSON";
        return false;
    }
    json_object *role_obj = nullptr;
    if (!json_object_object_get_ex(root, "netRole", &role_obj) ||
        json_object_get_type(role_obj) != json_type_string) {
        json_object_put(root);
        error = "DPP Configuration Request missing netRole";
        return false;
    }

    std::string normalized;
    if (!normalize_dpp_netrole(json_object_get_string(role_obj), normalized)) {
        json_object_put(root);
        error = "Unsupported DPP Configuration Request netRole";
        return false;
    }
    json_object_put(root);
    net_role = normalized;
    return true;
}

bool DppConfiguratorSession::build_connector_payload(const std::string &net_role,
                                                     const std::string &group_id,
                                                     uint32_t expiry,
                                                     std::string &payload_json,
                                                     std::string &error) const
{
    payload_json.clear();
    error.clear();
    if (!m_authentication_success || !m_peer_protocol_key) {
        error = "DPP connector payload requested before authentication success";
        return false;
    }

    json_object *root   = json_object_new_object();
    json_object *groups = json_object_new_array();
    json_object *group  = json_object_new_object();
    json_object_object_add(group, "groupId",
                           json_object_new_string(group_id.empty() ? "*" : group_id.c_str()));
    json_object_object_add(group, "netRole", json_object_new_string(net_role.c_str()));
    json_object_array_add(groups, group);
    json_object_object_add(root, "groups", groups);

    std::string jwk_error;
    json_object *netaccess_jwk = nullptr;
    if (!ec_key_to_jwk(m_peer_protocol_key.get(), &netaccess_jwk, jwk_error)) {
        json_object_put(root);
        error = "Failed encoding DPP netAccessKey: " + jwk_error;
        return false;
    }
    json_object_object_add(root, "netAccessKey", netaccess_jwk);

    if (expiry != 0) {
        std::time_t when = expiry;
        std::tm tm{};
        if (gmtime_r(&when, &tm)) {
            char buf[32] = {};
            if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) > 0) {
                json_object_object_add(root, "expiry", json_object_new_string(buf));
            }
        }
    }

    payload_json = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    json_object_put(root);
    if (payload_json.empty()) {
        error = "Failed building DPP connector payload";
        return false;
    }
    return true;
}

bool DppConfiguratorSession::build_configuration_response(
    const std::string &config_object_json, std::vector<uint8_t> &response_frame,
    std::string &error, bool send_conn_status)
{
    std::vector<std::string> config_objects;
    if (!config_object_json.empty()) {
        config_objects.push_back(config_object_json);
    }

    return build_configuration_response(config_objects, response_frame, error, send_conn_status);
}

bool DppConfiguratorSession::build_configuration_response(
    const std::vector<std::string> &config_object_jsons, std::vector<uint8_t> &response_frame,
    std::string &error, bool send_conn_status)
{
    response_frame.clear();
    error.clear();
    if (!m_authentication_success || m_ke.empty() || m_e_nonce.empty()) {
        error = "DPP Configuration Response without active request";
        return false;
    }
    if (config_object_jsons.empty()) {
        error = "DPP Configuration Response missing Config Object";
        return false;
    }

    std::vector<uint8_t> clear;
    append_dpp_attr(clear, k_dpp_attr_enrollee_nonce, m_e_nonce.data(), m_e_nonce.size());
    for (const auto &config_object_json : config_object_jsons) {
        if (config_object_json.empty()) {
            error = "DPP Configuration Response has empty Config Object";
            return false;
        }
        append_dpp_attr(clear, k_dpp_attr_config_obj,
                        reinterpret_cast<const uint8_t *>(config_object_json.data()),
                        config_object_json.size());
    }
    if (send_conn_status) {
        append_dpp_attr(clear, k_dpp_attr_send_conn_status, nullptr, 0);
    }

    append_dpp_attr_u8(response_frame, k_dpp_attr_status, k_dpp_status_ok);
    const std::vector<uint8_t> ad = response_frame;

    std::vector<uint8_t> wrapped;
    if (!aes_siv_encrypt_vector(m_ke, clear, {ad}, wrapped)) {
        error = "Failed wrapping DPP Configuration Response";
        return false;
    }
    append_dpp_attr(response_frame, k_dpp_attr_wrapped_data, wrapped.data(), wrapped.size());
    return true;
}

bool DppConfiguratorSession::unwrap_configuration_result(const std::vector<uint8_t> &frame,
                                                         uint8_t &status, std::string &error)
{
    status = 0;
    error.clear();
    if (!m_authentication_success || m_ke.empty() || m_e_nonce.empty()) {
        error = "DPP Configuration Result without authenticated controller session";
        return false;
    }

    const uint8_t *dpp_header = nullptr;
    const uint8_t *attrs      = nullptr;
    size_t attrs_len          = 0;
    if (!split_dpp_public_action_frame(frame, k_dpp_configuration_result, dpp_header, attrs,
                                       attrs_len)) {
        error = "Invalid DPP Configuration Result frame";
        return false;
    }

    DppAttributeView wrapped;
    if (!get_dpp_attr(attrs, attrs_len, k_dpp_attr_wrapped_data, wrapped) || !wrapped.data ||
        wrapped.len < 16 || !wrapped.header || wrapped.header < attrs) {
        error = "DPP Configuration Result missing Wrapped Data";
        return false;
    }

    const size_t attrs_before_len = static_cast<size_t>(wrapped.header - attrs);
    const std::vector<uint8_t> ad_header(dpp_header, dpp_header + 6);
    const std::vector<uint8_t> ad_attrs(attrs, attrs + attrs_before_len);
    std::vector<uint8_t> unwrapped;
    if (!aes_siv_decrypt_vector(m_ke, wrapped.data, wrapped.len, {ad_header, ad_attrs},
                                unwrapped)) {
        error = "Failed unwrapping DPP Configuration Result";
        return false;
    }

    DppAttributeView status_attr;
    DppAttributeView e_nonce;
    if (!parse_required_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_status, 1,
                             status_attr) ||
        !parse_required_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_enrollee_nonce,
                             m_e_nonce.size(), e_nonce)) {
        error = "DPP Configuration Result has incomplete attributes";
        return false;
    }
    if (!std::equal(m_e_nonce.begin(), m_e_nonce.end(), e_nonce.data)) {
        error = "DPP Configuration Result nonce mismatch";
        return false;
    }

    status = status_attr.data[0];
    return true;
}

bool DppConfiguratorSession::unwrap_connection_status_result(const std::vector<uint8_t> &frame,
                                                             uint8_t &result, std::string &error)
{
    result = 0;
    error.clear();
    if (!m_authentication_success || m_ke.empty() || m_e_nonce.empty()) {
        error = "DPP Connection Status Result without authenticated controller session";
        return false;
    }

    const uint8_t *dpp_header = nullptr;
    const uint8_t *attrs      = nullptr;
    size_t attrs_len          = 0;
    if (!split_dpp_public_action_frame(frame, k_dpp_connection_status_result, dpp_header, attrs,
                                       attrs_len)) {
        error = "Invalid DPP Connection Status Result frame";
        return false;
    }

    DppAttributeView wrapped;
    if (!get_dpp_attr(attrs, attrs_len, k_dpp_attr_wrapped_data, wrapped) || !wrapped.data ||
        wrapped.len < 16 || !wrapped.header || wrapped.header < attrs) {
        error = "DPP Connection Status Result missing Wrapped Data";
        return false;
    }

    const size_t attrs_before_len = static_cast<size_t>(wrapped.header - attrs);
    const std::vector<uint8_t> ad_header(dpp_header, dpp_header + 6);
    const std::vector<uint8_t> ad_attrs(attrs, attrs + attrs_before_len);
    std::vector<uint8_t> unwrapped;
    if (!aes_siv_decrypt_vector(m_ke, wrapped.data, wrapped.len, {ad_header, ad_attrs},
                                unwrapped)) {
        error = "Failed unwrapping DPP Connection Status Result";
        return false;
    }

    DppAttributeView e_nonce;
    DppAttributeView conn_status;
    if (!parse_required_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_enrollee_nonce,
                             m_e_nonce.size(), e_nonce) ||
        !get_dpp_attr(unwrapped.data(), unwrapped.size(), k_dpp_attr_conn_status, conn_status) ||
        !conn_status.data || conn_status.len == 0) {
        error = "DPP Connection Status Result has incomplete attributes";
        return false;
    }
    if (!std::equal(m_e_nonce.begin(), m_e_nonce.end(), e_nonce.data)) {
        error = "DPP Connection Status Result nonce mismatch";
        return false;
    }

    std::string conn_status_json(reinterpret_cast<const char *>(conn_status.data),
                                 conn_status.len);
    json_object *root = json_tokener_parse(conn_status_json.c_str());
    if (!root) {
        error = "Invalid DPP Connection Status JSON";
        return false;
    }

    json_object *result_obj = nullptr;
    if (!json_object_object_get_ex(root, "result", &result_obj) ||
        json_object_get_type(result_obj) != json_type_int) {
        json_object_put(root);
        error = "DPP Connection Status JSON missing result";
        return false;
    }

    const auto parsed_result = json_object_get_int(result_obj);
    json_object_put(root);
    if (parsed_result < 0 || parsed_result > 255) {
        error = "DPP Connection Status result out of range";
        return false;
    }

    result = static_cast<uint8_t>(parsed_result);
    return true;
}

} // namespace controller_dpp
} // namespace son
