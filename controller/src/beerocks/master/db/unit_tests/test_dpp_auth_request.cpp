/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include "../../controller_dpp_configuration.h"
#include "../../dpp/dpp_gas.h"
#include "../../dpp/dpp_internal.h"
#include "../db.h"

#include <gtest/gtest.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace dpp = son::controller_dpp;

// PUBLIC_INTERFACE
/** A real in-process enrollee; no private session state or radio infrastructure is used. */
class DppExchangeTest : public ::testing::Test {
protected:
    /** Decode the controller request and derive responder-side session keys. */
    void initialize(int nid = NID_X9_62_prime256v1, uint8_t version = 2)
    {
        ASSERT_TRUE(dpp::generate_ec_key(nid, bootstrap));
        ASSERT_TRUE(dpp::generate_ec_key(nid, responder));
        int actual_nid = 0;
        ASSERT_TRUE(dpp::curve_info_from_key(bootstrap.get(), actual_nid, md, hash_len, nonce_len,
                                             coord_len, error))
            << error;
        const int length = i2d_PUBKEY(bootstrap.get(), nullptr);
        ASSERT_GT(length, 0);
        std::vector<uint8_t> der(static_cast<size_t>(length));
        auto *ptr = der.data();
        ASSERT_EQ(length, i2d_PUBKEY(bootstrap.get(), &ptr));
        encoded_bootstrap = dpp::base64url_encode(der.data(), der.size());
        bootstrap_hash.resize(32);
        ASSERT_NE(nullptr, SHA256(der.data(), der.size(), bootstrap_hash.data()));
        ASSERT_TRUE(session.start(encoded_bootstrap, version, request, error)) << error;
        ASSERT_TRUE(error.empty());
        const uint8_t *header = nullptr, *attrs = nullptr;
        size_t attrs_len = 0;
        ASSERT_TRUE(dpp::split_dpp_public_action_frame(request, 0, header, attrs, attrs_len));
        dpp::DppAttributeView xy, wrapped, hash;
        ASSERT_TRUE(
            dpp::parse_required_attr(attrs, attrs_len, dpp::k_dpp_attr_r_bootstrap_hash, 32, hash));
        EXPECT_TRUE(std::equal(bootstrap_hash.begin(), bootstrap_hash.end(), hash.data));
        ASSERT_TRUE(dpp::parse_required_attr(attrs, attrs_len, dpp::k_dpp_attr_i_protocol_key,
                                             2 * coord_len, xy));
        ASSERT_TRUE(dpp::ec_public_from_xy(nid, xy.data, xy.len, initiator));
        ASSERT_TRUE(dpp::ecdh_secret(bootstrap.get(), initiator.get(), coord_len, mx));
        ASSERT_TRUE(dpp::derive_intermediate_key(md, mx, "first intermediate key", hash_len, k1));
        ASSERT_TRUE(dpp::get_dpp_attr(attrs, attrs_len, dpp::k_dpp_attr_wrapped_data, wrapped));
        std::vector<uint8_t> clear;
        ASSERT_TRUE(dpp::aes_siv_decrypt_vector(
            k1, wrapped.data, wrapped.len,
            {std::vector<uint8_t>(header, header + 6), std::vector<uint8_t>(attrs, wrapped.header)},
            clear));
        dpp::DppAttributeView nonce, cap;
        ASSERT_TRUE(dpp::parse_required_attr(clear.data(), clear.size(), dpp::k_dpp_attr_i_nonce,
                                             nonce_len, nonce));
        i_nonce.assign(nonce.data, nonce.data + nonce.len);
        ASSERT_TRUE(dpp::parse_required_attr(clear.data(), clear.size(),
                                             dpp::k_dpp_attr_i_capabilities, 1, cap));
        EXPECT_EQ(dpp::k_dpp_capab_configurator, cap.data[0]);
        ASSERT_TRUE(dpp::ecdh_secret(responder.get(), initiator.get(), coord_len, nx));
        ASSERT_TRUE(dpp::derive_intermediate_key(md, nx, "second intermediate key", hash_len, k2));
        r_nonce.assign(nonce_len, 0x73);
        e_nonce.assign(nonce_len, 0x35);
        ASSERT_TRUE(dpp::derive_bk_ke(md, i_nonce, r_nonce, mx, nx, hash_len, bk, ke));
        ASSERT_TRUE(dpp::ec_public_x(initiator.get(), coord_len, pi_x));
        ASSERT_TRUE(dpp::ec_public_x(responder.get(), coord_len, pr_x));
        ASSERT_TRUE(dpp::ec_public_x(bootstrap.get(), coord_len, br_x));
    }

