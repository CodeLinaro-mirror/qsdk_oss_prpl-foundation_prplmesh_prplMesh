/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include "../../controller_dpp_configuration.h"
#include "../../controller_dpp_csign.h"
#include "../../dpp/dpp_internal.h"
#include "../db.h"

#include <gtest/gtest.h>
#include <json-c/json.h>
#include <mapf/common/utils.h>
#include <openssl/obj_mac.h>
#include <openssl/x509.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

namespace {
std::string install_root;
}

// CMake renames references to get_install_path only in this executable, including
// db.cpp. No production library or installed keystore is modified.
namespace mapf {
namespace utils {
std::string dpp_test_install_path() { return install_root; }
} // namespace utils
} // namespace mapf

namespace {
namespace dpp = son::controller_dpp;

class DppCsignTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        char pattern[]       = "/tmp/prplmesh-csign-test-XXXXXX";
        const auto directory = mkdtemp(pattern);
        ASSERT_NE(nullptr, directory);
        root         = directory;
        install_root = root + "/";
        path         = root + "/share/prplmesh_dpp_keystore";
        ASSERT_EQ(path, son::db::get_dpp_store_path());
        ASSERT_EQ(0, mkdir((root + "/share").c_str(), 0700));
        logging_config.sLog.global_levels = "error,fatal,warning";
        logging_config.sLog.syslog_levels = "off";
        logger.reset(new beerocks::logging("dpp-csign-test", logging_config.sLog));
        database.reset(
            new son::db(config, *logger, tlvf::mac_from_string("02:00:00:00:00:01"), nullptr));
    }

    void TearDown() override
    {
        database.reset();
        logger.reset();
        if (!root.empty()) {
            // Only fixture-owned paths, including failure-injection directories.
            std::remove((path + ".tmp").c_str());
            rmdir((path + ".tmp").c_str());
            std::remove(path.c_str());
            rmdir((root + "/share").c_str());
            rmdir(root.c_str());
        }
        install_root.clear();
    }

    void write(const std::string &text)
    {
        ASSERT_EQ(path, son::db::get_dpp_store_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << text;
        file.close();
        ASSERT_TRUE(file);
    }

    std::string read()
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    void generate()
    {
        bool generated = false;
        ASSERT_TRUE(dpp::generate_dpp_configurator_csign_key(*database, generated, error)) << error;
        ASSERT_TRUE(generated);
        ASSERT_TRUE(error.empty());
    }

    // Complete a real authentication exchange through public session APIs.
    void authenticate(dpp::DppConfiguratorSession &session)
    {
        dpp::DppKeyPtr bootstrap, responder, initiator;
        ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, bootstrap));
        ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, responder));
        const int length = i2d_PUBKEY(bootstrap.get(), nullptr);
        ASSERT_GT(length, 0);
        std::vector<uint8_t> der(length);
        auto *ptr = der.data();
        ASSERT_EQ(length, i2d_PUBKEY(bootstrap.get(), &ptr));
        std::vector<uint8_t> request;
        ASSERT_TRUE(
            session.start(dpp::base64url_encode(der.data(), der.size()), 2, request, error));
        const uint8_t *header = nullptr, *attrs = nullptr;
        size_t attrs_len = 0;
        ASSERT_TRUE(dpp::split_dpp_public_action_frame(request, 0, header, attrs, attrs_len));
        dpp::DppAttributeView xy, wrapped, nonce, hash;
        ASSERT_TRUE(
            dpp::parse_required_attr(attrs, attrs_len, dpp::k_dpp_attr_i_protocol_key, 64, xy));
        ASSERT_TRUE(dpp::ec_public_from_xy(NID_X9_62_prime256v1, xy.data, xy.len, initiator));
        ASSERT_TRUE(
            dpp::parse_required_attr(attrs, attrs_len, dpp::k_dpp_attr_r_bootstrap_hash, 32, hash));
        std::vector<uint8_t> bootstrap_hash(hash.data, hash.data + hash.len);
        std::vector<uint8_t> mx, nx, k1, k2, clear, i_nonce, r_nonce(16, 0x73), bk, ke;
        ASSERT_TRUE(dpp::ecdh_secret(bootstrap.get(), initiator.get(), 32, mx));
        ASSERT_TRUE(
            dpp::derive_intermediate_key(EVP_sha256(), mx, "first intermediate key", 32, k1));
        ASSERT_TRUE(dpp::get_dpp_attr(attrs, attrs_len, dpp::k_dpp_attr_wrapped_data, wrapped));
        ASSERT_TRUE(dpp::aes_siv_decrypt_vector(
            k1, wrapped.data, wrapped.len,
            {std::vector<uint8_t>(header, header + 6), std::vector<uint8_t>(attrs, wrapped.header)},
            clear));
        ASSERT_TRUE(dpp::parse_required_attr(clear.data(), clear.size(), dpp::k_dpp_attr_i_nonce,
                                             16, nonce));
        i_nonce.assign(nonce.data, nonce.data + nonce.len);
        ASSERT_TRUE(dpp::ecdh_secret(responder.get(), initiator.get(), 32, nx));
        ASSERT_TRUE(
            dpp::derive_intermediate_key(EVP_sha256(), nx, "second intermediate key", 32, k2));
        ASSERT_TRUE(dpp::derive_bk_ke(EVP_sha256(), i_nonce, r_nonce, mx, nx, 32, bk, ke));
        std::vector<uint8_t> pi_x, pr_x, br_x, tag, secondary, encrypted, response, confirm;
        ASSERT_TRUE(dpp::ec_public_x(initiator.get(), 32, pi_x));
        ASSERT_TRUE(dpp::ec_public_x(responder.get(), 32, pr_x));
        ASSERT_TRUE(dpp::ec_public_x(bootstrap.get(), 32, br_x));
        ASSERT_TRUE(dpp::hash_vector(EVP_sha256(), {i_nonce, r_nonce, pi_x, pr_x, br_x, {0}}, tag));
        dpp::append_dpp_attr(secondary, dpp::k_dpp_attr_r_auth_tag, tag.data(), tag.size());
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(ke, secondary, {}, encrypted));
        clear.clear();
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_r_nonce, r_nonce.data(), r_nonce.size());
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_i_nonce, i_nonce.data(), i_nonce.size());
        dpp::append_dpp_attr_u8(clear, dpp::k_dpp_attr_r_capabilities, dpp::k_dpp_capab_enrollee);
        dpp::append_dpp_attr(clear, dpp::k_dpp_attr_wrapped_data, encrypted.data(),
                             encrypted.size());
        dpp::build_dpp_public_action_prefix(1, response);
        dpp::append_dpp_attr_u8(response, dpp::k_dpp_attr_status, 0);
        dpp::append_dpp_attr(response, dpp::k_dpp_attr_r_bootstrap_hash, bootstrap_hash.data(),
                             bootstrap_hash.size());
        std::vector<uint8_t> responder_xy;
        ASSERT_TRUE(dpp::ec_public_xy(responder.get(), 32, responder_xy));
        dpp::append_dpp_attr(response, dpp::k_dpp_attr_r_protocol_key, responder_xy.data(),
                             responder_xy.size());
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(
            k2, clear,
            {std::vector<uint8_t>(response.begin() + 2, response.begin() + 8),
             std::vector<uint8_t>(response.begin() + 8, response.end())},
            encrypted));
        dpp::append_dpp_attr(response, dpp::k_dpp_attr_wrapped_data, encrypted.data(),
                             encrypted.size());
        ASSERT_TRUE(session.handle_authentication_response(response, confirm, error)) << error;
        ASSERT_TRUE(session.authentication_success());
    }

    beerocks::config_file::sConfigMaster logging_config{};
    son::db::sDbMasterConfig config{};
    std::unique_ptr<beerocks::logging> logger;
    std::unique_ptr<son::db> database;
    std::string root, path, error;
};

