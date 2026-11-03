/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include "../../dpp/dpp_internal.h"

#include <bcl/beerocks_string_utils.h>
#include <gtest/gtest.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <ctime>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
namespace dpp = son::controller_dpp;
using JsonPtr = std::unique_ptr<json_object, decltype(&json_object_put)>;

// PUBLIC_INTERFACE
/** Return a JSON field while recording a useful failure instead of comparing serialization. */
json_object *field(json_object *parent, const char *name)
{
    json_object *value = nullptr;
    EXPECT_TRUE(parent && json_object_object_get_ex(parent, name, &value)) << name;
    return value;
}

// PUBLIC_INTERFACE
/** Build fresh signing and net-access keys and a payload with public material only. */
class DppConfigurationTest : public ::testing::Test {
protected:
    /** Create valid EC JWK input without depending on a live keystore. */
    void initialize(int nid = NID_X9_62_prime256v1, const std::string &role = "mapAgent")
    {
        ASSERT_TRUE(dpp::generate_ec_key(nid, csign));
        ASSERT_TRUE(dpp::generate_ec_key(nid, netaccess));
        json_object *jwk = nullptr;
        ASSERT_TRUE(dpp::ec_key_to_jwk(netaccess.get(), &jwk, error)) << error;
        JsonPtr root(json_object_new_object(), json_object_put);
        json_object *group  = json_object_new_object();
        json_object *groups = json_object_new_array();
        json_object_object_add(group, "groupId", json_object_new_string("test-group"));
        json_object_object_add(group, "netRole", json_object_new_string(role.c_str()));
        json_object_array_add(groups, group);
        json_object_object_add(root.get(), "groups", groups);
        json_object_object_add(root.get(), "netAccessKey", jwk);
        payload = json_object_to_json_string_ext(root.get(), JSON_C_TO_STRING_PLAIN);
    }

    dpp::DppKeyPtr csign, netaccess;
    std::string payload, error;
};

// PUBLIC_INTERFACE
/** The JWS has curve-specific raw signatures and validates only under its signing key. */
TEST_F(DppConfigurationTest, SignedConnectorSupportedCurvesAndWrongKey)
{
    for (int nid : {NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1}) {
        SCOPED_TRACE(nid);
        initialize(nid);
        ASSERT_FALSE(HasFatalFailure());
        std::string connector, pub, kid, group, role;
        ASSERT_TRUE(dpp::build_signed_connector_from_payload(csign.get(), payload, connector, pub,
                                                             kid, error))
            << error;
        EXPECT_FALSE(pub.empty());
        EXPECT_FALSE(kid.empty());
        std::time_t expiry = 99;
        ASSERT_TRUE(
            dpp::validate_signed_connector(connector, csign.get(), group, role, expiry, error))
            << error;
        EXPECT_EQ("test-group", group);
        EXPECT_EQ("mapAgent", role);
        EXPECT_EQ(0U, expiry);
        EXPECT_TRUE(dpp::connector_has_netrole(connector, "MAPAGENT"));
        EXPECT_FALSE(dpp::connector_has_netrole(connector, "sta"));

        std::string header, encoded_payload, signature;
        ASSERT_TRUE(dpp::split_jws(connector, header, encoded_payload, signature, error));
        std::string raw_signature;
        ASSERT_TRUE(beerocks::string_utils::base64_url_decode(signature, raw_signature));
        EXPECT_EQ(nid == NID_X9_62_prime256v1 ? 64U : nid == NID_secp384r1 ? 96U : 132U,
                  raw_signature.size());
        dpp::DppKeyPtr wrong;
        ASSERT_TRUE(dpp::generate_ec_key(nid, wrong));
        EXPECT_FALSE(
            dpp::validate_signed_connector(connector, wrong.get(), group, role, expiry, error));
        EXPECT_EQ("Connector kid does not match C-sign key", error);
    }
}

// PUBLIC_INTERFACE
/** Changing signed payload or signature bytes invalidates the connector. */
TEST_F(DppConfigurationTest, RejectsTamperedConnector)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    std::string connector, pub, kid, group, role;
    ASSERT_TRUE(
        dpp::build_signed_connector_from_payload(csign.get(), payload, connector, pub, kid, error));
    std::string header, encoded_payload, signature;
    ASSERT_TRUE(dpp::split_jws(connector, header, encoded_payload, signature, error));
    auto modified_payload = payload;
    const auto pos        = modified_payload.find("test-group");
    ASSERT_NE(std::string::npos, pos);
    modified_payload[pos]       = 'T';
    const auto encoded_modified = dpp::base64url_encode(
        reinterpret_cast<const uint8_t *>(modified_payload.data()), modified_payload.size());
    std::time_t expiry = 0;
    EXPECT_FALSE(dpp::validate_signed_connector(header + "." + encoded_modified + "." + signature,
                                                csign.get(), group, role, expiry, error));
    std::string raw_signature;
    ASSERT_TRUE(beerocks::string_utils::base64_url_decode(signature, raw_signature));
    ASSERT_FALSE(raw_signature.empty());
    raw_signature[0] ^= 1;
    const auto encoded_signature = dpp::base64url_encode(
        reinterpret_cast<const uint8_t *>(raw_signature.data()), raw_signature.size());
    EXPECT_FALSE(
        dpp::validate_signed_connector(header + "." + encoded_payload + "." + encoded_signature,
                                       csign.get(), group, role, expiry, error));
}

// PUBLIC_INTERFACE
/** A signed connector with a fixed historical expiration must be rejected. */
TEST_F(DppConfigurationTest, RejectsExpiredConnector)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    JsonPtr root(json_tokener_parse(payload.c_str()), json_object_put);
    ASSERT_NE(nullptr, root.get());
    json_object_object_add(root.get(), "expiry", json_object_new_string("1970-01-01T00:00:01Z"));
    payload = json_object_to_json_string_ext(root.get(), JSON_C_TO_STRING_PLAIN);
    std::string connector, pub, kid, group, role;
    ASSERT_TRUE(
        dpp::build_signed_connector_from_payload(csign.get(), payload, connector, pub, kid, error));
    std::time_t expiry = 0;
    EXPECT_FALSE(
        dpp::validate_signed_connector(connector, csign.get(), group, role, expiry, error));
    EXPECT_EQ("Connector has expired", error);
}