    /** Build a genuinely encrypted response, optionally corrupting cleartext semantics. */
    void make_response(std::vector<uint8_t> &frame, bool wrong_nonce = false,
                       uint8_t capability = dpp::k_dpp_capab_enrollee, bool wrong_tag = false,
                       bool echo_initiator_hash = false)
    {
        std::vector<uint8_t> r_auth, secondary, secondary_wrapped, clear, xy, wrapped;
        ASSERT_TRUE(dpp::hash_vector(md, {i_nonce, r_nonce, pi_x, pr_x, br_x, {0}}, r_auth));
        if (wrong_tag) {
            r_auth[0] ^= 1;
        }
        dpp::append_dpp_attr(secondary, dpp::k_dpp_attr_r_auth_tag, r_auth.data(), r_auth.size());
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(ke, secondary, {}, secondary_wrapped));
        auto echoed_nonce = i_nonce;
        if (wrong_nonce) {
            echoed_nonce[0] ^= 1;
        }
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_r_nonce, r_nonce.data(), r_nonce.size());
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_i_nonce, echoed_nonce.data(),
                             echoed_nonce.size());
        dpp::append_dpp_attr_u8(clear, dpp::k_dpp_attr_r_capabilities, capability);
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_wrapped_data, secondary_wrapped.data(),
                             secondary_wrapped.size());
        dpp::build_dpp_public_action_prefix(1, frame);
        dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_bootstrap_hash, bootstrap_hash.data(),
                             bootstrap_hash.size());
        if (echo_initiator_hash) {
            dpp::DppAttributeView hash;
            ASSERT_TRUE(dpp::parse_required_attr(request.data() + 8, request.size() - 8,
                                                 dpp::k_dpp_attr_i_bootstrap_hash,
                                                 SHA256_DIGEST_LENGTH, hash));
            dpp::append_dpp_attr(frame, dpp::k_dpp_attr_i_bootstrap_hash, hash.data, hash.len);
        }
        ASSERT_TRUE(dpp::ec_public_xy(responder.get(), coord_len, xy));
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_protocol_key, xy.data(), xy.size());
        ASSERT_TRUE(
            dpp::aes_siv_encrypt_vector(k2, clear,
                                        {std::vector<uint8_t>(frame.begin() + 2, frame.begin() + 8),
                                         std::vector<uint8_t>(frame.begin() + 8, frame.end())},
                                        wrapped));
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_wrapped_data, wrapped.data(), wrapped.size());
    }

    /** Complete authentication and verify the controller's initiator authentication tag. */
    void authenticate(bool echo_initiator_hash = false)
    {
        std::vector<uint8_t> response, confirm, clear, expected;
        make_response(response, false, dpp::k_dpp_capab_enrollee, false, echo_initiator_hash);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_TRUE(session.handle_authentication_response(response, confirm, error)) << error;
        EXPECT_TRUE(error.empty());
        EXPECT_TRUE(session.authentication_success());
        const uint8_t *header = nullptr, *attrs = nullptr;
        size_t length = 0;
        ASSERT_TRUE(dpp::split_dpp_public_action_frame(confirm, 2, header, attrs, length));
        dpp::DppAttributeView wrapped, tag, status;
        ASSERT_TRUE(dpp::parse_required_attr(attrs, length, dpp::k_dpp_attr_status, 1, status));
        EXPECT_EQ(0, status.data[0]);
        ASSERT_TRUE(dpp::get_dpp_attr(attrs, length, dpp::k_dpp_attr_wrapped_data, wrapped));
        ASSERT_TRUE(dpp::aes_siv_decrypt_vector(
            ke, wrapped.data, wrapped.len,
            {std::vector<uint8_t>(header, header + 6), std::vector<uint8_t>(attrs, wrapped.header)},
            clear));
        ASSERT_TRUE(dpp::parse_required_attr(clear.data(), clear.size(), dpp::k_dpp_attr_i_auth_tag,
                                             hash_len, tag));
        ASSERT_TRUE(dpp::hash_vector(md, {r_nonce, i_nonce, pr_x, pi_x, br_x, {1}}, expected));
        EXPECT_TRUE(std::equal(expected.begin(), expected.end(), tag.data));
    }

    /** Build an encrypted configuration request with no associated data. */
    void configuration_request(const std::string &json, std::vector<uint8_t> &frame)
    {
        std::vector<uint8_t> clear, wrapped;
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_enrollee_nonce, e_nonce.data(), e_nonce.size());
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_config_attr_obj,
                             reinterpret_cast<const uint8_t *>(json.data()), json.size());
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(ke, clear, {}, wrapped));
        frame.clear();
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_wrapped_data, wrapped.data(), wrapped.size());
    }

    /** Establish the enrollee nonce through a valid request. */
    void activate_configuration()
    {
        std::vector<uint8_t> frame;
        configuration_request(R"({"netRole":"mapAgent"})", frame);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        std::string json, role;
        ASSERT_TRUE(session.unwrap_configuration_request(frame, json, role, error)) << error;
        EXPECT_EQ("mapAgent", role);
    }

    /** Encrypt public-action result attributes with the real header as associated data. */
    void result_frame(uint8_t type, const std::vector<uint8_t> &clear, std::vector<uint8_t> &frame)
    {
        dpp::build_dpp_public_action_prefix(type, frame);
        std::vector<uint8_t> wrapped;
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(
            ke, clear, {std::vector<uint8_t>(frame.begin() + 2, frame.end()), {}}, wrapped));
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_wrapped_data, wrapped.data(), wrapped.size());
    }

    /** Replace the authenticated outer response plaintext while retaining a valid wire prefix. */
    void response_with_clear(const std::vector<uint8_t> &clear, std::vector<uint8_t> &frame)
    {
        make_response(frame);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        dpp::DppAttributeView wrapped;
        ASSERT_TRUE(dpp::get_dpp_attr(frame.data() + 8, frame.size() - 8,
                                      dpp::k_dpp_attr_wrapped_data, wrapped));
        frame.resize(static_cast<size_t>(wrapped.header - frame.data()));
        std::vector<uint8_t> encrypted;
        ASSERT_TRUE(
            dpp::aes_siv_encrypt_vector(k2, clear,
                                        {std::vector<uint8_t>(frame.begin() + 2, frame.begin() + 8),
                                         std::vector<uint8_t>(frame.begin() + 8, frame.end())},
                                        encrypted));
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_wrapped_data, encrypted.data(),
                             encrypted.size());
    }

    dpp::DppConfiguratorSession session;
    dpp::DppKeyPtr bootstrap, responder, initiator;
    const EVP_MD *md = nullptr;
    size_t hash_len = 0, nonce_len = 0, coord_len = 0;
    std::string error, encoded_bootstrap;
    std::vector<uint8_t> request, bootstrap_hash, i_nonce, r_nonce, e_nonce;
    std::vector<uint8_t> mx, nx, k1, k2, bk, ke, pi_x, pr_x, br_x;
};

// PUBLIC_INTERFACE
/** Authentication verifies on every curve with either an absent or correct optional I-hash. */
TEST_F(DppExchangeTest, RequestAndSuccessfulAuthenticationOnAllCurves)
{
    for (int nid : {NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1}) {
        SCOPED_TRACE(nid);
        for (bool echo_initiator_hash : {false, true}) {
            SCOPED_TRACE(echo_initiator_hash);
            initialize(nid);
            ASSERT_FALSE(HasFatalFailure());
            EXPECT_TRUE(session.authentication_started());
            EXPECT_FALSE(session.authentication_success());
            authenticate(echo_initiator_hash);
            ASSERT_FALSE(HasFatalFailure());
        }
    }
}