TEST_F(DppCsignTest, GeneratesLoadableKeyAndDoesNotRotateIt)
{
    generate();
    ASSERT_FALSE(HasFatalFailure());
    const auto original = read();
    EXPECT_FALSE(original.empty());
    dpp::DppKeyPtr key;
    ASSERT_TRUE(dpp::load_dpp_configurator_csign_key(*database, key, error)) << error;
    ASSERT_NE(nullptr, key.get());
    int nid          = 0;
    const EVP_MD *md = nullptr;
    size_t hash_len = 0, nonce_len = 0, coord_len = 0;
    ASSERT_TRUE(
        dpp::curve_info_from_key(key.get(), nid, md, hash_len, nonce_len, coord_len, error));
    EXPECT_EQ(NID_X9_62_prime256v1, nid);
    bool generated = true;
    error          = "stale";
    ASSERT_TRUE(dpp::generate_dpp_configurator_csign_key(*database, generated, error));
    EXPECT_FALSE(generated);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(original, read());
    EXPECT_NE(0, access((path + ".tmp").c_str(), F_OK));
}

TEST_F(DppCsignTest, FillsEmptyKeyAndPreservesOtherSections)
{
    const std::string before = "# fixture\n[bootstrap]\nuri=unchanged\n[dpp_controller_keys]\n"
                               "csign_key=  \npp_key=abcdef\ngroup_id=test-group\n";
    write(before);
    ASSERT_FALSE(HasFatalFailure());
    generate();
    ASSERT_FALSE(HasFatalFailure());
    const auto after = read();
    EXPECT_EQ(
        0U, after.find("# fixture\n[bootstrap]\nuri=unchanged\n[dpp_controller_keys]\ncsign_key="));
    EXPECT_NE(std::string::npos, after.find("\npp_key=abcdef\ngroup_id=test-group\n"));
    EXPECT_EQ(std::string::npos,
              after.find("[dpp_controller_keys]", after.find("[dpp_controller_keys]") + 1));
    dpp::DppKeyPtr key;
    EXPECT_TRUE(dpp::load_dpp_configurator_csign_key(*database, key, error)) << error;
}