// PUBLIC_INTERFACE
/** MAP objects can omit discovery and carry policy counters without leaking private JWK fields. */
TEST_F(DppConfigurationTest, MapAgentObjectOptionsAndPublicKeyOnly)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    dpp::DppConfigurationObjectOptions options;
    options.include_discovery            = false;
    options.include_net_role             = true;
    options.include_df_counter_threshold = true;
    options.df_counter_threshold         = 3;
    std::string config;
    ASSERT_TRUE(dpp::build_dpp_configuration_object(csign.get(), "", payload, "", "mapAgent",
                                                    "test-group", options, config, error))
        << error;
    JsonPtr root(json_tokener_parse(config.c_str()), json_object_put);
    ASSERT_NE(nullptr, root.get());
    EXPECT_STREQ("map", json_object_get_string(field(root.get(), "wi-fi_tech")));
    EXPECT_STREQ("mapAgent", json_object_get_string(field(root.get(), "netRole")));
    EXPECT_EQ(3, json_object_get_int(field(root.get(), "dfCounterThreshold")));
    json_object *unused = nullptr;
    EXPECT_FALSE(json_object_object_get_ex(root.get(), "discovery", &unused));
    auto *cred = field(root.get(), "cred");
    ASSERT_NE(nullptr, cred);
    EXPECT_STREQ("dpp", json_object_get_string(field(cred, "akm")));
    auto *jwk = field(cred, "csign");
    ASSERT_NE(nullptr, jwk);
    EXPECT_STREQ("EC", json_object_get_string(field(jwk, "kty")));
    EXPECT_FALSE(json_object_object_get_ex(jwk, "d", &unused));
    std::string group, role;
    std::time_t expiry = 0;
    ASSERT_TRUE(
        dpp::validate_signed_connector(json_object_get_string(field(cred, "signedConnector")),
                                       csign.get(), group, role, expiry, error))
        << error;
    EXPECT_EQ("test-group", group);
    EXPECT_EQ("mapAgent", role);
}

// PUBLIC_INTERFACE
/** Infrastructure SSIDs use text when printable and base64url for binary data. */
TEST_F(DppConfigurationTest, InfrastructureDiscoveryAndCredentialOptions)
{
    initialize(NID_X9_62_prime256v1, "sta");
    ASSERT_FALSE(HasFatalFailure());
    for (const std::string &ssid_hex : {std::string("54657374"), std::string("00ff")}) {
        dpp::DppConfigurationObjectOptions options;
        options.akm                      = "psk+sae";
        options.passphrase               = "test-passphrase";
        options.psk_hex                  = std::string(64, 'a');
        options.security_ies_hex         = "3000";
        options.ssid_advertisement_valid = true;
        options.ssid_advertisement       = false;
        std::string config;
        ASSERT_TRUE(dpp::build_dpp_configuration_object(csign.get(), "", payload, ssid_hex, "STA",
                                                        "test-group", options, config, error));
        JsonPtr root(json_tokener_parse(config.c_str()), json_object_put);
        ASSERT_NE(nullptr, root.get());
        EXPECT_STREQ("infra", json_object_get_string(field(root.get(), "wi-fi_tech")));
        auto *discovery = field(root.get(), "discovery");
        ASSERT_NE(nullptr, discovery);
        if (ssid_hex == "54657374") {
            EXPECT_STREQ("Test", json_object_get_string(field(discovery, "ssid")));
        } else {
            EXPECT_STREQ("AP8", json_object_get_string(field(discovery, "ssid64")));
        }
        auto *cred = field(root.get(), "cred");
        ASSERT_NE(nullptr, cred);
        EXPECT_EQ(options.psk_hex, json_object_get_string(field(cred, "psk_hex")));
        json_object *unused = nullptr;
        EXPECT_FALSE(json_object_object_get_ex(cred, "pass", &unused)); // PSK takes precedence.
        EXPECT_STREQ("3000", json_object_get_string(field(cred, "SecurityIEs")));
        EXPECT_FALSE(json_object_get_boolean(field(cred, "SSIDAdvertisement")));
    }
}

// PUBLIC_INTERFACE
/** Role/group/SSID validation fails with useful errors and clears stale output. */
TEST_F(DppConfigurationTest, ConfigurationObjectValidation)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    std::string config = "stale";
    EXPECT_FALSE(
        dpp::build_dpp_configuration_object(nullptr, "", payload, "41", "", "", config, error));
    EXPECT_EQ("Missing C-sign key", error);
    EXPECT_TRUE(config.empty());
    EXPECT_FALSE(dpp::build_dpp_configuration_object(csign.get(), "", payload, "41", "sta", "",
                                                     config, error));
    EXPECT_EQ("Connector netRole mismatch", error);
    EXPECT_FALSE(dpp::build_dpp_configuration_object(csign.get(), "", payload, "41", "", "wrong",
                                                     config, error));
    EXPECT_EQ("Connector groupId mismatch", error);
    EXPECT_FALSE(
        dpp::build_dpp_configuration_object(csign.get(), "", payload, "gg", "", "", config, error));
    EXPECT_EQ("Invalid SSID hex", error);
    EXPECT_TRUE(config.empty());
    EXPECT_FALSE(dpp::build_dpp_configuration_object(csign.get(), "invalid", payload, "41", "", "",
                                                     config, error));
    EXPECT_EQ("Invalid ppKey", error);
}

// PUBLIC_INTERFACE
/** A plain mapAgent object has no implicit backhaul AKM, links or radios. */
TEST(DppConfigRequestObjects, ParsesMapAgentWithoutBackhaulMetadata)
{
    std::vector<std::string> roles{"stale"};
    std::string akm = "stale";
    bool max_links  = true;
    std::unordered_set<std::string> ruids{"stale"};
    ASSERT_TRUE(dpp::parse_dpp_config_request_objects(
        R"({"name":"ut-agent","wi-fi_tech":"map","netRole":"mapAgent"})", roles, &akm, &max_links,
        &ruids));
    EXPECT_EQ((std::vector<std::string>{"mapAgent"}), roles);
    EXPECT_TRUE(akm.empty());
    EXPECT_FALSE(max_links);
    EXPECT_TRUE(ruids.empty());
}

// PUBLIC_INTERFACE
/** Only positive integer bSTA_Maximum_Links values set the optional presence flag. */
TEST(DppConfigRequestObjects, ParsesBackhaulLinkCountBoundaries)
{
    const std::vector<std::string> counts{"-1", "0", "1", "2", R"("2")", "true"};
    for (size_t index = 0; index < counts.size(); ++index) {
        SCOPED_TRACE(counts[index]);
        const std::string input =
            R"({"netRole":"mapAgent","bSTAList":[{"netRole":"mapBackhaulSta",)"
            R"("akm":"dpp+sae","bSTA_Maximum_Links":)" +
            counts[index] + R"(,"RadioList":[{"RUID":"000000000001"},{"RUID":"000000000002"}]}]})";
        std::vector<std::string> roles;
        std::string akm;
        bool max_links = true;
        std::unordered_set<std::string> ruids;
        ASSERT_TRUE(dpp::parse_dpp_config_request_objects(input, roles, &akm, &max_links, &ruids));
        EXPECT_EQ((std::vector<std::string>{"mapAgent", "mapBackhaulSta"}), roles);
        EXPECT_EQ("dpp+sae", akm);
        EXPECT_EQ(index == 2 || index == 3, max_links);
        EXPECT_EQ((std::unordered_set<std::string>{"000000000001", "000000000002"}), ruids);
    }
}