// PUBLIC_INTERFACE
/** Version one omits the optional attribute; version zero selects version two. */
TEST_F(DppExchangeTest, VersionEncoding)
{
    for (uint8_t version : {uint8_t(0), uint8_t(1), uint8_t(3)}) {
        initialize(NID_X9_62_prime256v1, version);
        ASSERT_FALSE(HasFatalFailure());
        dpp::DppAttributeView attr;
        const bool found = dpp::get_dpp_attr(request.data() + 8, request.size() - 8,
                                             dpp::k_dpp_attr_protocol_version, attr);
        EXPECT_EQ(version != 1, found);
        if (found) {
            ASSERT_EQ(1U, attr.len);
            EXPECT_EQ(version == 0 ? 2 : version, attr.data[0]);
        }
    }
}

// PUBLIC_INTERFACE
/** Invalid restart clears a previous authenticated session and all request output. */
TEST_F(DppExchangeTest, InvalidBootstrapRestartAndReset)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    for (const std::string &input : {std::string(""), std::string("!"), std::string("YWJj")}) {
        std::vector<uint8_t> output{0xff};
        EXPECT_FALSE(session.start(input, 2, output, error));
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(output.empty());
        EXPECT_FALSE(session.authentication_started());
        EXPECT_FALSE(session.authentication_success());
    }
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    session.reset();
    session.reset();
    EXPECT_FALSE(session.authentication_started());
    std::vector<uint8_t> confirm{0xff};
    EXPECT_FALSE(session.handle_authentication_response({}, confirm, error));
    EXPECT_TRUE(confirm.empty());
    EXPECT_EQ("DPP authentication response without active controller session", error);
}

// PUBLIC_INTERFACE
/** Authenticated encryption alone does not validate a nonce, role or R-auth mismatch. */
TEST_F(DppExchangeTest, RejectsEncryptedSemanticMismatches)
{
    const std::vector<std::string> errors{"DPP Authentication Response nonce mismatch",
                                          "DPP peer did not select Enrollee role",
                                          "DPP responder authentication tag mismatch"};
    for (size_t variant = 0; variant < errors.size(); ++variant) {
        // Each failed response erases k1/mx; rebuild a fresh exchange per variant.
        initialize();
        ASSERT_FALSE(HasFatalFailure());
        std::vector<uint8_t> response, confirm{0xff};
        make_response(response, variant == 0,
                      variant == 1 ? dpp::k_dpp_capab_configurator : dpp::k_dpp_capab_enrollee,
                      variant == 2);
        ASSERT_FALSE(HasFatalFailure());
        EXPECT_FALSE(session.handle_authentication_response(response, confirm, error));
        EXPECT_EQ(errors[variant], error);
        EXPECT_TRUE(confirm.empty());
        EXPECT_FALSE(session.authentication_success());
    }
}

// PUBLIC_INTERFACE
/** JSON role validation is exercised after decryption, not with artificial private state. */
TEST_F(DppExchangeTest, ConfigurationRequestRoleValidation)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    const std::vector<std::string> inputs{"{", "{}", R"({"netRole":4})",
                                          R"({"netRole":"unknown"})"};
    const std::vector<std::string> errors{"Invalid DPP Configuration Request JSON",
                                          "DPP Configuration Request missing netRole",
                                          "DPP Configuration Request missing netRole",
                                          "Unsupported DPP Configuration Request netRole"};
    for (size_t i = 0; i < inputs.size(); ++i) {
        std::vector<uint8_t> frame;
        configuration_request(inputs[i], frame);
        ASSERT_FALSE(HasFatalFailure());
        std::string json, role = "stale";
        EXPECT_FALSE(session.unwrap_configuration_request(frame, json, role, error));
        EXPECT_EQ(errors[i], error);
        EXPECT_TRUE(role.empty());
    }
    std::vector<uint8_t> frame;
    const std::string valid = R"({"netRole":"MAPAGENT"})";
    configuration_request(valid, frame);
    ASSERT_FALSE(HasFatalFailure());
    std::string json, role;
    ASSERT_TRUE(session.unwrap_configuration_request(frame, json, role, error)) << error;
    EXPECT_EQ(valid, json);
    EXPECT_EQ("mapAgent", role);
}

// PUBLIC_INTERFACE
/** Multiple configuration objects and the empty Send Connection Status attribute are preserved. */
TEST_F(DppExchangeTest, ConfigurationResponseObjectsAndGas)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    activate_configuration();
    ASSERT_FALSE(HasFatalFailure());
    const std::vector<std::string> objects{R"({"wi-fi_tech":"map"})", R"({"wi-fi_tech":"infra"})"};
    std::vector<uint8_t> response, clear;
    ASSERT_TRUE(session.build_configuration_response(objects, response, error, true)) << error;
    dpp::DppAttributeView wrapped, nonce, send;
    ASSERT_TRUE(
        dpp::get_dpp_attr(response.data(), response.size(), dpp::k_dpp_attr_wrapped_data, wrapped));
    ASSERT_TRUE(dpp::aes_siv_decrypt_vector(
        ke, wrapped.data, wrapped.len,
        {std::vector<uint8_t>(response.cbegin(),
                              response.cbegin() + (wrapped.header - response.data()))},
        clear));
    ASSERT_TRUE(dpp::parse_required_attr(clear.data(), clear.size(), dpp::k_dpp_attr_enrollee_nonce,
                                         nonce_len, nonce));
    EXPECT_TRUE(std::equal(e_nonce.begin(), e_nonce.end(), nonce.data));
    size_t offset = 4 + nonce.len;
    for (const auto &object : objects) {
        dpp::DppAttributeView config;
        ASSERT_TRUE(dpp::get_dpp_attr(clear.data() + offset, clear.size() - offset,
                                      dpp::k_dpp_attr_config_obj, config));
        EXPECT_EQ(object, std::string(reinterpret_cast<const char *>(config.data), config.len));
        offset = static_cast<size_t>(config.header - clear.data()) + 4 + config.len;
    }
    ASSERT_TRUE(
        dpp::get_dpp_attr(clear.data(), clear.size(), dpp::k_dpp_attr_send_conn_status, send));
    EXPECT_EQ(0U, send.len);
    std::vector<uint8_t> gas;
    ASSERT_TRUE(session.build_gas_configuration_response(objects, gas, error, true)) << error;
    EXPECT_EQ(dpp::wrap_dpp_response_in_gas(response), gas);
    EXPECT_FALSE(session.build_configuration_response(std::vector<std::string>{}, response, error));
    EXPECT_EQ("DPP Configuration Response missing Config Object", error);
    EXPECT_TRUE(response.empty());
    EXPECT_FALSE(
        session.build_configuration_response(std::vector<std::string>{""}, response, error));
    EXPECT_EQ("DPP Configuration Response has empty Config Object", error);
}

