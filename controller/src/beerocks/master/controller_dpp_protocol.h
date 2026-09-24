/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _CONTROLLER_DPP_PROTOCOL_H_
#define _CONTROLLER_DPP_PROTOCOL_H_

#include <openssl/evp.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace son {
namespace controller_dpp {

using DppKey    = EVP_PKEY;

struct DppKeyDeleter {
    void operator()(DppKey *key) const { EVP_PKEY_free(key); }
};

using DppKeyPtr = std::unique_ptr<DppKey, DppKeyDeleter>;

struct DppConfigurationObjectOptions {
    std::string akm = "dpp";
    bool include_discovery = true;
    bool include_net_role = false;
    std::string net_role;
    bool include_df_counter_threshold = false;
    uint16_t df_counter_threshold = 0;
    std::string passphrase;
    std::string psk_hex;
    std::string security_ies_hex;
    bool ssid_advertisement_valid = false;
    bool ssid_advertisement       = true;
};

bool parse_ec_private_key_hex(const std::string &hex, DppKeyPtr &out);

bool build_signed_connector(DppKey *csign_key, const std::string &netaccess_key_hex,
                            const std::string &net_role, const std::string &group_id,
                            uint32_t expiry, std::string &connector, std::string &csign_pub_hex,
                            std::string &netaccess_pub_hex, std::string &error);

bool build_dpp_configuration_object(DppKey *csign_key, const std::string &pp_key_hex,
                                    const std::string &connector_payload_json,
                                    const std::string &ssid_hex,
                                    const std::string &expected_net_role,
                                    const std::string &expected_group_id,
                                    std::string &config_object_json, std::string &error);
bool build_dpp_configuration_object(DppKey *csign_key, const std::string &pp_key_hex,
                                    const std::string &connector_payload_json,
                                    const std::string &ssid_hex,
                                    const std::string &expected_net_role,
                                    const std::string &expected_group_id,
                                    const DppConfigurationObjectOptions &options,
                                    std::string &config_object_json, std::string &error);

/*
 * Controller-native DPP Configurator protocol session.
 *
 * This class intentionally implements the Controller side of the DPP Authentication and
 * Configuration exchanges with OpenSSL primitives and prplMesh common crypto helpers. It is not a
 * wrapper around hostapd's DPP implementation. hostapd remains on the Agent side for radio-facing
 * DPP frame TX/RX, GAS response transmission, and credential application.
 */
class DppConfiguratorSession {
public:
    void reset();

    bool start(const std::string &peer_bootstrap_public_key, uint8_t version,
               std::vector<uint8_t> &auth_request_frame, std::string &error);
    bool handle_authentication_response(const std::vector<uint8_t> &frame,
                                        std::vector<uint8_t> &auth_confirm_frame,
                                        std::string &error);
    bool unwrap_configuration_request(const std::vector<uint8_t> &frame,
                                      std::string &request_object_json, std::string &net_role,
                                      std::string &error);
    bool build_connector_payload(const std::string &net_role, const std::string &group_id,
                                 uint32_t expiry, std::string &payload_json,
                                 std::string &error) const;
    bool build_configuration_response(const std::string &config_object_json,
                                      std::vector<uint8_t> &response_frame,
                                      std::string &error, bool send_conn_status = false);
    bool build_configuration_response(const std::vector<std::string> &config_object_jsons,
                                      std::vector<uint8_t> &response_frame,
                                      std::string &error, bool send_conn_status = false);
    bool unwrap_configuration_result(const std::vector<uint8_t> &frame, uint8_t &status,
                                     std::string &error);
    bool unwrap_connection_status_result(const std::vector<uint8_t> &frame, uint8_t &result,
                                         std::string &error);

    bool authentication_started() const { return m_authentication_started; }
    bool authentication_success() const { return m_authentication_success; }

private:
    DppKeyPtr m_peer_bootstrap_key;
    DppKeyPtr m_own_protocol_key;
    DppKeyPtr m_peer_protocol_key;
    std::vector<uint8_t> m_peer_bootstrap_hash;
    std::vector<uint8_t> m_i_nonce;
    std::vector<uint8_t> m_r_nonce;
    std::vector<uint8_t> m_e_nonce;
    std::vector<uint8_t> m_mx;
    std::vector<uint8_t> m_nx;
    std::vector<uint8_t> m_k1;
    std::vector<uint8_t> m_k2;
    std::vector<uint8_t> m_bk;
    std::vector<uint8_t> m_ke;
    const EVP_MD *m_md = nullptr;
    int m_curve_nid    = 0;
    size_t m_hash_len  = 0;
    size_t m_nonce_len = 0;
    size_t m_coord_len = 0;
    uint8_t m_version  = 2;
    bool m_authentication_started = false;
    bool m_authentication_success = false;
};

bool validate_signed_connector(const std::string &connector, DppKey *csign_key,
                               std::string &group_id, std::string &net_role, uint32_t &expiry,
                               std::string &error);

bool parse_dpp_config_request_objects(const std::string &json_blob,
                                      std::vector<std::string> &net_roles,
                                      std::string *backhaul_akm, bool *bsta_max_links,
                                      std::unordered_set<std::string> *bsta_ruids);

bool find_dpp_public_action_attributes(const uint8_t *frame, size_t len, uint8_t &frame_type,
                                       size_t &attrs_offset);

bool extract_dpp_connector_from_frame(const std::vector<uint8_t> &frame, uint8_t &frame_type,
                                      std::string &connector);

bool connector_has_netrole(const std::string &connector, const std::string &expected_role);

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_PROTOCOL_H_