// PUBLIC_INTERFACE
/** A top-level backhaul STA object exposes its maximum-links flag and both radio identifiers. */
TEST(DppConfigRequestObjects, ParsesTopLevelBackhaulMaximumLinks)
{
    const std::string input = R"({
        "netRole": "mapBackhaulSta",
        "akm": "dpp+sae",
        "bSTA_Maximum_Links": 2,
        "RadioList": [{"RUID": "aabbccddeeff"}, {"RUID": "112233445566"}]
    })";
    std::vector<std::string> roles;
    std::string akm;
    bool max_links = false;
    std::unordered_set<std::string> ruids;
    ASSERT_TRUE(dpp::parse_dpp_config_request_objects(input, roles, &akm, &max_links, &ruids));
    EXPECT_EQ((std::vector<std::string>{"mapBackhaulSta"}), roles);
    EXPECT_EQ("dpp+sae", akm);
    EXPECT_TRUE(max_links);
    EXPECT_EQ((std::unordered_set<std::string>{"aabbccddeeff", "112233445566"}), ruids);
}

// PUBLIC_INTERFACE
/** Direct backhaul metadata respects field types, positive links and optional outputs. */
TEST(DppConfigRequestObjects, TopLevelBackhaulMetadataBoundaries)
{
    const std::vector<std::string> counts{"-1", "0", "1", R"("2")", "true"};
    for (size_t index = 0; index < counts.size(); ++index) {
        SCOPED_TRACE(counts[index]);
        const std::string input =
            R"([{"netRole":"mapBackhaulSta","akm":3,"bSTA_Maximum_Links":)" + counts[index] +
            R"(,"RadioList":[{"RUID":"radio1"},{"RUID":"radio1"},{"RUID":3},null]}])";
        std::vector<std::string> roles;
        std::string akm = "stale";
        bool max_links  = true;
        std::unordered_set<std::string> ruids{"stale"};
        ASSERT_TRUE(dpp::parse_dpp_config_request_objects(input, roles, &akm, &max_links, &ruids));
        EXPECT_EQ((std::vector<std::string>{"mapBackhaulSta"}), roles);
        EXPECT_TRUE(akm.empty());
        EXPECT_EQ(index == 2, max_links);
        EXPECT_EQ((std::unordered_set<std::string>{"radio1"}), ruids);
        EXPECT_TRUE(dpp::parse_dpp_config_request_objects(input, roles, nullptr, nullptr, nullptr));
    }
}

// PUBLIC_INTERFACE
/** Metadata on a direct non-backhaul role must not be collected as backhaul policy. */
TEST(DppConfigRequestObjects, IgnoresDirectNonBackhaulMetadata)
{
    std::vector<std::string> roles;
    std::string akm;
    bool max_links = false;
    std::unordered_set<std::string> ruids;
    ASSERT_TRUE(dpp::parse_dpp_config_request_objects(
        R"({"netRole":"mapAgent","akm":"sae","bSTA_Maximum_Links":2,
            "RadioList":[{"RUID":"radio1"}]})",
        roles, &akm, &max_links, &ruids));
    EXPECT_EQ((std::vector<std::string>{"mapAgent"}), roles);
    EXPECT_TRUE(akm.empty());
    EXPECT_FALSE(max_links);
    EXPECT_TRUE(ruids.empty());
}

// PUBLIC_INTERFACE
/** Nested bSTAList accepts array/object form and deduplicates roles and RUIDs. */
TEST(DppConfigRequestObjects, NestedObjectsAndArrays)
{
    const std::string input = R"([
      {"netRole":"mapAgent","bSTAList":[
        {"netRole":"mapBackhaulSta","akm":"sae","bSTA_Maximum_Links":2,
         "RadioList":[{"RUID":"radio1"},{"RUID":"radio1"},{"RUID":3},null]},
        {"netRole":"mapBackhaulSta","akm":"psk","bSTA_Maximum_Links":0}]},
      {"netRole":"mapAgent","bSTAList":{"netRole":"sta","RadioList":[{"RUID":"radio2"}]}}
    ])";
    std::vector<std::string> roles;
    std::string akm;
    bool max_links = false;
    std::unordered_set<std::string> ruids;
    ASSERT_TRUE(dpp::parse_dpp_config_request_objects(input, roles, &akm, &max_links, &ruids));
    EXPECT_EQ((std::vector<std::string>{"mapAgent", "mapBackhaulSta", "sta"}), roles);
    EXPECT_EQ("sae", akm);
    EXPECT_TRUE(max_links);
    EXPECT_EQ((std::unordered_set<std::string>{"radio1", "radio2"}), ruids);
    EXPECT_TRUE(dpp::parse_dpp_config_request_objects(input, roles, nullptr, nullptr, nullptr));
}

// PUBLIC_INTERFACE
/** Empty and invalid input reset optional result containers rather than retaining stale data. */
TEST(DppConfigRequestObjects, ClearsOutputsOnEmptyOrInvalidJson)
{
    for (const auto &input : {std::string(""), std::string("{")}) {
        std::vector<std::string> roles{"stale"};
        std::string akm = "stale";
        bool max_links  = true;
        std::unordered_set<std::string> ruids{"stale"};
        EXPECT_EQ(input.empty(),
                  dpp::parse_dpp_config_request_objects(input, roles, &akm, &max_links, &ruids));
        EXPECT_TRUE(roles.empty());
        EXPECT_TRUE(akm.empty());
        EXPECT_FALSE(max_links);
        EXPECT_TRUE(ruids.empty());
    }
}

// PUBLIC_INTERFACE
/** Supported roles normalize case but unrelated roles are rejected. */
TEST(DppConnectorRoles, Normalization)
{
    const std::vector<std::pair<std::string, std::string>> cases{
        {"STA", "sta"},
        {"AP", "ap"},
        {"MAPBACKHAULSTA", "mapBackhaulSta"},
        {"MAPBACKHAULBSS", "mapBackhaulBss"},
        {"MAPAGENT", "mapAgent"},
        {"MAPCONTROLLER", "mapController"},
        {"CONFIGURATOR", "configurator"}};
    for (const auto &entry : cases) {
        std::string normalized;
        ASSERT_TRUE(dpp::normalize_dpp_netrole(entry.first, normalized));
        EXPECT_EQ(entry.second, normalized);
    }
    std::string normalized;
    EXPECT_FALSE(dpp::normalize_dpp_netrole("unknown", normalized));
}
// PUBLIC_INTERFACE
/** Serialize a generated test key as EC private DER without using a filesystem keystore. */
std::string private_hex(EVP_PKEY *key)
{
    const int length = i2d_PrivateKey(key, nullptr);
    EXPECT_GT(length, 0);
    if (length <= 0) {
        return {};
    }
    std::vector<uint8_t> der(static_cast<size_t>(length));
    auto *ptr = der.data();
    EXPECT_EQ(length, i2d_PrivateKey(key, &ptr));
    return dpp::bytes_to_hex_string(der.data(), der.size());
}

// PUBLIC_INTERFACE
/** Encode a text component for compact-JWS parser tests. */
std::string encode_component(const std::string &text)
{
    return dpp::base64url_encode(reinterpret_cast<const uint8_t *>(text.data()), text.size());
}