TEST_F(DppCsignTest, AppendsControllerSectionWithoutDiscardingBootstrap)
{
    write("[bootstrap]\nuri=keep-me\n");
    ASSERT_FALSE(HasFatalFailure());
    generate();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(0U, read().find("[bootstrap]\nuri=keep-me\n\n[dpp_controller_keys]\ncsign_key="));
}

TEST_F(DppCsignTest, ExistingInvalidKeyIsNotSilentlyReplaced)
{
    const std::string original = "[dpp_controller_keys]\ncsign_key=not-hex\n";
    write(original);
    ASSERT_FALSE(HasFatalFailure());
    bool generated = true;
    ASSERT_TRUE(dpp::generate_dpp_configurator_csign_key(*database, generated, error));
    EXPECT_FALSE(generated);
    EXPECT_EQ(original, read());
    dpp::DppKeyPtr key;
    ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, key));
    EXPECT_FALSE(dpp::load_dpp_configurator_csign_key(*database, key, error));
    EXPECT_EQ(nullptr, key.get());
    EXPECT_EQ("Invalid C-sign private key (expected EC private key DER hex)", error);
}

TEST_F(DppCsignTest, MissingSectionAndEmptyKeyClearOutput)
{
    for (const auto &content : {"[other]\nx=y\n", "[dpp_controller_keys]\ncsign_key=\n"}) {
        write(content);
        ASSERT_FALSE(HasFatalFailure());
        dpp::DppKeyPtr key;
        ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, key));
        error = "stale";
        EXPECT_FALSE(dpp::load_dpp_configurator_csign_key(*database, key, error));
        EXPECT_EQ(nullptr, key.get());
        EXPECT_FALSE(error.empty());
        EXPECT_NE("stale", error);
    }
}

TEST_F(DppCsignTest, FailedReloadClearsPreviouslyCachedKey)
{
    generate();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(database->load_dpp_configurator_csign_key_from_store());
    EXPECT_TRUE(database->has_dpp_configurator_csign_key());
    write("[dpp_controller_keys]\ncsign_key=bad-hex\n");
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_FALSE(database->load_dpp_configurator_csign_key_from_store());
    EXPECT_FALSE(database->has_dpp_configurator_csign_key());
    std::string output = "stale";
    EXPECT_FALSE(database->get_dpp_configurator_csign_key_hex(output));
    EXPECT_TRUE(output.empty());
}

