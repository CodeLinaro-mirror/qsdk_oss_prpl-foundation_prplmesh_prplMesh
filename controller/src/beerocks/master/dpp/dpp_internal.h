/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _CONTROLLER_DPP_INTERNAL_H_
#define _CONTROLLER_DPP_INTERNAL_H_

#include "controller_dpp_protocol.h"

#include <json-c/json.h>
#include <openssl/evp.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace son {
namespace controller_dpp {

struct DppConnectorPayload {
    std::string group_id;
    std::string net_role;
    std::string netaccess_jwk_der_hex;
    uint32_t expiry = 0;
};

constexpr uint8_t k_dpp_public_action_category   = 0x04;
constexpr uint8_t k_dpp_public_action_vendor     = 0x09;
constexpr uint8_t k_dpp_oui[]                    = {0x50, 0x6f, 0x9a};
constexpr uint8_t k_dpp_oui_type                 = 0x1a;
constexpr uint8_t k_dpp_crypto_suite             = 1;
constexpr uint8_t k_dpp_authentication_request   = 0;
constexpr uint8_t k_dpp_authentication_response  = 1;
constexpr uint8_t k_dpp_authentication_confirm   = 2;
constexpr uint8_t k_dpp_status_ok                = 0;
constexpr uint8_t k_dpp_capab_enrollee           = 0x01;
constexpr uint8_t k_dpp_capab_configurator       = 0x02;
constexpr uint8_t k_dpp_role_mask                = k_dpp_capab_enrollee | k_dpp_capab_configurator;
constexpr uint8_t k_dpp_default_version          = 2;
constexpr uint16_t k_dpp_attr_status             = 0x1000;
constexpr uint16_t k_dpp_attr_i_bootstrap_hash   = 0x1001;
constexpr uint16_t k_dpp_attr_r_bootstrap_hash   = 0x1002;
constexpr uint16_t k_dpp_attr_i_protocol_key     = 0x1003;
constexpr uint16_t k_dpp_attr_wrapped_data       = 0x1004;
constexpr uint16_t k_dpp_attr_i_nonce            = 0x1005;
constexpr uint16_t k_dpp_attr_i_capabilities     = 0x1006;
constexpr uint16_t k_dpp_attr_r_nonce            = 0x1007;
constexpr uint16_t k_dpp_attr_r_capabilities     = 0x1008;
constexpr uint16_t k_dpp_attr_r_protocol_key     = 0x1009;
constexpr uint16_t k_dpp_attr_i_auth_tag         = 0x100a;
constexpr uint16_t k_dpp_attr_r_auth_tag         = 0x100b;
constexpr uint16_t k_dpp_attr_config_obj         = 0x100c;
constexpr uint16_t k_dpp_attr_config_attr_obj    = 0x100e;
constexpr uint16_t k_dpp_attr_enrollee_nonce     = 0x1014;
constexpr uint16_t k_dpp_attr_protocol_version   = 0x1019;
constexpr uint16_t k_dpp_attr_send_conn_status   = 0x101b;
constexpr uint16_t k_dpp_attr_conn_status        = 0x101c;
constexpr uint8_t k_dpp_configuration_result     = 11;
constexpr uint8_t k_dpp_connection_status_result = 12;

struct DppAttributeView {
    const uint8_t *data   = nullptr;
    size_t len            = 0;
    const uint8_t *header = nullptr;
};

void append_le16(std::vector<uint8_t> &out, uint16_t value);

void append_dpp_attr(std::vector<uint8_t> &out, uint16_t attr_id, const uint8_t *data, size_t len);

void append_dpp_attr_u8(std::vector<uint8_t> &out, uint16_t attr_id, uint8_t value);

bool get_dpp_attr(const uint8_t *attrs, size_t len, uint16_t attr_id, DppAttributeView &attr);

bool split_dpp_public_action_frame(const std::vector<uint8_t> &frame, uint8_t expected_type,
                                   const uint8_t *&dpp_header, const uint8_t *&attrs,
                                   size_t &attrs_len);

void build_dpp_public_action_prefix(uint8_t frame_type, std::vector<uint8_t> &frame);

bool curve_info_from_key(EVP_PKEY *key, int &nid, const EVP_MD *&md, size_t &hash_len,
                         size_t &nonce_len, size_t &coord_len, std::string &error);

bool parse_ec_pubkey_der(const uint8_t *der, size_t len, DppKeyPtr &out);

bool generate_ec_key(int nid, DppKeyPtr &out);

bool ec_public_xy(EVP_PKEY *key, size_t coord_len, std::vector<uint8_t> &xy);

bool ec_public_x(EVP_PKEY *key, size_t coord_len, std::vector<uint8_t> &x_only);

bool ec_public_from_xy(int nid, const uint8_t *xy, size_t xy_len, DppKeyPtr &out);

bool ecdh_secret(EVP_PKEY *own, EVP_PKEY *peer, size_t coord_len, std::vector<uint8_t> &secret);

bool hmac_digest(const EVP_MD *md, const uint8_t *key, size_t key_len, const uint8_t *data,
                 size_t data_len, std::vector<uint8_t> &out);

bool hkdf_expand_one_block(const EVP_MD *md, const std::vector<uint8_t> &prk, const char *info,
                           size_t out_len, std::vector<uint8_t> &out);

bool derive_intermediate_key(const EVP_MD *md, const std::vector<uint8_t> &secret, const char *info,
                             size_t hash_len, std::vector<uint8_t> &out);

bool derive_bk_ke(const EVP_MD *md, const std::vector<uint8_t> &i_nonce,
                  const std::vector<uint8_t> &r_nonce, const std::vector<uint8_t> &mx,
                  const std::vector<uint8_t> &nx, size_t hash_len, std::vector<uint8_t> &bk,
                  std::vector<uint8_t> &ke);

bool hash_vector(const EVP_MD *md, const std::vector<std::vector<uint8_t>> &parts,
                 std::vector<uint8_t> &out);

bool aes_siv_encrypt_vector(const std::vector<uint8_t> &key, const std::vector<uint8_t> &plain,
                            const std::vector<std::vector<uint8_t>> &ad, std::vector<uint8_t> &out);

bool aes_siv_decrypt_vector(const std::vector<uint8_t> &key, const uint8_t *wrapped,
                            size_t wrapped_len, const std::vector<std::vector<uint8_t>> &ad,
                            std::vector<uint8_t> &plain);

bool parse_required_attr(const uint8_t *attrs, size_t attrs_len, uint16_t attr_id,
                         size_t expected_len, DppAttributeView &attr);

std::string bytes_to_hex_string(const uint8_t *data, size_t len);

bool is_hex_string(const std::string &value);

std::string base64url_encode(const uint8_t *data, size_t len);

bool parse_rfc3339_utc(const std::string &value, uint32_t &epoch);

bool split_jws(const std::string &jws, std::string &header_b64, std::string &payload_b64,
               std::string &sig_b64, std::string &error);

bool parse_jws_header(const std::string &json, std::string &alg, std::string &typ, std::string &kid,
                      std::string &error);

bool jwk_pubkey_to_der_hex(json_object *jwk, std::string &out_hex);

bool normalize_dpp_netrole(const std::string &input, std::string &normalized);

bool parse_ec_pubkey_hex(const std::string &hex,
                         std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> &out);

bool derive_pubkey_hex_from_private(const std::string &hex, std::string &out_hex);

bool ec_key_info(EVP_PKEY *key, std::string &alg, const EVP_MD *&md, size_t &sig_part_len);

bool compute_csign_kid(const std::string &csign_pub_hex, std::string &kid);

bool verify_jws_signature(EVP_PKEY *pkey, const EVP_MD *md, size_t sig_part_len,
                          const std::string &signing_input, const std::string &signature_raw,
                          std::string &error);

bool jws_sign(EVP_PKEY *pkey, const EVP_MD *md, size_t sig_part_len,
              const std::string &signing_input, std::string &signature_raw, std::string &error);

bool ec_key_to_jwk(EVP_PKEY *key, json_object **jwk_out, std::string &error);

bool parse_dpp_connector_payload(const std::string &payload_json, DppConnectorPayload &out,
                                 std::string &error);

bool build_signed_connector_from_payload(DppKey *csign_key, const std::string &payload_json,
                                         std::string &connector, std::string &csign_pub_hex,
                                         std::string &kid, std::string &error);

bool add_public_jwk(json_object *parent, const char *name, EVP_PKEY *key, const std::string &kid,
                    std::string &error);

bool add_discovery_ssid(json_object *discovery_obj, const std::string &ssid_hex,
                        std::string &error);

bool extract_dpp_netrole(const std::string &connector, std::string &net_role);

bool extract_dpp_attribute(const uint8_t *attrs, size_t len, uint16_t attr_id, std::string &out);

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_INTERNAL_H_