// PUBLIC_INTERFACE
/** The private-key builder derives both public keys, emits expiry, and supports cached public DER. */
TEST_F(DppConfigurationTest, PrivateKeyConnectorBuilderAndCachedPublicKeys)
{
    for (int nid : {NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1}) {
        SCOPED_TRACE(nid);
        initialize(nid);
        ASSERT_FALSE(HasFatalFailure());
        const auto secret = private_hex(netaccess.get());
        ASSERT_FALSE(secret.empty());
        std::string connector, csign_pub, netaccess_pub, group, role;
        for (uint32_t expiration : {uint32_t(0), uint32_t(4102444800U)}) {
            ASSERT_TRUE(dpp::build_signed_connector(csign.get(), secret, "STA", "", expiration,
                                                    connector, csign_pub, netaccess_pub, error))
                << error;
            ASSERT_FALSE(csign_pub.empty());
            ASSERT_FALSE(netaccess_pub.empty());
            std::time_t actual_expiry = 99;
            ASSERT_TRUE(dpp::validate_signed_connector(connector, csign.get(), group, role,
                                                       actual_expiry, error))
                << error;
            EXPECT_EQ("*", group);
            EXPECT_EQ("sta", role);
            EXPECT_EQ(expiration, actual_expiry);
            std::string h, p, s, decoded;
            ASSERT_TRUE(dpp::split_jws(connector, h, p, s, error));
            ASSERT_TRUE(beerocks::string_utils::base64_url_decode(p, decoded));
            dpp::DppConnectorPayload parsed;
            ASSERT_TRUE(dpp::parse_dpp_connector_payload(decoded, parsed, error));
            EXPECT_EQ(netaccess_pub, parsed.netaccess_jwk_der_hex);
            JsonPtr root(json_tokener_parse(decoded.c_str()), json_object_put);
            ASSERT_NE(nullptr, root.get());
            EXPECT_EQ(3, json_object_get_int(field(root.get(), "version")));
        }
    }
}

// PUBLIC_INTERFACE
/** Builder guards reject missing keys, malformed private DER and invalid cached public DER. */
TEST_F(DppConfigurationTest, PrivateKeyConnectorBuilderFailures)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    const auto secret     = private_hex(netaccess.get());
    std::string connector = "stale", pub, netpub;
    EXPECT_FALSE(
        dpp::build_signed_connector(nullptr, secret, "sta", "", 0, connector, pub, netpub, error));
    EXPECT_EQ("Missing C-sign key", error);
    EXPECT_TRUE(connector.empty());
    EXPECT_FALSE(
        dpp::build_signed_connector(csign.get(), "", "sta", "", 0, connector, pub, netpub, error));
    EXPECT_EQ("Missing netAccessKey", error);
    EXPECT_FALSE(dpp::build_signed_connector(csign.get(), "00", "sta", "", 0, connector, pub,
                                             netpub, error));
    EXPECT_EQ("Failed deriving netAccessKey public key", error);
    pub = "00";
    EXPECT_FALSE(dpp::build_signed_connector(csign.get(), secret, "sta", "", 0, connector, pub,
                                             netpub, error));
    EXPECT_EQ("Failed computing connector kid", error);
    pub.clear();
    netpub = "00";
    EXPECT_FALSE(dpp::build_signed_connector(csign.get(), secret, "sta", "", 0, connector, pub,
                                             netpub, error));
    EXPECT_EQ("Invalid netAccessKey", error);
    EXPECT_TRUE(connector.empty());
}

// PUBLIC_INTERFACE
/** Public/private parsers round-trip DER and KID is SHA-256 of the uncompressed EC point. */
TEST_F(DppConfigurationTest, PrivateDerPublicDerAndIndependentKid)
{
    for (int nid : {NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1}) {
        initialize(nid);
        ASSERT_FALSE(HasFatalFailure());
        const auto secret = private_hex(csign.get());
        dpp::DppKeyPtr parsed;
        ASSERT_TRUE(dpp::parse_ec_private_key_hex(secret, parsed));
        std::string pub;
        ASSERT_TRUE(dpp::derive_pubkey_hex_from_private(secret, pub));
        const int length = i2d_PUBKEY(csign.get(), nullptr);
        ASSERT_GT(length, 0);
        std::vector<uint8_t> der(static_cast<size_t>(length));
        auto *ptr = der.data();
        ASSERT_EQ(length, i2d_PUBKEY(csign.get(), &ptr));
        EXPECT_EQ(dpp::bytes_to_hex_string(der.data(), der.size()), pub);
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> public_key(nullptr, EVP_PKEY_free);
        ASSERT_TRUE(dpp::parse_ec_pubkey_hex(pub, public_key));
        std::vector<uint8_t> xy, parsed_xy;
        const size_t coord = nid == NID_X9_62_prime256v1 ? 32 : nid == NID_secp384r1 ? 48 : 66;
        ASSERT_TRUE(dpp::ec_public_xy(public_key.get(), coord, xy));
        ASSERT_TRUE(dpp::ec_public_xy(parsed.get(), coord, parsed_xy));
        EXPECT_EQ(xy, parsed_xy);
        xy.insert(xy.begin(), 0x04);
        uint8_t digest[SHA256_DIGEST_LENGTH];
        ASSERT_NE(nullptr, SHA256(xy.data(), xy.size(), digest));
        std::string kid;
        ASSERT_TRUE(dpp::compute_csign_kid(pub, kid));
        EXPECT_EQ(dpp::base64url_encode(digest, sizeof(digest)), kid);
    }
    for (const auto &bad :
         {std::string(""), std::string("0"), std::string("gg"), std::string("00")}) {
        SCOPED_TRACE(bad);
        dpp::DppKeyPtr parsed;
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> public_key(nullptr, EVP_PKEY_free);
        std::string out = "stale";
        EXPECT_FALSE(dpp::parse_ec_private_key_hex(bad, parsed));
        EXPECT_FALSE(dpp::parse_ec_pubkey_hex(bad, public_key));
        EXPECT_FALSE(dpp::derive_pubkey_hex_from_private(bad, out));
        EXPECT_TRUE(out.empty());
        out = "stale";
        EXPECT_FALSE(dpp::compute_csign_kid(bad, out));
        EXPECT_TRUE(out.empty());
    }
}