TEST_F(DppCsignTest, CachedKeySurvivesStoreRemoval)
{
    generate();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(database->load_dpp_configurator_csign_key_from_store());
    ASSERT_EQ(0, std::remove(path.c_str()));
    dpp::DppKeyPtr key;
    ASSERT_TRUE(dpp::load_dpp_configurator_csign_key(*database, key, error)) << error;
    EXPECT_NE(nullptr, key.get());
    EXPECT_TRUE(error.empty());
}

TEST_F(DppCsignTest, MissingParentReportsWriteFailureWithoutGenerating)
{
    ASSERT_EQ(0, rmdir((root + "/share").c_str()));
    bool generated = true;
    EXPECT_FALSE(dpp::generate_dpp_configurator_csign_key(*database, generated, error));
    EXPECT_FALSE(generated);
    EXPECT_NE(std::string::npos, error.find("failed writing"));
    EXPECT_NE(0, access(path.c_str(), F_OK));
}

TEST_F(DppCsignTest, BlockedTemporaryFilePreservesOriginalStore)
{
    const std::string original = "[bootstrap]\nuri=keep-me\n";
    write(original);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(0, mkdir((path + ".tmp").c_str(), 0700));
    bool generated = true;
    EXPECT_FALSE(dpp::generate_dpp_configurator_csign_key(*database, generated, error));
    EXPECT_FALSE(generated);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(original, read());
}

TEST_F(DppCsignTest, PrivateKeyEncodingRoundTripsAllSupportedCurves)
{
    for (int nid : {NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1}) {
        SCOPED_TRACE(nid);
        dpp::DppKeyPtr key, decoded;
        ASSERT_TRUE(dpp::generate_ec_key(nid, key));
        std::string encoded = "stale";
        ASSERT_TRUE(dpp::encode_ec_private_key_hex(key.get(), encoded));
        ASSERT_TRUE(dpp::parse_ec_private_key_hex(encoded, decoded));
        std::vector<uint8_t> original_xy, decoded_xy;
        const size_t width = nid == NID_X9_62_prime256v1 ? 32 : nid == NID_secp384r1 ? 48 : 66;
        ASSERT_TRUE(dpp::ec_public_xy(key.get(), width, original_xy));
        ASSERT_TRUE(dpp::ec_public_xy(decoded.get(), width, decoded_xy));
        EXPECT_EQ(original_xy, decoded_xy);
        std::vector<uint8_t> first, second;
        ASSERT_TRUE(dpp::ecdh_secret(key.get(), decoded.get(), width, first));
        ASSERT_TRUE(dpp::ecdh_secret(decoded.get(), key.get(), width, second));
        EXPECT_EQ(first, second); // Both decoded and original keys retain private material.
    }
}

TEST_F(DppCsignTest, PrivateKeyEncodingRejectsNullAndUntypedKeys)
{
    std::string encoded = "stale";
    EXPECT_FALSE(dpp::encode_ec_private_key_hex(nullptr, encoded));
    EXPECT_TRUE(encoded.empty());
    dpp::DppKeyPtr key(EVP_PKEY_new());
    ASSERT_NE(nullptr, key.get());
    encoded = "stale";
    EXPECT_FALSE(dpp::encode_ec_private_key_hex(key.get(), encoded));
    EXPECT_TRUE(encoded.empty());
}

TEST_F(DppCsignTest, PolicyAssemblyRejectsUnauthenticatedSession)
{
    generate();
    ASSERT_FALSE(HasFatalFailure());
    dpp::DppConfiguratorSession session;
    std::vector<std::string> objects{"stale"};
    bool send = true;
    EXPECT_FALSE(dpp::build_dpp_configuration_objects_from_policy(
        *database, session, tlvf::mac_from_string("02:00:00:00:00:02"),
        R"({"netRole":"mapAgent"})", objects, send, error));
    EXPECT_TRUE(objects.empty());
    EXPECT_FALSE(send);
    EXPECT_EQ("DPP connector payload requested before authentication success", error);
}