// PUBLIC_INTERFACE
/** Connector payloads contain the authenticated peer key, default group and optional UTC expiry. */
TEST_F(DppExchangeTest, ConnectorPayloadUsesAuthenticatedPeerKey)
{
    std::string payload = "stale";
    EXPECT_FALSE(session.build_connector_payload("mapAgent", "", 0, payload, error));
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ("DPP connector payload requested before authentication success", error);
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    for (uint32_t expiry : {uint32_t(0), uint32_t(1)}) {
        ASSERT_TRUE(session.build_connector_payload("mapAgent", "", expiry, payload, error))
            << error;
        std::unique_ptr<json_object, decltype(&json_object_put)> root(
            json_tokener_parse(payload.c_str()), json_object_put);
        ASSERT_NE(nullptr, root.get());
        json_object *groups = nullptr, *jwk = nullptr, *value = nullptr;
        ASSERT_TRUE(json_object_object_get_ex(root.get(), "groups", &groups));
        ASSERT_EQ(1U, json_object_array_length(groups));
        auto *group = json_object_array_get_idx(groups, 0);
        ASSERT_TRUE(json_object_object_get_ex(group, "groupId", &value));
        EXPECT_STREQ("*", json_object_get_string(value));
        ASSERT_TRUE(json_object_object_get_ex(group, "netRole", &value));
        EXPECT_STREQ("mapAgent", json_object_get_string(value));
        ASSERT_TRUE(json_object_object_get_ex(root.get(), "netAccessKey", &jwk));
        ASSERT_TRUE(json_object_object_get_ex(jwk, "x", &value));
        EXPECT_EQ(dpp::base64url_encode(pr_x.data(), pr_x.size()),
                  std::string(json_object_get_string(value)));
        EXPECT_FALSE(json_object_object_get_ex(jwk, "d", &value));
        EXPECT_EQ(expiry != 0, json_object_object_get_ex(root.get(), "expiry", &value));
        if (expiry != 0) {
            EXPECT_STREQ("1970-01-01T00:00:01Z", json_object_get_string(value));
        }
    }
    session.reset();
    EXPECT_FALSE(session.build_connector_payload("mapAgent", "", 0, payload, error));
    EXPECT_TRUE(payload.empty());
}

// PUBLIC_INTERFACE
/** GAS decoding delegates to real authenticated request parsing and clears malformed outputs. */
TEST_F(DppExchangeTest, GasConfigurationRequestDecoding)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    const std::string expected = R"({"netRole":"mapAgent"})";
    std::vector<uint8_t> attrs;
    configuration_request(expected, attrs);
    ASSERT_FALSE(HasFatalFailure());
    for (bool action : {false, true}) {
        std::vector<uint8_t> gas;
        if (action) {
            gas.push_back(0x0a);
        }
        gas.insert(gas.end(), {0x42, 0x6c, 8, 0x7f, 0xdd, 5, 0x50, 0x6f, 0x9a, 0x1a, 1});
        dpp::append_le16(gas, static_cast<uint16_t>(attrs.size()));
        gas.insert(gas.end(), attrs.begin(), attrs.end());
        std::string json, role;
        ASSERT_TRUE(session.unwrap_gas_configuration_request(gas, json, role, error)) << error;
        EXPECT_EQ(expected, json);
        EXPECT_EQ("mapAgent", role);
        EXPECT_TRUE(error.empty());

        const std::vector<std::string> objects{R"({"wi-fi_tech":"map"})"};
        std::vector<uint8_t> response, gas_response;
        ASSERT_TRUE(session.build_configuration_response(objects, response, error)) << error;
        ASSERT_TRUE(session.build_gas_configuration_response(objects, gas_response, error))
            << error;
        EXPECT_EQ(dpp::wrap_dpp_response_in_gas(response, 0x42), gas_response);
    }
    std::string json = "stale", role = "stale";
    EXPECT_FALSE(session.unwrap_gas_configuration_request({0x0a}, json, role, error));
    EXPECT_TRUE(json.empty());
    EXPECT_TRUE(role.empty());
    EXPECT_EQ("Failed extracting DPP attributes from GAS Configuration Request", error);
}

// PUBLIC_INTERFACE
/** Result status and nonce are verified; replay after session reset is refused. */
TEST_F(DppExchangeTest, ConfigurationResultAndReplayAfterReset)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    activate_configuration();
    ASSERT_FALSE(HasFatalFailure());
    std::vector<uint8_t> clear, frame;
    dpp::append_dpp_attr_u8(clear, dpp::k_dpp_attr_status, 7);
    dpp::append_dpp_attr(clear, dpp::k_dpp_attr_enrollee_nonce, e_nonce.data(), e_nonce.size());
    result_frame(dpp::k_dpp_configuration_result, clear, frame);
    ASSERT_FALSE(HasFatalFailure());
    uint8_t status = 0;
    ASSERT_TRUE(session.unwrap_configuration_result(frame, status, error)) << error;
    EXPECT_EQ(7, status);
    clear.back() ^= 1;
    result_frame(dpp::k_dpp_configuration_result, clear, frame);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_FALSE(session.unwrap_configuration_result(frame, status, error));
    EXPECT_EQ("DPP Configuration Result nonce mismatch", error);
    session.reset();
    EXPECT_FALSE(session.unwrap_configuration_result(frame, status, error));
    EXPECT_EQ("DPP Configuration Result without authenticated controller session", error);
}