// PUBLIC_INTERFACE
/** JWS headers require a string algorithm and ignore non-string optional fields. */
TEST(DppJwsHeader, RequiredAndOptionalFieldTypes)
{
    const std::vector<std::pair<std::string, std::string>> invalid{
        {"{", "Invalid JWS header JSON"},
        {"{}", "Missing JWS alg"},
        {R"({"alg":1})", "Missing JWS alg"}};
    for (const auto &entry : invalid) {
        std::string alg = "stale", typ = "stale", kid = "stale", error;
        EXPECT_FALSE(dpp::parse_jws_header(entry.first, alg, typ, kid, error));
        EXPECT_EQ(entry.second, error);
        EXPECT_TRUE(alg.empty());
        EXPECT_TRUE(typ.empty());
        EXPECT_TRUE(kid.empty());
    }
    for (const auto &input : {std::string(R"({"alg":"ES256"})"),
                              std::string(R"({"alg":"ES256","typ":1,"kid":false})")}) {
        std::string alg, typ = "stale", kid = "stale", error;
        ASSERT_TRUE(dpp::parse_jws_header(input, alg, typ, kid, error));
        EXPECT_EQ("ES256", alg);
        EXPECT_TRUE(typ.empty());
        EXPECT_TRUE(kid.empty());
    }
    std::string alg, typ, kid, error;
    ASSERT_TRUE(dpp::parse_jws_header(R"({"alg":"ES384","typ":"dppCon","kid":"id"})", alg, typ, kid,
                                      error));
    EXPECT_EQ("ES384", alg);
    EXPECT_EQ("dppCon", typ);
    EXPECT_EQ("id", kid);
}

// PUBLIC_INTERFACE
/** Mutating every JWK field independently reaches schema, curve, encoding and point guards. */
TEST_F(DppConfigurationTest, RejectsMalformedJwkFields)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    std::string out = "stale";
    EXPECT_FALSE(dpp::jwk_pubkey_to_der_hex(nullptr, out));
    EXPECT_TRUE(out.empty());
    JsonPtr nonobject(json_object_new_array(), json_object_put);
    EXPECT_FALSE(dpp::jwk_pubkey_to_der_hex(nonobject.get(), out));
    for (const char *name : {"kty", "crv", "x", "y"}) {
        for (bool missing : {false, true}) {
            json_object *raw = nullptr;
            ASSERT_TRUE(dpp::ec_key_to_jwk(netaccess.get(), &raw, error));
            JsonPtr jwk(raw, json_object_put);
            if (missing) {
                json_object_object_del(jwk.get(), name);
            } else {
                json_object_object_add(jwk.get(), name, json_object_new_int(1));
            }
            SCOPED_TRACE(name);
            out = "stale";
            EXPECT_FALSE(dpp::jwk_pubkey_to_der_hex(jwk.get(), out));
            EXPECT_TRUE(out.empty());
        }
    }
    const std::vector<std::pair<std::string, std::string>> mutations{
        {"kty", "RSA"}, {"crv", "P-224"}, {"x", "!"},  {"y", "!"},
        {"x", ""},      {"y", ""},        {"x", "AA"}, {"y", "AA"}};
    for (const auto &entry : mutations) {
        SCOPED_TRACE(entry.first + "=" + entry.second);
        json_object *raw = nullptr;
        ASSERT_TRUE(dpp::ec_key_to_jwk(netaccess.get(), &raw, error));
        JsonPtr jwk(raw, json_object_put);
        json_object_object_add(jwk.get(), entry.first.c_str(),
                               json_object_new_string(entry.second.c_str()));
        out = "stale";
        EXPECT_FALSE(dpp::jwk_pubkey_to_der_hex(jwk.get(), out));
        EXPECT_TRUE(out.empty());
    }
}

// PUBLIC_INTERFACE
/** Connector payload parsing skips malformed groups and reports precise key/expiry errors. */
TEST_F(DppConfigurationTest, ConnectorPayloadSchemaAndGroupSelection)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    const std::vector<std::pair<std::string, std::string>> invalid{
        {"{", "Invalid connector payload JSON"},
        {"{}", "Missing connector groups"},
        {R"({"groups":[]})", "Missing connector groupId/netRole"},
        {R"({"groups":[null,4,{},{"groupId":1,"netRole":"sta"},
             {"groupId":"g","netRole":1},{"groupId":"","netRole":"sta"},
             {"groupId":"g","netRole":""}]})",
         "Missing connector groupId/netRole"},
        {R"({"groups":{"groupId":"g","netRole":"sta"}})", "Missing connector netAccessKey"},
        {R"({"groups":{"groupId":"g","netRole":"sta"},"netAccessKey":1})",
         "Missing connector netAccessKey"},
        {R"({"groups":{"groupId":"g","netRole":"sta"},"netAccessKey":{}})",
         "Invalid connector netAccessKey"}};
    for (const auto &entry : invalid) {
        SCOPED_TRACE(entry.first);
        dpp::DppConnectorPayload parsed;
        parsed.netaccess_jwk_der_hex = "stale";
        EXPECT_FALSE(dpp::parse_dpp_connector_payload(entry.first, parsed, error));
        EXPECT_EQ(entry.second, error);
        EXPECT_TRUE(parsed.netaccess_jwk_der_hex.empty());
    }
    for (const auto &expiry : {std::string(R"("bad")"), std::string(R"("")"), std::string("1"),
                               std::string(R"("2020-01-01T00:00:00Z")")}) {
        JsonPtr root(json_tokener_parse(payload.c_str()), json_object_put);
        json_object_object_add(
            root.get(), "groups",
            json_tokener_parse(R"([null,{},{"groupId":"selected","netRole":"sta"},
                                   {"groupId":"ignored","netRole":"ap"}])"));
        json_object_object_add(root.get(), "expiry", json_tokener_parse(expiry.c_str()));
        dpp::DppConnectorPayload parsed;
        const bool accepted = dpp::parse_dpp_connector_payload(
            json_object_to_json_string_ext(root.get(), JSON_C_TO_STRING_PLAIN), parsed, error);
        EXPECT_EQ(expiry != R"("bad")", accepted);
        if (accepted) {
            EXPECT_EQ("selected", parsed.group_id);
            EXPECT_EQ("sta", parsed.net_role);
            EXPECT_EQ(expiry == R"("2020-01-01T00:00:00Z")" ? 1577836800U : 0U, parsed.expiry);
        } else {
            EXPECT_EQ("Invalid connector expiry format", error);
        }
    }
}