TEST_F(DppCsignTest, PolicyAssemblyBuildsRequestedRolesAndConnectionStatus)
{
    generate();
    ASSERT_FALSE(HasFatalFailure());
    const auto mac = tlvf::mac_from_string("02:00:00:00:00:02");
    ASSERT_NE(nullptr, database->m_agents.add(mac));
    son::wireless_utils::sBssInfoConf bss{};
    bss.ssid      = "PolicyNetwork";
    bss.fronthaul = bss.backhaul = true;
    bss.authentication_type      = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
    bss.network_key              = "policy-passphrase";
    database->add_bss_info_configuration(bss);
    dpp::DppConfiguratorSession session;
    authenticate(session);
    ASSERT_FALSE(HasFatalFailure());
    for (const auto &role : {"sta", "mapAgent", "mapBackhaulSta"}) {
        SCOPED_TRACE(role);
        std::vector<std::string> objects{"stale"};
        bool send = false;
        ASSERT_TRUE(dpp::build_dpp_configuration_objects_from_policy(
            *database, session, mac, std::string("{\"netRole\":\"") + role + "\"}", objects, send,
            error))
            << error;
        ASSERT_EQ(1U, objects.size());
        EXPECT_EQ(std::string(role) == "mapBackhaulSta", send);
        EXPECT_TRUE(error.empty());
        std::unique_ptr<json_object, decltype(&json_object_put)> object(
            json_tokener_parse(objects[0].c_str()), json_object_put);
        ASSERT_NE(nullptr, object.get());
        json_object *value = nullptr;
        ASSERT_TRUE(json_object_object_get_ex(object.get(), "netRole", &value));
        EXPECT_STREQ(role, json_object_get_string(value));
        const bool map_agent = std::string(role) == "mapAgent";
        EXPECT_EQ(!map_agent, json_object_object_get_ex(object.get(), "discovery", &value));
        json_object *credential = nullptr;
        ASSERT_TRUE(json_object_object_get_ex(object.get(), "cred", &credential));
        ASSERT_TRUE(json_object_object_get_ex(credential, "signedConnector", &value));
        const std::string connector = json_object_get_string(value);
        EXPECT_TRUE(dpp::connector_has_netrole(connector, role));
        ASSERT_TRUE(json_object_object_get_ex(object.get(), "wi-fi_tech", &value));
        EXPECT_STREQ(map_agent ? "map" : "infra", json_object_get_string(value));
        ASSERT_TRUE(json_object_object_get_ex(credential, "akm", &value));
        EXPECT_STREQ(map_agent ? "dpp" : "psk", json_object_get_string(value));
        EXPECT_EQ(!map_agent, json_object_object_get_ex(credential, "pass", &value));
        if (!map_agent) {
            EXPECT_STREQ("policy-passphrase", json_object_get_string(value));
        }
    }

    std::vector<std::string> objects;
    bool send = false;
    ASSERT_TRUE(dpp::build_dpp_configuration_objects_from_policy(
        *database, session, mac,
        R"({"netRole":"mapAgent","bSTAList":[{"netRole":"mapBackhaulSta","akm":"psk"}]})", objects,
        send, error))
        << error;
    ASSERT_EQ(2U, objects.size());
    EXPECT_TRUE(send);
    for (size_t i = 0; i < objects.size(); ++i) {
        std::unique_ptr<json_object, decltype(&json_object_put)> object(
            json_tokener_parse(objects[i].c_str()), json_object_put);
        ASSERT_NE(nullptr, object.get());
        json_object *value = nullptr;
        ASSERT_TRUE(json_object_object_get_ex(object.get(), "netRole", &value));
        EXPECT_STREQ(i == 0 ? "mapAgent" : "mapBackhaulSta", json_object_get_string(value));
        ASSERT_TRUE(json_object_object_get_ex(object.get(), "wi-fi_tech", &value));
        EXPECT_STREQ("map", json_object_get_string(value));
    }
}
} // namespace