// PUBLIC_INTERFACE
/** Connection-status result accepts the byte range only and requires an integer JSON field. */
TEST_F(DppExchangeTest, ConnectionStatusJsonValidation)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    activate_configuration();
    ASSERT_FALSE(HasFatalFailure());
    const std::vector<std::string> values{"{",
                                          "{}",
                                          R"({"result":"0"})",
                                          R"({"result":-1})",
                                          R"({"result":256})",
                                          R"({"result":0})",
                                          R"({"result":255})"};
    for (size_t i = 0; i < values.size(); ++i) {
        std::vector<uint8_t> clear, frame;
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_enrollee_nonce, e_nonce.data(), e_nonce.size());
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_conn_status,
                             reinterpret_cast<const uint8_t *>(values[i].data()), values[i].size());
        result_frame(dpp::k_dpp_connection_status_result, clear, frame);
        ASSERT_FALSE(HasFatalFailure());
        uint8_t result      = 99;
        const bool accepted = session.unwrap_connection_status_result(frame, result, error);
        EXPECT_EQ(i >= 5, accepted) << values[i];
        if (accepted) {
            EXPECT_EQ(i == 5 ? 0 : 255, result);
            EXPECT_TRUE(error.empty());
        } else {
            EXPECT_FALSE(error.empty());
            EXPECT_EQ(0, result);
        }
    }
}

// PUBLIC_INTERFACE
/** A duplicate Auth Response cannot emit a second Confirm or reset successful authentication. */
TEST_F(DppExchangeTest, DuplicateAuthenticationResponseDoesNotSendConfirm)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    std::vector<uint8_t> response, confirm{0xff};
    make_response(response);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_FALSE(session.handle_authentication_response(response, confirm, error));
    EXPECT_EQ("DPP Authentication Response ignored; authentication already complete", error);
    EXPECT_TRUE(confirm.empty());
    EXPECT_TRUE(session.authentication_success());
}

// PUBLIC_INTERFACE
/** Configuration APIs reject unauthenticated sessions and authenticated sessions without a request. */
TEST_F(DppExchangeTest, ConfigurationStateGuardsClearOutputs)
{
    for (bool authenticated : {false, true}) {
        if (authenticated) {
            initialize();
            ASSERT_FALSE(HasFatalFailure());
            authenticate();
            ASSERT_FALSE(HasFatalFailure());
        }
        std::vector<uint8_t> response{0xff};
        EXPECT_FALSE(session.build_configuration_response(std::string("{}"), response, error));
        EXPECT_EQ("DPP Configuration Response without active request", error);
        EXPECT_TRUE(response.empty());
        response = {0xff};
        EXPECT_FALSE(session.build_gas_configuration_response({"{}"}, response, error));
        EXPECT_TRUE(response.empty());
        uint8_t status = 99;
        EXPECT_FALSE(session.unwrap_configuration_result({}, status, error));
        EXPECT_EQ("DPP Configuration Result without authenticated controller session", error);
        EXPECT_EQ(0, status);
        status = 99;
        EXPECT_FALSE(session.unwrap_connection_status_result({}, status, error));
        EXPECT_EQ("DPP Connection Status Result without authenticated controller session", error);
        EXPECT_EQ(0, status);
    }
    session.reset();
    std::string json = "stale", role = "stale";
    EXPECT_FALSE(session.unwrap_configuration_request({}, json, role, error));
    EXPECT_EQ("DPP Configuration Request without authenticated controller session", error);
    EXPECT_TRUE(json.empty());
    EXPECT_TRUE(role.empty());
}

// PUBLIC_INTERFACE
/** Valid outer encryption cannot hide missing nonce, capabilities, nested data or R-auth. */
TEST_F(DppExchangeTest, RejectsIncompleteEncryptedAuthenticationAttributes)
{
    for (size_t variant = 0; variant < 8; ++variant) {
        SCOPED_TRACE(variant);
        // Each failed response erases k1/mx; rebuild a fresh exchange per variant.
        initialize();
        ASSERT_FALSE(HasFatalFailure());
        std::vector<uint8_t> clear, secondary, nested, response, confirm{0xff};
        if (variant != 0) {
            dpp::append_dpp_attr(clear, dpp::k_dpp_attr_r_nonce, r_nonce.data(), r_nonce.size());
        }
        if (variant != 1) {
            dpp::append_dpp_attr(clear, dpp::k_dpp_attr_i_nonce, i_nonce.data(), i_nonce.size());
        }
        if (variant != 2) {
            dpp::append_dpp_attr_u8(clear, dpp::k_dpp_attr_r_capabilities,
                                    dpp::k_dpp_capab_enrollee);
        }
        if (variant < 6) {
            std::vector<uint8_t> tag(hash_len, 0x42);
            dpp::append_dpp_attr(secondary, dpp::k_dpp_attr_r_auth_tag, tag.data(), tag.size());
        } else {
            // Valid nested encryption with an absent or incorrectly sized R-auth.
            dpp::append_dpp_attr_u8(
                secondary, variant == 6 ? dpp::k_dpp_attr_status : dpp::k_dpp_attr_r_auth_tag, 0);
        }
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(ke, secondary, {}, nested));
        if (variant == 4) {
            nested.resize(15);
        } else if (variant == 5) {
            nested[0] ^= 1;
        }
        if (variant != 3) {
            dpp::append_dpp_attr(clear, dpp::k_dpp_attr_wrapped_data, nested.data(), nested.size());
        }
        response_with_clear(clear, response);
        ASSERT_FALSE(HasFatalFailure());
        EXPECT_FALSE(session.handle_authentication_response(response, confirm, error));
        EXPECT_EQ(variant <= 4 ? "DPP Authentication Response has incomplete wrapped attributes"
                               : variant == 5 ? "Failed unwrapping DPP responder authentication tag"
                                              : "DPP Authentication Response missing R-auth",
                  error);
        EXPECT_TRUE(confirm.empty());
        EXPECT_FALSE(session.authentication_success());
    }
}