// PUBLIC_INTERFACE
/** Validation distinguishes header, encoding, algorithm and signature-length failures. */
TEST_F(DppConfigurationTest, ConnectorValidationFailureStages)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    std::string connector, pub, kid, group, role;
    ASSERT_TRUE(
        dpp::build_signed_connector_from_payload(csign.get(), payload, connector, pub, kid, error));
    std::string h, p, s;
    ASSERT_TRUE(dpp::split_jws(connector, h, p, s, error));
    const auto header = [&](const std::string &alg, const std::string &typ, const std::string &id) {
        return encode_component(R"({"alg":")" + alg + R"(","typ":")" + typ + R"(","kid":")" + id +
                                R"("})");
    };
    const std::vector<std::pair<std::string, std::string>> invalid{
        {"", "Missing connector"},
        {"no-separator", "Missing JWS header separator"},
        {"!." + p + "." + s, "Invalid connector header encoding"},
        {encode_component("{") + "." + p + "." + s, "Invalid JWS header JSON"},
        {encode_component("{}") + "." + p + "." + s, "Missing JWS alg"},
        {header("ES256", "other", kid) + "." + p + "." + s, "Unexpected connector typ"},
        {header("ES256", "dppCon", "") + "." + p + "." + s, "Missing connector kid"},
        {h + ".!." + s, "Invalid connector payload encoding"},
        {h + "." + p + ".!", "Invalid connector signature encoding"},
        {header("ES384", "dppCon", kid) + "." + p + "." + s,
         "Connector alg does not match C-sign key"},
        {h + "." + p + ".AA", "Invalid JWS signature length"}};
    for (const auto &entry : invalid) {
        std::time_t expiry = 99;
        group = role = "stale";
        EXPECT_FALSE(
            dpp::validate_signed_connector(entry.first, csign.get(), group, role, expiry, error));
        EXPECT_EQ(entry.second, error);
        EXPECT_TRUE(group.empty());
        EXPECT_TRUE(role.empty());
        EXPECT_EQ(0U, expiry);
    }
    std::time_t expiry = 99;
    EXPECT_FALSE(dpp::validate_signed_connector(connector, nullptr, group, role, expiry, error));
    EXPECT_EQ("Missing C-sign public key", error);
    // A correctly signed but unsupported role reaches semantic validation after signature checking.
    initialize(NID_X9_62_prime256v1, "unsupported");
    ASSERT_FALSE(HasFatalFailure());
    pub.clear();
    ASSERT_TRUE(
        dpp::build_signed_connector_from_payload(csign.get(), payload, connector, pub, kid, error));
    EXPECT_FALSE(
        dpp::validate_signed_connector(connector, csign.get(), group, role, expiry, error));
    EXPECT_EQ("Unsupported netRole in connector", error);
}

// PUBLIC_INTERFACE
/** Unverified role extraction handles object/array groups but is never used as signature validation. */
TEST(DppConnectorRoles, ExtractionSchemaFailuresAndObjectGroups)
{
    for (const auto &text : {std::string("{"), std::string("{}"), std::string(R"({"groups":[]})"),
                             std::string(R"({"groups":[null,{},{"netRole":1},{"netRole":""}]})")}) {
        std::string role = "stale";
        EXPECT_FALSE(dpp::extract_dpp_netrole("h." + encode_component(text) + ".s", role));
        EXPECT_TRUE(role.empty());
    }
    for (const auto &compact :
         {std::string("h"), std::string("h.p"), std::string("h..s"), std::string("h.!.s")}) {
        std::string role = "stale";
        EXPECT_FALSE(dpp::extract_dpp_netrole(compact, role));
        EXPECT_TRUE(role.empty());
        EXPECT_FALSE(dpp::connector_has_netrole(compact, "sta"));
    }
    for (const auto &groups : {std::string(R"({"netRole":"STA"})"),
                               std::string(R"([null,{},{"netRole":"STA"},{"netRole":"ap"}])")}) {
        const auto compact = "h." + encode_component(R"({"groups":)" + groups + "}") + ".s";
        std::string role;
        ASSERT_TRUE(dpp::extract_dpp_netrole(compact, role));
        EXPECT_EQ("STA", role);
        EXPECT_TRUE(dpp::connector_has_netrole(compact, "sta"));
        EXPECT_FALSE(dpp::connector_has_netrole(compact, "ap"));
    }
}

// PUBLIC_INTERFACE
/** Optional ppKey publishes only public coordinates; explicit role and passphrase options survive. */
TEST_F(DppConfigurationTest, ConfigurationObjectWithPpKeyAndPassphrase)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    dpp::DppConfigurationObjectOptions options;
    options.include_discovery = false;
    options.include_net_role  = true;
    options.net_role          = "mapController";
    options.akm.clear();
    options.passphrase = "test-passphrase";
    std::string config;
    ASSERT_TRUE(dpp::build_dpp_configuration_object(csign.get(), private_hex(netaccess.get()),
                                                    payload, "", "", "", options, config, error))
        << error;
    JsonPtr root(json_tokener_parse(config.c_str()), json_object_put);
    ASSERT_NE(nullptr, root.get());
    EXPECT_STREQ("mapController", json_object_get_string(field(root.get(), "netRole")));
    auto *cred = field(root.get(), "cred");
    ASSERT_NE(nullptr, cred);
    EXPECT_STREQ("dpp", json_object_get_string(field(cred, "akm")));
    EXPECT_STREQ("test-passphrase", json_object_get_string(field(cred, "pass")));
    auto *pp = field(cred, "ppKey");
    ASSERT_NE(nullptr, pp);
    std::string pp_hex, expected;
    ASSERT_TRUE(dpp::jwk_pubkey_to_der_hex(pp, pp_hex));
    ASSERT_TRUE(dpp::derive_pubkey_hex_from_private(private_hex(netaccess.get()), expected));
    EXPECT_EQ(expected, pp_hex);
    json_object *unused = nullptr;
    EXPECT_FALSE(json_object_object_get_ex(pp, "d", &unused));
    EXPECT_FALSE(json_object_object_get_ex(pp, "kid", &unused));
    EXPECT_FALSE(json_object_object_get_ex(cred, "psk_hex", &unused));
    EXPECT_FALSE(json_object_object_get_ex(cred, "SSIDAdvertisement", &unused));
    EXPECT_FALSE(
        dpp::build_dpp_configuration_object(csign.get(), "", "", "41", "", "", config, error));
    EXPECT_EQ("Missing connector payload", error);
    EXPECT_FALSE(dpp::build_dpp_configuration_object(csign.get(), "", payload, "41", "unknown", "",
                                                     config, error));
    EXPECT_EQ("Connector netRole mismatch", error);
}

// PUBLIC_INTERFACE
/** JWS helpers reject absent parameters and unsupported curves without publishing a JWK. */
TEST_F(DppConfigurationTest, JwsParameterAndUnsupportedCurveGuards)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_FALSE(dpp::verify_jws_signature(nullptr, EVP_sha256(), 32, "input", "", error));
    EXPECT_EQ("Missing signature parameters", error);
    EXPECT_FALSE(dpp::verify_jws_signature(csign.get(), nullptr, 32, "input", "", error));
    EXPECT_EQ("Missing signature parameters", error);
    EXPECT_FALSE(dpp::verify_jws_signature(csign.get(), EVP_sha256(), 32, "input", "short", error));
    EXPECT_EQ("Invalid JWS signature length", error);
    json_object *raw = nullptr;
    EXPECT_FALSE(dpp::ec_key_to_jwk(nullptr, &raw, error));
    EXPECT_EQ("Missing key", error);
    EXPECT_EQ(nullptr, raw);
    EXPECT_FALSE(dpp::ec_key_to_jwk(csign.get(), nullptr, error));
    JsonPtr parent(json_object_new_object(), json_object_put);
    EXPECT_FALSE(dpp::add_public_jwk(nullptr, "key", csign.get(), "", error));
    EXPECT_FALSE(dpp::add_public_jwk(parent.get(), nullptr, csign.get(), "", error));
    EXPECT_FALSE(dpp::add_public_jwk(parent.get(), "key", nullptr, "", error));
    EXPECT_EQ("Missing JWK parameters", error);
    EXPECT_FALSE(dpp::add_discovery_ssid(nullptr, "41", error));
    EXPECT_EQ("Missing discovery object", error);

    dpp::DppKeyPtr unsupported;
    ASSERT_TRUE(dpp::generate_ec_key(NID_secp224r1, unsupported));
    EXPECT_FALSE(dpp::ec_key_to_jwk(unsupported.get(), &raw, error));
    EXPECT_EQ("Unsupported EC curve", error);
    EXPECT_EQ(nullptr, raw);
    std::string alg;
    const EVP_MD *md = nullptr;
    size_t part      = 0;
    EXPECT_FALSE(dpp::ec_key_info(unsupported.get(), alg, md, part));
    std::string connector = "stale", pub, kid = "stale";
    EXPECT_FALSE(
        dpp::build_signed_connector_from_payload(nullptr, payload, connector, pub, kid, error));
    EXPECT_EQ("Missing C-sign key", error);
    EXPECT_TRUE(connector.empty());
    EXPECT_TRUE(kid.empty());
    pub = "00";
    EXPECT_FALSE(
        dpp::build_signed_connector_from_payload(csign.get(), payload, connector, pub, kid, error));
    EXPECT_EQ("Failed computing connector kid", error);
    pub.clear();
    EXPECT_FALSE(dpp::build_signed_connector_from_payload(unsupported.get(), payload, connector,
                                                          pub, kid, error));
    EXPECT_EQ("Unsupported C-sign key", error);
}