// PUBLIC_INTERFACE
/** Request guards distinguish missing ciphertext, failed authentication and incomplete cleartext. */
TEST_F(DppExchangeTest, ConfigurationRequestAttributeGuardsAndPublicActionLayout)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    for (size_t variant = 0; variant < 7; ++variant) {
        SCOPED_TRACE(variant);
        std::vector<uint8_t> clear, wrapped, frame;
        if (variant >= 4) {
            dpp::append_dpp_attr(clear, dpp::k_dpp_attr_enrollee_nonce, e_nonce.data(),
                                 e_nonce.size());
        }
        if (variant == 3 || variant == 5) {
            const std::string json = R"({"netRole":"sta"})";
            dpp::append_dpp_attr(clear, dpp::k_dpp_attr_config_attr_obj,
                                 reinterpret_cast<const uint8_t *>(json.data()), json.size());
        } else if (variant == 6) {
            dpp::append_dpp_attr(clear, dpp::k_dpp_attr_config_attr_obj, nullptr, 0);
        }
        // Ensure the plaintext is nonempty even when deliberately omitting mandatory fields.
        dpp::append_dpp_attr_u8(clear, dpp::k_dpp_attr_status, 0);
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(ke, clear, {}, wrapped));
        if (variant == 1) {
            wrapped.resize(15);
        } else if (variant == 2) {
            wrapped[0] ^= 1;
        }
        if (variant != 0) {
            dpp::append_dpp_attr(frame, dpp::k_dpp_attr_wrapped_data, wrapped.data(),
                                 wrapped.size());
        }
        std::string json = "stale", role = "stale";
        const bool accepted = session.unwrap_configuration_request(frame, json, role, error);
        EXPECT_EQ(variant == 5, accepted);
        if (accepted) {
            EXPECT_EQ("sta", role);
            std::vector<uint8_t> full;
            dpp::build_dpp_public_action_prefix(0, full);
            full.insert(full.end(), frame.begin(), frame.end());
            ASSERT_TRUE(session.unwrap_configuration_request(full, json, role, error)) << error;
            EXPECT_EQ("sta", role);
            std::vector<uint8_t> response;
            ASSERT_TRUE(session.build_configuration_response(std::string("{}"), response, error));
        } else {
            EXPECT_EQ(variant <= 1
                          ? "DPP Configuration Request missing Wrapped Data"
                          : variant == 2 ? "Failed unwrapping DPP Configuration Request"
                                         : "DPP Configuration Request has incomplete attributes",
                      error);
            EXPECT_TRUE(json.empty());
            EXPECT_TRUE(role.empty());
        }
    }
}

// PUBLIC_INTERFACE
/** Both result parsers enforce headers, ciphertext authentication, mandatory fields and nonce. */
TEST_F(DppExchangeTest, ResultFrameValidationStages)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    authenticate();
    ASSERT_FALSE(HasFatalFailure());
    activate_configuration();
    ASSERT_FALSE(HasFatalFailure());
    for (bool connection : {false, true}) {
        const uint8_t type =
            connection ? dpp::k_dpp_connection_status_result : dpp::k_dpp_configuration_result;
        const std::string label =
            connection ? "DPP Connection Status Result" : "DPP Configuration Result";
        for (size_t variant = 0; variant < 8; ++variant) {
            SCOPED_TRACE(label);
            SCOPED_TRACE(variant);
            std::vector<uint8_t> clear, frame;
            auto nonce = e_nonce;
            if (variant == 7) {
                nonce[0] ^= 1;
            }
            if (variant != 4) {
                dpp::append_dpp_attr(clear, dpp::k_dpp_attr_enrollee_nonce, nonce.data(),
                                     nonce.size());
            }
            if (variant != 5) {
                const std::string json = R"({"result":0})";
                if (connection) {
                    dpp::append_dpp_attr(clear, dpp::k_dpp_attr_conn_status,
                                         reinterpret_cast<const uint8_t *>(json.data()),
                                         variant == 6 ? 0 : json.size());
                } else {
                    const uint8_t status[] = {0, 0};
                    dpp::append_dpp_attr(clear, dpp::k_dpp_attr_status, status,
                                         variant == 6 ? 2 : 1);
                }
            }
            result_frame(type, clear, frame);
            ASSERT_FALSE(HasFatalFailure());
            if (variant == 0) {
                frame[7] ^= 1;
            } else if (variant == 1) {
                frame.resize(8);
            } else if (variant == 2) {
                frame.resize(8);
                const std::vector<uint8_t> short_cipher(15);
                dpp::append_dpp_attr(frame, dpp::k_dpp_attr_wrapped_data, short_cipher.data(),
                                     short_cipher.size());
            } else if (variant == 3) {
                frame.back() ^= 1;
            }
            uint8_t status = 99;
            const bool accepted =
                connection ? session.unwrap_connection_status_result(frame, status, error)
                           : session.unwrap_configuration_result(frame, status, error);
            EXPECT_FALSE(accepted);
            EXPECT_EQ(0, status);
            const std::string expected =
                variant == 0
                    ? "Invalid " + label + " frame"
                    : variant <= 2
                          ? label + " missing Wrapped Data"
                          : variant == 3 ? "Failed unwrapping " + label
                                         : variant <= 6 ? label + " has incomplete attributes"
                                                        : label + " nonce mismatch";
            EXPECT_EQ(expected, error);
            EXPECT_TRUE(session.authentication_success());
        }
    }
}

const std::string k_map_agent_request =
    R"({"name":"Agent","wi-fi_tech":"map","netRole":"mapAgent","bSTAList":[{"netRole":"mapBackhaulSta","akm":"506F9A02+000FAC08","channelList":"81/1"}]})";