// PUBLIC_INTERFACE
/** A valid signature still requires a valid payload; absent typ is accepted by the validator. */
TEST_F(DppConfigurationTest, SignedPayloadValidationAndOptionalTyp)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    std::string connector, pub, kid;
    ASSERT_TRUE(
        dpp::build_signed_connector_from_payload(csign.get(), payload, connector, pub, kid, error));
    const auto h = encode_component(R"({"alg":"ES256","kid":")" + kid + R"("})");
    for (const auto &body : {std::string("{"), payload}) {
        const auto input = h + "." + encode_component(body);
        std::string signature;
        ASSERT_TRUE(dpp::jws_sign(csign.get(), EVP_sha256(), 32, input, signature, error)) << error;
        std::string group, role;
        std::time_t expiry  = 99;
        const bool accepted = dpp::validate_signed_connector(
            input + "." + encode_component(signature), csign.get(), group, role, expiry, error);
        EXPECT_EQ(body == payload, accepted);
        if (accepted) {
            EXPECT_EQ("test-group", group);
            EXPECT_EQ("mapAgent", role);
            EXPECT_EQ(0U, expiry);
            EXPECT_TRUE(error.empty());
        } else {
            EXPECT_EQ("Invalid connector payload JSON", error);
        }
    }
}

// PUBLIC_INTERFACE
/** Non-object entries and incorrectly typed nested metadata cannot invent roles or radio IDs. */
TEST(DppConfigRequestObjects, IgnoresMalformedObjectShapes)
{
    const std::vector<std::string> inputs{
        "[]", R"([null,1,"text",[],{}])", R"({"netRole":1,"bSTAList":1})",
        R"({"bSTAList":[null,{},{"netRole":false,"akm":false,"RadioList":1},
             {"RadioList":[null,{},{"RUID":false}]}]})"};
    for (const auto &input : inputs) {
        SCOPED_TRACE(input);
        std::vector<std::string> roles{"stale"};
        std::string akm = "stale";
        bool links      = true;
        std::unordered_set<std::string> ruids{"stale"};
        ASSERT_TRUE(dpp::parse_dpp_config_request_objects(input, roles, &akm, &links, &ruids));
        EXPECT_TRUE(roles.empty());
        EXPECT_TRUE(akm.empty());
        EXPECT_FALSE(links);
        EXPECT_TRUE(ruids.empty());
    }
}

// PUBLIC_INTERFACE
/** A key object without key material cannot encode a C-sign public key in either builder. */
TEST_F(DppConfigurationTest, EmptySignerFailsPublicKeyEncoding)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    dpp::DppKeyPtr empty(EVP_PKEY_new());
    ASSERT_NE(nullptr, empty.get());
    std::string connector = "stale", pub, kid = "stale", netpub;
    EXPECT_FALSE(
        dpp::build_signed_connector_from_payload(empty.get(), payload, connector, pub, kid, error));
    EXPECT_EQ("Failed encoding C-sign public key", error);
    EXPECT_TRUE(connector.empty());
    EXPECT_TRUE(pub.empty());
    EXPECT_TRUE(kid.empty());
    connector = "stale";
    EXPECT_FALSE(dpp::build_signed_connector(empty.get(), private_hex(netaccess.get()), "sta", "",
                                             0, connector, pub, netpub, error));
    EXPECT_EQ("Failed encoding C-sign public key", error);
    EXPECT_TRUE(connector.empty());
    EXPECT_TRUE(netpub.empty());
}

// PUBLIC_INTERFACE
/** A public EC key can be encoded and identified but cannot produce a connector signature. */
TEST_F(DppConfigurationTest, PublicOnlySignerFailsBothConnectorBuilders)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    std::string pub;
    ASSERT_TRUE(dpp::derive_pubkey_hex_from_private(private_hex(csign.get()), pub));
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> public_only(nullptr, EVP_PKEY_free);
    ASSERT_TRUE(dpp::parse_ec_pubkey_hex(pub, public_only));
    std::string connector = "stale", cached_pub, kid, netpub;
    EXPECT_FALSE(dpp::build_signed_connector_from_payload(public_only.get(), payload, connector,
                                                          cached_pub, kid, error));
    EXPECT_TRUE(connector.empty());
    EXPECT_EQ(pub, cached_pub);
    EXPECT_FALSE(kid.empty());
    EXPECT_FALSE(error.empty()); // Signing may fail at initialization or finalization.
    connector = "stale";
    cached_pub.clear();
    EXPECT_FALSE(dpp::build_signed_connector(public_only.get(), private_hex(netaccess.get()), "sta",
                                             "test-group", 0, connector, cached_pub, netpub,
                                             error));
    EXPECT_TRUE(connector.empty());
    EXPECT_FALSE(error.empty());
    std::string object = "stale";
    EXPECT_FALSE(dpp::build_dpp_configuration_object(public_only.get(), "", payload, "41", "", "",
                                                     object, error));
    EXPECT_TRUE(object.empty());
    EXPECT_FALSE(error.empty());
}

// PUBLIC_INTERFACE
/** Unsupported signing and network-access curves fail at their distinct builder stages. */
TEST_F(DppConfigurationTest, UnsupportedCurvesInPrivateKeyConnectorBuilder)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    dpp::DppKeyPtr unsupported;
    ASSERT_TRUE(dpp::generate_ec_key(NID_secp224r1, unsupported));
    std::string connector = "stale", pub, netpub;
    EXPECT_FALSE(dpp::build_signed_connector(unsupported.get(), private_hex(netaccess.get()), "sta",
                                             "", 0, connector, pub, netpub, error));
    EXPECT_EQ("Unsupported C-sign key", error);
    EXPECT_TRUE(connector.empty());
    pub.clear();
    netpub.clear();
    EXPECT_FALSE(dpp::build_signed_connector(csign.get(), private_hex(unsupported.get()), "sta", "",
                                             0, connector, pub, netpub, error));
    EXPECT_EQ("Unsupported EC curve", error);
    EXPECT_TRUE(connector.empty());
    EXPECT_FALSE(netpub.empty()); // DER derivation succeeded before JWK conversion failed.
}

// PUBLIC_INTERFACE
/** Configuration assembly forwards payload/role errors and rejects unsupported ppKey curves. */
TEST_F(DppConfigurationTest, ConfigurationObjectFailurePropagation)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    std::string object = "stale";
    EXPECT_FALSE(
        dpp::build_dpp_configuration_object(csign.get(), "", "{", "41", "", "", object, error));
    EXPECT_EQ("Invalid connector payload JSON", error);
    EXPECT_TRUE(object.empty());

    JsonPtr root(json_tokener_parse(payload.c_str()), json_object_put);
    ASSERT_NE(nullptr, root.get());
    json_object *groups = field(root.get(), "groups");
    ASSERT_NE(nullptr, groups);
    json_object *group = json_object_array_get_idx(groups, 0);
    ASSERT_NE(nullptr, group);
    json_object_object_add(group, "netRole", json_object_new_string("unknown"));
    const std::string unsupported_payload =
        json_object_to_json_string_ext(root.get(), JSON_C_TO_STRING_PLAIN);
    EXPECT_FALSE(dpp::build_dpp_configuration_object(csign.get(), "", unsupported_payload, "41", "",
                                                     "", object, error));
    EXPECT_EQ("Unsupported connector netRole", error);
    EXPECT_TRUE(object.empty());

    dpp::DppKeyPtr unsupported;
    ASSERT_TRUE(dpp::generate_ec_key(NID_secp224r1, unsupported));
    EXPECT_FALSE(dpp::build_dpp_configuration_object(csign.get(), private_hex(unsupported.get()),
                                                     payload, "41", "", "", object, error));
    EXPECT_EQ("Unsupported EC curve", error);
    EXPECT_TRUE(object.empty());
    EXPECT_FALSE(dpp::build_dpp_configuration_object(unsupported.get(), "", payload, "41", "", "",
                                                     object, error));
    EXPECT_EQ("Unsupported C-sign key", error);
    EXPECT_TRUE(object.empty());
}

// PUBLIC_INTERFACE
/** Signature helpers reject unusable keys at OpenSSL initialization rather than size guards. */
TEST(DppJwsSignature, EmptyKeyInitializationFailures)
{
    dpp::DppKeyPtr empty(EVP_PKEY_new());
    ASSERT_NE(nullptr, empty.get());
    std::string signature = "stale", error;
    EXPECT_FALSE(dpp::jws_sign(empty.get(), EVP_sha256(), 32, "header.payload", signature, error));
    EXPECT_EQ("Failed initializing signature", error);
    EXPECT_TRUE(signature.empty());
    EXPECT_FALSE(dpp::verify_jws_signature(empty.get(), EVP_sha256(), 32, "header.payload",
                                           std::string(64, '\0'), error));
    EXPECT_EQ("Failed initializing signature verification", error);
}

// PUBLIC_INTERFACE
/** A syntactically valid connector cannot be validated with a non-DPP signing curve. */
TEST_F(DppConfigurationTest, ValidatorRejectsUnsupportedCsignCurve)
{
    initialize();
    ASSERT_FALSE(HasFatalFailure());
    std::string connector, pub, kid;
    ASSERT_TRUE(
        dpp::build_signed_connector_from_payload(csign.get(), payload, connector, pub, kid, error));
    dpp::DppKeyPtr unsupported;
    ASSERT_TRUE(dpp::generate_ec_key(NID_secp224r1, unsupported));
    std::string group = "stale", role = "stale";
    std::time_t expiry = 99;
    EXPECT_FALSE(
        dpp::validate_signed_connector(connector, unsupported.get(), group, role, expiry, error));
    EXPECT_EQ("Unsupported C-sign key type", error);
    EXPECT_TRUE(group.empty());
    EXPECT_TRUE(role.empty());
    EXPECT_EQ(0U, expiry);
}

// PUBLIC_INTERFACE
/** A valid object-form group is accepted, while unused credential options remain absent. */
TEST_F(DppConfigurationTest, ObjectGroupAndMinimalCredentialShape)
{
    initialize(NID_X9_62_prime256v1, "AP");
    ASSERT_FALSE(HasFatalFailure());
    JsonPtr root(json_tokener_parse(payload.c_str()), json_object_put);
    ASSERT_NE(nullptr, root.get());
    // Take a reference before replacing the array that owns this group.
    auto *groups = field(root.get(), "groups");
    ASSERT_NE(nullptr, groups);
    auto *group = json_object_array_get_idx(groups, 0);
    ASSERT_NE(nullptr, group);
    json_object_object_add(root.get(), "groups", json_object_get(group));
    payload = json_object_to_json_string_ext(root.get(), JSON_C_TO_STRING_PLAIN);
    dpp::DppConfigurationObjectOptions options;
    options.ssid_advertisement_valid = true;
    options.ssid_advertisement       = true;
    std::string object;
    ASSERT_TRUE(dpp::build_dpp_configuration_object(csign.get(), "", payload, "207e", "ap",
                                                    "test-group", options, object, error))
        << error;
    JsonPtr result(json_tokener_parse(object.c_str()), json_object_put);
    ASSERT_NE(nullptr, result.get());
    EXPECT_STREQ("infra", json_object_get_string(field(result.get(), "wi-fi_tech")));
    auto *discovery = field(result.get(), "discovery");
    ASSERT_NE(nullptr, discovery);
    EXPECT_STREQ(" ~", json_object_get_string(field(discovery, "ssid")));
    auto *cred = field(result.get(), "cred");
    ASSERT_NE(nullptr, cred);
    EXPECT_TRUE(json_object_get_boolean(field(cred, "SSIDAdvertisement")));
    json_object *unused = nullptr;
    EXPECT_FALSE(json_object_object_get_ex(result.get(), "netRole", &unused));
    EXPECT_FALSE(json_object_object_get_ex(result.get(), "dfCounterThreshold", &unused));
    for (const char *name : {"pass", "psk_hex", "SecurityIEs", "ppKey"}) {
        EXPECT_FALSE(json_object_object_get_ex(cred, name, &unused)) << name;
    }
    std::string parsed_group, role;
    std::time_t expiry = 99;
    ASSERT_TRUE(
        dpp::validate_signed_connector(json_object_get_string(field(cred, "signedConnector")),
                                       csign.get(), parsed_group, role, expiry, error))
        << error;
    EXPECT_EQ("test-group", parsed_group);
    EXPECT_EQ("ap", role);
    EXPECT_EQ(0U, expiry);
}
} // namespace