// PUBLIC_INTERFACE
/**
 * Policy outcome of build_dpp_configuration_objects_from_policy() for an authenticated session,
 * a known Agent and configured BSSs. The key-injected overload avoids the DPP keystore.
 */
class DppPolicyObjectsTest : public DppExchangeTest {
protected:
    /** Fields of one Configuration Object that the policy decides. */
    struct PolicyObject {
        std::string wifi_tech, net_role, ssid, akm, pass;
        int df_counter_threshold = -1;
        bool has_discovery = false, has_mld_config = false, signed_connector = false,
             has_csign = false;

        /** Comparable summary; signatures and keys differ per build, the policy does not. */
        std::string describe() const
        {
            return wifi_tech + "|" + net_role + "|" + std::to_string(df_counter_threshold) + "|" +
                   (has_discovery ? "ssid=" + ssid : "no-discovery") + "|" + akm + "|" + pass +
                   (has_mld_config ? "|mld" : "");
        }
    };

    void SetUp() override
    {
        log_config.sLog.global_levels = "error,fatal,warning";
        log_config.sLog.syslog_levels = "off";
        logger.reset(new beerocks::logging("dpp-test", log_config.sLog));
        database.reset(
            new son::db(db_config, *logger, tlvf::mac_from_string("02:00:00:00:00:01"), nullptr));
        database->m_agents.add(agent_mac);
        ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, csign));
        initialize();
        ASSERT_FALSE(HasFatalFailure());
        authenticate();
        ASSERT_FALSE(HasFatalFailure());
    }

    void add_agent_bss(const std::string &ssid, bool backhaul, WSC::eWscAuth auth,
                       const std::string &key)
    {
        son::wireless_utils::sBssInfoConf bss{};
        bss.ssid                = ssid;
        bss.fronthaul           = !backhaul;
        bss.backhaul            = backhaul;
        bss.authentication_type = auth;
        bss.network_key         = key;
        database->add_bss_info_configuration(agent_mac, bss);
    }

    static std::string text(json_object *parent, const char *name)
    {
        json_object *value = nullptr;
        if (!parent || !json_object_object_get_ex(parent, name, &value) ||
            !json_object_is_type(value, json_type_string)) {
            return {};
        }
        return json_object_get_string(value);
    }

    static PolicyObject parse_object(const std::string &object_json)
    {
        PolicyObject out;
        std::unique_ptr<json_object, decltype(&json_object_put)> root(
            json_tokener_parse(object_json.c_str()), json_object_put);
        EXPECT_NE(nullptr, root.get()) << object_json;
        if (!root) {
            return out;
        }
        out.wifi_tech      = text(root.get(), "wi-fi_tech");
        out.net_role       = text(root.get(), "netRole");
        json_object *value = nullptr;
        if (json_object_object_get_ex(root.get(), "dfCounterThreshold", &value)) {
            out.df_counter_threshold = json_object_get_int(value);
        }
        json_object *discovery = nullptr;
        out.has_discovery      = json_object_object_get_ex(root.get(), "discovery", &discovery);
        out.has_mld_config =
            json_object_object_get_ex(root.get(), "Backhaul_STA_MLD_Config", nullptr) ||
            (discovery && json_object_object_get_ex(discovery, "Backhaul_STA_MLD_Config", nullptr));
        out.ssid          = text(discovery, "ssid");
        json_object *cred = nullptr;
        if (json_object_object_get_ex(root.get(), "cred", &cred)) {
            out.akm              = text(cred, "akm");
            out.pass             = text(cred, "pass");
            const auto connector = text(cred, "signedConnector");
            out.signed_connector = std::count(connector.begin(), connector.end(), '.') == 2;
            out.has_csign        = json_object_object_get_ex(cred, "csign", nullptr);
        }
        return out;
    }

    /** Run the policy builder and decode every produced Configuration Object. */
    bool build(const std::string &request, std::vector<PolicyObject> &objects,
               bool &send_conn_status)
    {
        std::vector<std::string> raw{"stale"};
        send_conn_status = true;
        const bool ok    = dpp::build_dpp_configuration_objects_from_policy(
            *database, session, agent_mac, request, csign.get(), {}, {}, raw, send_conn_status,
            error);
        objects.clear();
        for (const auto &object_json : raw) {
            objects.push_back(parse_object(object_json));
        }
        return ok;
    }

    const sMacAddr agent_mac = tlvf::mac_from_string("02:00:00:00:00:02");
    beerocks::config_file::sConfigMaster log_config{};
    son::db::sDbMasterConfig db_config{};
    std::unique_ptr<beerocks::logging> logger;
    std::unique_ptr<son::db> database;
    dpp::DppKeyPtr csign;
};

// PUBLIC_INTERFACE
/** mapAgent + bSTAList: mapAgent object without discovery, then the backhaul bSTA object. */
TEST_F(DppPolicyObjectsTest, MapAgentWithBackhaulStaBuildsBothObjects)
{
    add_agent_bss("Fronthaul", false, WSC::eWscAuth::WSC_AUTH_WPA2PSK, "fronthaul-pass");
    add_agent_bss("Backhaul", true, WSC::eWscAuth::WSC_AUTH_SAE, "backhaul-pass");
    std::vector<PolicyObject> objects;
    bool send_conn_status = false;
    ASSERT_TRUE(build(k_map_agent_request, objects, send_conn_status)) << error;
    ASSERT_EQ(2u, objects.size());
    EXPECT_EQ("map|mapAgent|3|no-discovery|dpp|", objects[0].describe());
    EXPECT_EQ("map|mapBackhaulSta|-1|ssid=Backhaul|sae|backhaul-pass", objects[1].describe());
    for (const auto &object : objects) {
        EXPECT_TRUE(object.signed_connector);
        EXPECT_TRUE(object.has_csign);
    }
    EXPECT_TRUE(send_conn_status);
}

// PUBLIC_INTERFACE
/** A plain sta request gets the fronthaul BSS and no Connection Status Result request. */
TEST_F(DppPolicyObjectsTest, StaOnlyRequestUsesFronthaulBss)
{
    add_agent_bss("Backhaul", true, WSC::eWscAuth::WSC_AUTH_SAE, "backhaul-pass");
    add_agent_bss("Fronthaul", false, WSC::eWscAuth::WSC_AUTH_WPA2PSK, "fronthaul-pass");
    std::vector<PolicyObject> objects;
    bool send_conn_status = true;
    ASSERT_TRUE(build(R"({"wi-fi_tech":"infra","netRole":"sta"})", objects, send_conn_status))
        << error;
    ASSERT_EQ(1u, objects.size());
    EXPECT_EQ("infra|sta|-1|ssid=Fronthaul|psk|fronthaul-pass", objects[0].describe());
    EXPECT_TRUE(objects[0].signed_connector);
    EXPECT_FALSE(send_conn_status);
}

// PUBLIC_INTERFACE
/** bSTAList without mapAgent: only the bSTA object, as wi-fi_tech infra for wpa_supplicant. */
TEST_F(DppPolicyObjectsTest, BackhaulStaOnlyRequestUsesInfraTech)
{
    add_agent_bss("Backhaul", true, WSC::eWscAuth::WSC_AUTH_SAE, "backhaul-pass");
    std::vector<PolicyObject> objects;
    bool send_conn_status = false;
    ASSERT_TRUE(
        build(R"({"wi-fi_tech":"map","bSTAList":[{"netRole":"mapBackhaulSta","akm":"000FAC08"}]})",
              objects, send_conn_status))
        << error;
    ASSERT_EQ(1u, objects.size());
    EXPECT_EQ("infra|mapBackhaulSta|-1|ssid=Backhaul|sae|backhaul-pass", objects[0].describe());
    EXPECT_TRUE(send_conn_status);
}

// PUBLIC_INTERFACE
/**
 * Selection policy: the first configured backhaul BSS is authoritative. An incompatible AKM
 * fails the whole build (no partial mapAgent object) instead of trying the next candidate.
 */
TEST_F(DppPolicyObjectsTest, FirstBackhaulCandidateIsAuthoritative)
{
    add_agent_bss("Legacy", true, WSC::eWscAuth::WSC_AUTH_WPA2PSK, "legacy-pass");
    add_agent_bss("Wpa3", true, WSC::eWscAuth::WSC_AUTH_SAE, "wpa3-pass");
    std::vector<PolicyObject> objects;
    bool send_conn_status = true;
    EXPECT_FALSE(build(
        R"({"netRole":"mapAgent","bSTAList":[{"netRole":"mapBackhaulSta","akm":"000FAC08"}]})",
        objects, send_conn_status));
    EXPECT_EQ("Configured backhaul AKM is not compatible with DPP Configuration Request", error);
    EXPECT_TRUE(objects.empty());
    EXPECT_FALSE(send_conn_status);

    ASSERT_TRUE(build(
        R"({"netRole":"mapAgent","bSTAList":[{"netRole":"mapBackhaulSta","akm":"506F9A02+000FAC02+000FAC08"}]})",
        objects, send_conn_status))
        << error;
    ASSERT_EQ(2u, objects.size());
    EXPECT_EQ("map|mapBackhaulSta|-1|ssid=Legacy|psk|legacy-pass", objects[1].describe());
    EXPECT_TRUE(send_conn_status);
}

// PUBLIC_INTERFACE
/**
 * bSTA_Maximum_Links / RadioList are not applied: an MLO-capable request gets the same
 * single-link objects as a plain request, and no Backhaul_STA_MLD_Config.
 */
TEST_F(DppPolicyObjectsTest, BackhaulMloMetadataFallsBackToSingleLink)
{
    add_agent_bss("Backhaul", true, WSC::eWscAuth::WSC_AUTH_SAE, "backhaul-pass");
    std::vector<PolicyObject> single_link, mlo;
    bool send_conn_status = false;
    ASSERT_TRUE(build(k_map_agent_request, single_link, send_conn_status)) << error;
    ASSERT_TRUE(build(
        R"({"name":"Agent","wi-fi_tech":"map","netRole":"mapAgent","bSTAList":[{"netRole":"mapBackhaulSta","akm":"506F9A02+000FAC08","channelList":"81/1","bSTA_Maximum_Links":2,"RadioList":[{"RUID":"020000000010","bSTA_STR_Support":true},{"RUID":"020000000011","bSTA_EMLSR_Support":true}]}]})",
        mlo, send_conn_status))
        << error;
    ASSERT_EQ(single_link.size(), mlo.size());
    for (size_t i = 0; i < mlo.size(); ++i) {
        EXPECT_EQ(single_link[i].describe(), mlo[i].describe());
        EXPECT_FALSE(mlo[i].has_mld_config);
    }
    EXPECT_TRUE(send_conn_status);
}

// PUBLIC_INTERFACE
/** The key-injected overload still validates the request first, then requires a C-sign key. */
TEST_F(DppPolicyObjectsTest, InjectedKeyOverloadRequiresCsignKey)
{
    add_agent_bss("Backhaul", true, WSC::eWscAuth::WSC_AUTH_SAE, "backhaul-pass");
    std::vector<std::string> objects{"stale"};
    bool send_conn_status = true;
    EXPECT_FALSE(dpp::build_dpp_configuration_objects_from_policy(
        *database, session, agent_mac, "{}", nullptr, {}, {}, objects, send_conn_status, error));
    EXPECT_EQ("DPP Configuration Request contained no netRole", error);
    EXPECT_FALSE(dpp::build_dpp_configuration_objects_from_policy(
        *database, session, agent_mac, k_map_agent_request, nullptr, {}, {}, objects,
        send_conn_status, error));
    EXPECT_EQ("Missing DPP Configurator C-sign key", error);
    EXPECT_TRUE(objects.empty());
    EXPECT_FALSE(send_conn_status);
}

} // namespace
