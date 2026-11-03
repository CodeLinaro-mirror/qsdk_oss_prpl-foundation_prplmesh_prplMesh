/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include "../../controller_dpp_configuration.h"
#include "../../dpp/dpp_internal.h"
#include "../db.h"

#include <gtest/gtest.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace dpp = son::controller_dpp;

// PUBLIC_INTERFACE
/** Own the configuration and logger referenced by the database for its entire lifetime. */
class DppDatabaseTest : public ::testing::Test {
protected:
    /** Construct only the in-memory database; tested APIs never call the data-model client. */
    void SetUp() override
    {
        // Both level strings are parsed at construction, even when syslog is disabled.
        logging_config.sLog.global_levels = "error,fatal,warning";
        logging_config.sLog.syslog_levels = "off";
        logger.reset(new beerocks::logging("dpp-test", logging_config.sLog));
        database.reset(
            new son::db(config, *logger, tlvf::mac_from_string("02:00:00:00:00:01"), nullptr));
        dpp::DppKeyPtr key;
        ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, key));
        const int length = i2d_PUBKEY(key.get(), nullptr);
        ASSERT_GT(length, 0);
        der.resize(static_cast<size_t>(length));
        auto *ptr = der.data();
        ASSERT_EQ(length, i2d_PUBKEY(key.get(), &ptr));
        public_key = dpp::base64url_encode(der.data(), der.size());
    }

    /** Insert a fresh valid bootstrap entry with the given alias. */
    void insert(const std::string &alias, const std::string &key = "")
    {
        son::db::sDppBootstrappingInfo info;
        ASSERT_TRUE(database->parse_dpp_bootstrap_info(
            "DPP:M:020000000002;K:" + (key.empty() ? public_key : key) + ";;", info, error))
            << error;
        info.alias = alias;
        ASSERT_TRUE(database->add_dpp_bootstrap_info(std::move(info), error)) << error;
    }

    beerocks::config_file::sConfigMaster logging_config{};
    son::db::sDbMasterConfig config{};
    std::unique_ptr<beerocks::logging> logger;
    std::unique_ptr<son::db> database;
    std::vector<uint8_t> der;
    std::string public_key, error;
};

// PUBLIC_INTERFACE
/** Fixture logging retains warning/error/fatal diagnostics and disables syslog levels. */
TEST_F(DppDatabaseTest, InitializesValidLoggingLevels)
{
    EXPECT_EQ("error, fatal, warning", logger->get_log_levels().to_string());
    EXPECT_TRUE(logger->get_syslog_levels().is_off());
    EXPECT_FALSE(logger->get_syslog_enabled());
}

// PUBLIC_INTERFACE
/** A missing mandatory key yields no URI; optional defaults are omitted from a minimal URI. */
TEST_F(DppDatabaseTest, GeneratesMinimalBootstrapUri)
{
    son::db::sDppBootstrappingInfo info;
    EXPECT_TRUE(database->calculate_dpp_bootstrapping_str(info).empty());
    info.public_key = public_key;
    EXPECT_EQ("DPP:K:" + public_key + ";;", database->calculate_dpp_bootstrapping_str(info));
}

// PUBLIC_INTERFACE
/** Full bootstrap serialization uses ordered channels and a compact MAC and remains parseable. */
TEST_F(DppDatabaseTest, GeneratesCompleteBootstrapUriAndRoundTrips)
{
    son::db::sDppBootstrappingInfo info;
    info.public_key              = public_key;
    info.mac                     = tlvf::mac_from_string("02:00:00:00:00:02");
    info.operating_class_channel = {{115, 36}, {81, 6}, {81, 1}};
    info.info                    = "test-device";
    info.version                 = 3;
    info.host                    = "example.test";
    const auto uri               = database->calculate_dpp_bootstrapping_str(info);
    EXPECT_EQ("DPP:C:81/6,81/1,115/36;M:020000000002;I:test-device;V:3;H:example.test;K:" +
                  public_key + ";;",
              uri);
    son::db::sDppBootstrappingInfo parsed;
    ASSERT_TRUE(database->parse_dpp_bootstrap_info(uri, parsed, error)) << error;
    EXPECT_EQ(info.public_key, parsed.public_key);
    EXPECT_EQ(info.mac, parsed.mac);
    EXPECT_EQ(info.operating_class_channel, parsed.operating_class_channel);
    EXPECT_EQ(info.info, parsed.info);
    EXPECT_EQ(info.version, parsed.version);
    EXPECT_EQ(info.host, parsed.host);
    EXPECT_EQ(uri, database->calculate_dpp_bootstrapping_str(parsed));
}

// PUBLIC_INTERFACE
/** An untouched in-memory C-sign cache reports absence and clears stale caller output. */
TEST_F(DppDatabaseTest, EmptyCsignCacheClearsOutput)
{
    EXPECT_FALSE(database->has_dpp_configurator_csign_key());
    std::string output = "stale-key";
    EXPECT_FALSE(database->get_dpp_configurator_csign_key_hex(output));
    EXPECT_TRUE(output.empty());
    // Do not call load-from-store here: it resolves a production install-relative path.
}

// PUBLIC_INTERFACE
/** Optional URI fields parse correctly and the hash is SHA-256 over decoded public DER. */
TEST_F(DppDatabaseTest, ParsesCompleteUriAndHashesPublicKey)
{
    son::db::sDppBootstrappingInfo info;
    const std::string uri =
        " \tDPP:C:81/1,115/36;M:020000000002;I:unit-test;V:3;H:example.test;K:" + public_key +
        ";; \n";
    ASSERT_TRUE(database->parse_dpp_bootstrap_info(uri, info, error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(tlvf::mac_from_string("02:00:00:00:00:02"), info.mac);
    EXPECT_EQ("unit-test", info.info);
    EXPECT_EQ(3, info.version);
    EXPECT_EQ("example.test", info.host);
    EXPECT_EQ(public_key, info.public_key);
    EXPECT_EQ((std::multimap<uint8_t, uint8_t>{{81, 1}, {115, 36}}), info.operating_class_channel);
    std::array<uint8_t, 32> expected{};
    ASSERT_NE(nullptr, SHA256(der.data(), der.size(), expected.data()));
    EXPECT_EQ(expected, info.pkhash);
    EXPECT_TRUE(info.pkhash_valid);
    EXPECT_FALSE(info.chirp_matched);
}

// PUBLIC_INTERFACE
/** Key-only URIs accept padded standard base64 and unpadded URL-safe encoding identically. */
TEST_F(DppDatabaseTest, ParsesKeyOnlyUriInBothBase64Alphabets)
{
    std::string standard(4 * ((der.size() + 2) / 3), '\0');
    const int encoded_length =
        EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&standard[0]), der.data(), der.size());
    ASSERT_GT(encoded_length, 0);
    standard.resize(static_cast<size_t>(encoded_length));
    std::array<uint8_t, 32> expected{};
    ASSERT_NE(nullptr, SHA256(der.data(), der.size(), expected.data()));
    for (const auto &key : {standard, public_key}) {
        SCOPED_TRACE(key);
        son::db::sDppBootstrappingInfo info;
        error = "stale";
        ASSERT_TRUE(database->parse_dpp_bootstrap_info("DPP:K:" + key + ";;", info, error))
            << error;
        EXPECT_TRUE(error.empty());
        EXPECT_EQ(key, info.public_key);
        EXPECT_TRUE(info.operating_class_channel.empty());
        EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, info.mac);
        EXPECT_TRUE(info.info.empty());
        EXPECT_TRUE(info.host.empty());
        EXPECT_TRUE(info.pkhash_valid);
        EXPECT_EQ(expected, info.pkhash);
        EXPECT_EQ(dpp::bytes_to_hex_string(expected.data(), expected.size()), info.pkhash_hex);
        EXPECT_FALSE(info.chirp_matched);
    }
}

// PUBLIC_INTERFACE
/** A parsed and stored bootstrap entry matches its chirp hash; a different full hash never matches. */
TEST_F(DppDatabaseTest, ParsedBootstrapChirpMatchAndMismatch)
{
    insert("parsed-chirp");
    ASSERT_FALSE(HasFatalFailure());
    const auto *stored = database->get_dpp_bootstrap_info("parsed-chirp");
    ASSERT_NE(nullptr, stored);
    ASSERT_TRUE(stored->pkhash_valid);
    // Exercise hex transport of a real key hash, including any high-bit SHA-256 bytes.
    const std::vector<uint8_t> encoded(stored->pkhash_hex.begin(), stored->pkhash_hex.end());
    std::string received = "stale";
    const auto *matched =
        database->dpp_chirp_hash_matches(encoded.data(), encoded.size(), received);
    ASSERT_NE(nullptr, matched);
    EXPECT_EQ(stored, matched);
    EXPECT_EQ("parsed-chirp", matched->alias);
    EXPECT_EQ(stored->pkhash_hex, received);

    auto wrong = stored->pkhash;
    wrong[0] ^= 1;
    const auto wrong_hex = dpp::bytes_to_hex_string(wrong.data(), wrong.size());
    const std::vector<uint8_t> wrong_encoded(wrong_hex.begin(), wrong_hex.end());
    received = "stale";
    EXPECT_EQ(nullptr, database->dpp_chirp_hash_matches(wrong_encoded.data(), wrong_encoded.size(),
                                                        received));
    // Mismatch diagnostics retain the supplied ASCII bytes, not a decoded digest.
    EXPECT_EQ(dpp::bytes_to_hex_string(wrong_encoded.data(), wrong_encoded.size()), received);
}

// PUBLIC_INTERFACE
/** Raw SHA-256 bytes from a parsed bootstrap key match only the stored public-key hash. */
TEST_F(DppDatabaseTest, ParsedBootstrapRawChirpMatchAndMismatch)
{
    insert("parsed-raw-chirp");
    ASSERT_FALSE(HasFatalFailure());
    const auto *stored = database->get_dpp_bootstrap_info("parsed-raw-chirp");
    ASSERT_NE(nullptr, stored);
    ASSERT_TRUE(stored->pkhash_valid);

    std::string received = "stale";
    const auto *matched =
        database->dpp_chirp_hash_matches(stored->pkhash.data(), stored->pkhash.size(), received);
    // Keep the real SHA-256 bytes, including high-bit values, to expose signed-byte comparisons.
    EXPECT_EQ(stored, matched);
    EXPECT_EQ(stored->pkhash_hex, received);

    auto wrong = stored->pkhash;
    wrong[0] ^= 1;
    received = "stale";
    EXPECT_EQ(nullptr, database->dpp_chirp_hash_matches(wrong.data(), wrong.size(), received));
    EXPECT_EQ(dpp::bytes_to_hex_string(wrong.data(), wrong.size()), received);
}

// PUBLIC_INTERFACE
/** Malformed URI syntax, duplicate fields, and invalid numeric/base64 fields are rejected. */
TEST_F(DppDatabaseTest, RejectsMalformedUris)
{
    const std::vector<std::pair<std::string, std::string>> cases{
        {"K:" + public_key + ";;", "missing DPP prefix"},
        {"DPP:K:" + public_key + ";", "missing DPP suffix"},
        {"DPP:;;", "empty DPP payload"},
        {"DPP:I:only-info;;", "missing mandatory K field"},
        {"DPP:KK:value;;", "invalid DPP field format"},
        {"DPP:K:;;", "empty value for DPP field 'K'"},
        {"DPP:V:256;K:" + public_key + ";;", "invalid version"},
        {"DPP:M:0200;K:" + public_key + ";;", "invalid DPP MAC length"},
        {"DPP:M:zz0000000002;K:" + public_key + ";;", "invalid DPP MAC"},
        {"DPP:C:81;K:" + public_key + ";;", "invalid channel list"},
        {"DPP:C:256/1;K:" + public_key + ";;", "invalid operating class"},
        {"DPP:C:81/-1;K:" + public_key + ";;", "invalid channel"},
        {"DPP:C:81/1,81/1;K:" + public_key + ";;", "duplicate operating class/channel pair"},
        {"DPP:V:2;V:3;K:" + public_key + ";;", "duplicate V field"},
        {"DPP:K:AA=A;;", "invalid base64 padding position"},
        {"DPP:K:AA===;;", "too much base64 padding"},
        {"DPP:K:!invalid;;", "invalid character in public key"},
        {"DPP:K:A;;", "invalid base64 length in public key"},
        {"DPP:K:YWJj;;", "decoded public key too short"},
        {"DPP:K:" + public_key + ";K:" + public_key + ";;", "duplicate K field"}};
    for (const auto &entry : cases) {
        SCOPED_TRACE(entry.first);
        son::db::sDppBootstrappingInfo info;
        error = "stale";
        EXPECT_FALSE(database->parse_dpp_bootstrap_info(entry.first, info, error));
        EXPECT_EQ(entry.second, error);
    }
}

// PUBLIC_INTERFACE
/** Alias replacement, sole-entry selection and removals reflect the current store. */
TEST_F(DppDatabaseTest, AliasValidationReplacementAndLookup)
{
    for (const auto &alias :
         {std::string(""), std::string("bad alias"), std::string("../invalid")}) {
        son::db::sDppBootstrappingInfo info;
        info.alias = alias;
        EXPECT_FALSE(database->add_dpp_bootstrap_info(std::move(info), error));
        EXPECT_FALSE(database->has_dpp_bootstrap_info());
    }
    insert("test-1.example");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_NE(nullptr, database->get_sole_dpp_bootstrap_info());
    EXPECT_EQ("test-1.example", database->get_sole_dpp_bootstrap_info()->alias);
    auto *stored = database->get_dpp_bootstrap_info("test-1.example");
    ASSERT_NE(nullptr, stored);
    const auto hash = stored->pkhash;
    EXPECT_EQ(stored, database->get_dpp_bootstrap_info_by_pkhash(hash));
    EXPECT_EQ(stored, database->get_dpp_bootstrap_info_by_mac(stored->mac));
    const auto old_fingerprint = database->calculate_dpp_bootstrap_map_fingerprint();
    insert("test-1.example");
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(old_fingerprint, database->calculate_dpp_bootstrap_map_fingerprint());
    insert("second");
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(nullptr, database->get_sole_dpp_bootstrap_info());
    EXPECT_TRUE(database->remove_dpp_bootstrap_info("second"));
    EXPECT_FALSE(database->remove_dpp_bootstrap_info("missing"));
    database->clear_dpp_bootstrap_info_db();
    EXPECT_FALSE(database->has_dpp_bootstrap_info());
    EXPECT_EQ(nullptr, database->get_dpp_bootstrap_info("test-1.example"));
    EXPECT_EQ(nullptr, database->get_sole_dpp_bootstrap_info());
    EXPECT_TRUE(database->calculate_dpp_bootstrap_map_fingerprint().empty());
}

// PUBLIC_INTERFACE
/** Fingerprints are independent of insertion order and change when a URI key is replaced. */
TEST_F(DppDatabaseTest, StableFingerprintAndKeyReplacement)
{
    insert("z");
    ASSERT_FALSE(HasFatalFailure());
    insert("a");
    ASSERT_FALSE(HasFatalFailure());
    const auto first = database->calculate_dpp_bootstrap_map_fingerprint();
    EXPECT_EQ("a|" + public_key + ";z|" + public_key + ";", first);
    database->clear_dpp_bootstrap_info_db();
    insert("a");
    ASSERT_FALSE(HasFatalFailure());
    insert("z");
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(first, database->calculate_dpp_bootstrap_map_fingerprint());
    // The URI parser accepts arbitrary decoded keys >=32 bytes, independently of session DER parsing.
    const std::vector<uint8_t> replacement(32, 0x42);
    insert("a", dpp::base64url_encode(replacement.data(), replacement.size()));
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_NE(first, database->calculate_dpp_bootstrap_map_fingerprint());
}

// PUBLIC_INTERFACE
/** Raw/hex chirp hashes, optional terminators and invalid-length rejection use real lookup. */
TEST_F(DppDatabaseTest, ChirpHashEncodings)
{
    son::db::sDppBootstrappingInfo info;
    info.alias        = "chirp";
    info.pkhash_valid = true;
    for (size_t i = 0; i < info.pkhash.size(); ++i) {
        info.pkhash[i] = static_cast<uint8_t>(i + 1); // Below 0x80 for the basic encoding cases.
    }
    const auto hash = info.pkhash;
    ASSERT_TRUE(database->add_dpp_bootstrap_info(std::move(info), error));
    std::vector<uint8_t> raw(hash.begin(), hash.end());
    const auto hex = dpp::bytes_to_hex_string(hash.data(), hash.size());
    std::vector<std::vector<uint8_t>> encodings{raw, std::vector<uint8_t>(hex.begin(), hex.end())};
    raw.push_back(0);
    encodings.push_back(raw);
    auto terminated_hex = std::vector<uint8_t>(hex.begin(), hex.end());
    terminated_hex.push_back(0);
    encodings.push_back(terminated_hex);
    for (const auto &encoding : encodings) {
        std::string received = "stale";
        const auto *match =
            database->dpp_chirp_hash_matches(encoding.data(), encoding.size(), received);
        ASSERT_NE(nullptr, match);
        EXPECT_EQ("chirp", match->alias);
        EXPECT_EQ(hex, received);
    }
    std::string received = "stale";
    EXPECT_EQ(nullptr, database->dpp_chirp_hash_matches(hash.data(), 31, received));
    EXPECT_EQ(dpp::bytes_to_hex_string(hash.data(), 31), received);
    EXPECT_EQ(nullptr, database->dpp_chirp_hash_matches(nullptr, 32, received));
    EXPECT_TRUE(received.empty());
}

// PUBLIC_INTERFACE
/** Regression: SHA-256 bytes with the high bit set must compare as unsigned bytes. */
TEST_F(DppDatabaseTest, ChirpHashHighBitRegression)
{
    son::db::sDppBootstrappingInfo info;
    info.alias        = "high-bit";
    info.pkhash_valid = true;
    info.pkhash.fill(0xff);
    const auto hash = info.pkhash;
    ASSERT_TRUE(database->add_dpp_bootstrap_info(std::move(info), error));
    const auto hex = dpp::bytes_to_hex_string(hash.data(), hash.size());
    for (auto encoding : {std::vector<uint8_t>(hash.begin(), hash.end()),
                          std::vector<uint8_t>(hex.begin(), hex.end())}) {
        for (bool terminated : {false, true}) {
            if (terminated) {
                encoding.push_back(0);
            }
            std::string received = "stale";
            ASSERT_NE(nullptr,
                      database->dpp_chirp_hash_matches(encoding.data(), encoding.size(), received));
            EXPECT_EQ(hex, received);
        }
    }
}

// PUBLIC_INTERFACE
/** Real chirp digests use SHA-256("chirp" || DER), independently of the pkhash fallback. */
TEST_F(DppDatabaseTest, ParsedChirpDigestEncodings)
{
    insert("chirp-digest");
    ASSERT_FALSE(HasFatalFailure());
    const auto *stored = database->get_dpp_bootstrap_info("chirp-digest");
    ASSERT_NE(nullptr, stored);
    ASSERT_TRUE(stored->chirp_hash_valid);
    std::vector<uint8_t> input{'c', 'h', 'i', 'r', 'p'};
    input.insert(input.end(), der.begin(), der.end());
    std::array<uint8_t, SHA256_DIGEST_LENGTH> expected{};
    ASSERT_NE(nullptr, SHA256(input.data(), input.size(), expected.data()));
    EXPECT_EQ(expected, stored->chirp_hash);
    const auto hex = dpp::bytes_to_hex_string(expected.data(), expected.size());
    for (auto encoding : {std::vector<uint8_t>(expected.begin(), expected.end()),
                          std::vector<uint8_t>(hex.begin(), hex.end())}) {
        for (bool terminated : {false, true}) {
            if (terminated) {
                encoding.push_back(0);
            }
            std::string received = "stale";
            EXPECT_EQ(stored,
                      database->dpp_chirp_hash_matches(encoding.data(), encoding.size(), received));
            EXPECT_EQ(hex, received);
        }
    }
}

// PUBLIC_INTERFACE
/** The chirp-specific branch also compares high-bit and embedded-zero octets correctly. */
TEST_F(DppDatabaseTest, ChirpDigestHighBitRegression)
{
    son::db::sDppBootstrappingInfo info;
    info.alias            = "high-bit-chirp";
    info.chirp_hash_valid = true;
    info.chirp_hash.fill(0xff);
    info.chirp_hash[0]     = 0x80;
    info.chirp_hash[1]     = 0;
    info.chirp_hash.back() = 0;
    const auto hash        = info.chirp_hash;
    ASSERT_TRUE(database->add_dpp_bootstrap_info(std::move(info), error));
    const auto hex = dpp::bytes_to_hex_string(hash.data(), hash.size());
    for (auto encoding : {std::vector<uint8_t>(hash.begin(), hash.end()),
                          std::vector<uint8_t>(hex.begin(), hex.end())}) {
        for (bool terminated : {false, true}) {
            if (terminated) {
                encoding.push_back(0);
            }
            std::string received;
            const auto *match =
                database->dpp_chirp_hash_matches(encoding.data(), encoding.size(), received);
            ASSERT_NE(nullptr, match);
            EXPECT_EQ("high-bit-chirp", match->alias);
            EXPECT_EQ(hex, received);
        }
    }
}

// PUBLIC_INTERFACE
/** Empty stores still expose received bytes; null and empty inputs clear stale diagnostics. */
TEST_F(DppDatabaseTest, ChirpDiagnosticsWithoutEntries)
{
    const std::vector<uint8_t> raw(32, 0xff);
    const auto hex = dpp::bytes_to_hex_string(raw.data(), raw.size());
    auto ascii     = std::vector<uint8_t>(hex.begin(), hex.end());
    ascii.push_back(0);
    for (const auto &encoding : {raw, ascii}) {
        std::string received = "stale";
        EXPECT_EQ(nullptr,
                  database->dpp_chirp_hash_matches(encoding.data(), encoding.size(), received));
        EXPECT_EQ(dpp::bytes_to_hex_string(encoding.data(), encoding.size()), received);
    }
    std::string received = "stale";
    EXPECT_EQ(nullptr, database->dpp_chirp_hash_matches(nullptr, 32, received));
    EXPECT_TRUE(received.empty());
    received = "stale";
    EXPECT_EQ(nullptr, database->dpp_chirp_hash_matches(raw.data(), 0, received));
    EXPECT_TRUE(received.empty());
}

// PUBLIC_INTERFACE
/** Missing policy fails for STA, while an explicit backhaul request retains its fallback values. */
TEST_F(DppDatabaseTest, PolicyMissingAgentInvalidHexAndUnconfiguredFallback)
{
    dpp::DppStaConfiguration sta;
    dpp::DppBackhaulStaConfiguration backhaul;
    EXPECT_FALSE(dpp::get_dpp_sta_configuration(*database, nullptr, "", sta, error));
    EXPECT_FALSE(
        dpp::get_dpp_backhaul_sta_configuration(*database, nullptr, "", "", backhaul, error));
    auto agent = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    EXPECT_FALSE(dpp::get_dpp_sta_configuration(*database, agent, "gg", sta, error));
    EXPECT_EQ("Invalid DPP onboarding SSID hex", error);
    EXPECT_FALSE(dpp::get_dpp_sta_configuration(*database, agent, "", sta, error));
    EXPECT_EQ("No configured fronthaul BSS available for DPP sta object", error);
    ASSERT_TRUE(dpp::get_dpp_backhaul_sta_configuration(*database, agent, "54657374", "sae",
                                                        backhaul, error))
        << error;
    EXPECT_EQ("Test", backhaul.ssid);
    EXPECT_EQ("sae", backhaul.akm);
    EXPECT_TRUE(error.empty());
}

// PUBLIC_INTERFACE
/** Regression: global fronthaul policy must be usable when no configured radio BSS exists. */
TEST_F(DppDatabaseTest, GlobalFronthaulPolicyFallbackRegression)
{
    auto agent = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    son::wireless_utils::sBssInfoConf bss{};
    bss.ssid                = "Fronthaul";
    bss.fronthaul           = true;
    bss.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
    bss.network_key         = "test-passphrase";
    database->add_bss_info_configuration(bss);
    dpp::DppStaConfiguration selected;
    ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "", selected, error)) << error;
    EXPECT_EQ("Fronthaul", selected.ssid);
    EXPECT_EQ("psk", selected.akm);
    EXPECT_EQ("test-passphrase", selected.passphrase);
}

// PUBLIC_INTERFACE
/** Regression: per-agent backhaul policy must not fail after a successful fallback selection. */
TEST_F(DppDatabaseTest, AgentBackhaulPolicyFallbackRegression)
{
    auto agent = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    son::wireless_utils::sBssInfoConf bss{};
    bss.ssid                = "Backhaul";
    bss.backhaul            = true;
    bss.authentication_type = WSC::eWscAuth::WSC_AUTH_SAE;
    bss.network_key         = "test-passphrase";
    database->add_bss_info_configuration(agent->al_mac, bss);
    dpp::DppBackhaulStaConfiguration selected;
    ASSERT_TRUE(
        dpp::get_dpp_backhaul_sta_configuration(*database, agent, "", "SAE", selected, error))
        << error;
    EXPECT_EQ("Backhaul", selected.ssid);
    EXPECT_EQ("sae", selected.akm);
    EXPECT_EQ("test-passphrase", selected.passphrase);
}

// PUBLIC_INTERFACE
/** Configured-radio BSS selection exercises the production-private AKM helper without copying it. */
TEST_F(DppDatabaseTest, ConfiguredBackhaulAkmMapping)
{
    using Auth       = WSC::eWscAuth;
    using Additional = son::wireless_utils::eAdditionalAuth;
    struct MappingCase {
        Auth authentication;
        Additional additional;
        const char *expected;
        bool accepted;
    };
    const std::vector<MappingCase> cases{
        {Auth::WSC_AUTH_OPEN, Additional::NONE, "open", true},
        {Auth::WSC_AUTH_WPAPSK, Additional::NONE, "psk", true},
        {Auth::WSC_AUTH_WPA2PSK, Additional::NONE, "psk", true},
        {Auth::WSC_AUTH_SAE, Additional::NONE, "sae", true},
        {Auth::WSC_AUTH_SAE_AKM24, Additional::NONE, "sae", true},
        {Auth::WSC_AUTH_RSN, Additional::WPA3_PERSONAL_COMPATIBILITY, "psk+sae", true},
        {Auth::WSC_AUTH_RSN, Additional::NONE, "", false},
        {Auth::WSC_AUTH_INVALID, Additional::NONE, "", false}};
    const auto ruid = tlvf::mac_from_string("02:00:00:00:01:01");
    auto agent      = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    ASSERT_NE(nullptr, agent->radios.add(ruid));
    for (const auto &entry : cases) {
        SCOPED_TRACE(static_cast<uint16_t>(entry.authentication));
        SCOPED_TRACE(static_cast<int>(entry.additional));
        database->clear_configured_bss_info(ruid);
        son::wireless_utils::sBssInfoConf bss{};
        bss.ssid                = "Backhaul";
        bss.backhaul            = true;
        bss.authentication_type = entry.authentication;
        bss.additional_auth     = entry.additional;
        bss.network_key         = "test-passphrase";
        database->add_configured_bss_info(ruid, bss);
        dpp::DppBackhaulStaConfiguration selected;
        const bool accepted =
            dpp::get_dpp_backhaul_sta_configuration(*database, agent, "", "", selected, error);
        EXPECT_EQ(entry.accepted, accepted) << error;
        if (!entry.accepted) {
            EXPECT_EQ("Configured backhaul BSS authentication cannot be mapped for DPP "
                      "mapBackhaulSta (expected PSK, SAE, or PSK+SAE)",
                      error);
            EXPECT_TRUE(selected.akm.empty());
            EXPECT_TRUE(selected.passphrase.empty());
            EXPECT_TRUE(selected.psk_hex.empty());
            continue;
        }
        ASSERT_TRUE(accepted) << error;
        EXPECT_TRUE(error.empty());
        EXPECT_EQ("Backhaul", selected.ssid);
        EXPECT_EQ(entry.expected, selected.akm);
        EXPECT_EQ((selected.akm == "psk" || selected.akm == "sae" || selected.akm == "psk+sae")
                      ? "test-passphrase"
                      : "",
                  selected.passphrase);
        EXPECT_TRUE(selected.psk_hex.empty());
    }
}

// PUBLIC_INTERFACE
/** Unmapped backhaul auth retains the existing keyless and explicitly requested-AKM fallbacks. */
TEST_F(DppDatabaseTest, UnmappedBackhaulAuthenticationFallbacks)
{
    auto agent      = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    const auto ruid = tlvf::mac_from_string("02:00:00:00:01:01");
    ASSERT_NE(nullptr, agent->radios.add(ruid));
    for (auto auth : {WSC::eWscAuth::WSC_AUTH_RSN, WSC::eWscAuth::WSC_AUTH_INVALID}) {
        for (bool requested : {false, true}) {
            SCOPED_TRACE(static_cast<uint16_t>(auth));
            SCOPED_TRACE(requested);
            database->clear_configured_bss_info(ruid);
            son::wireless_utils::sBssInfoConf bss{};
            bss.ssid                = "Backhaul";
            bss.backhaul            = true;
            bss.authentication_type = auth;
            bss.additional_auth     = son::wireless_utils::eAdditionalAuth::NONE;
            bss.network_key         = requested ? "test-passphrase" : "";
            database->add_configured_bss_info(ruid, bss);
            dpp::DppBackhaulStaConfiguration selected;
            ASSERT_TRUE(dpp::get_dpp_backhaul_sta_configuration(
                *database, agent, "", requested ? "sae" : "", selected, error))
                << error;
            EXPECT_TRUE(error.empty());
            EXPECT_EQ(requested ? "sae" : "dpp", selected.akm);
            EXPECT_EQ(requested ? "test-passphrase" : "", selected.passphrase);
        }
    }
}

// PUBLIC_INTERFACE
/** The INVALID sentinel cannot yield PSK+SAE credentials on the fronthaul path either. */
TEST_F(DppDatabaseTest, RejectsInvalidFronthaulAuthenticationWithPassphrase)
{
    auto agent = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    son::wireless_utils::sBssInfoConf bss{};
    bss.ssid                = "Fronthaul";
    bss.fronthaul           = true;
    bss.authentication_type = WSC::eWscAuth::WSC_AUTH_INVALID;
    bss.network_key         = "test-passphrase";
    database->add_bss_info_configuration(bss);
    dpp::DppStaConfiguration selected;
    EXPECT_FALSE(dpp::get_dpp_sta_configuration(*database, agent, "", selected, error));
    EXPECT_EQ("Configured fronthaul BSS authentication cannot be mapped for DPP sta "
              "(expected PSK, SAE, or PSK+SAE)",
              error);
    EXPECT_TRUE(selected.akm.empty());
    EXPECT_TRUE(selected.passphrase.empty());
    EXPECT_TRUE(selected.psk_hex.empty());
}

// PUBLIC_INTERFACE
/** Regression: transition bitmasks must retain PSK+SAE and the passphrase on both policy paths. */
TEST_F(DppDatabaseTest, PskSaeBitmaskMappingRegression)
{
    using Auth      = WSC::eWscAuth;
    const auto ruid = tlvf::mac_from_string("02:00:00:00:01:01");
    auto agent      = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    ASSERT_NE(nullptr, agent->radios.add(ruid));
    for (auto psk : {Auth::WSC_AUTH_WPA2PSK, Auth::WSC_AUTH_WPAPSK}) {
        for (auto sae : {Auth::WSC_AUTH_SAE, Auth::WSC_AUTH_SAE_AKM24}) {
            const auto combined =
                static_cast<Auth>(static_cast<uint16_t>(psk) | static_cast<uint16_t>(sae));
            SCOPED_TRACE(static_cast<uint16_t>(combined));
            database->clear_configured_bss_info(ruid);
            son::wireless_utils::sBssInfoConf bss{};
            bss.ssid                = "Transition";
            bss.backhaul            = true;
            bss.fronthaul           = true;
            bss.authentication_type = combined;
            bss.network_key         = "test-passphrase";
            database->add_configured_bss_info(ruid, bss);

            // Empty requested AKM prevents the backhaul path from masking a broken mapping.
            dpp::DppBackhaulStaConfiguration backhaul;
            ASSERT_TRUE(
                dpp::get_dpp_backhaul_sta_configuration(*database, agent, "", "", backhaul, error))
                << error;
            EXPECT_EQ("psk+sae", backhaul.akm);
            EXPECT_EQ("test-passphrase", backhaul.passphrase);
            EXPECT_TRUE(backhaul.psk_hex.empty());
            dpp::DppStaConfiguration sta;
            ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "", sta, error)) << error;
            EXPECT_EQ("psk+sae", sta.akm);
            EXPECT_EQ("test-passphrase", sta.passphrase);
            EXPECT_TRUE(sta.psk_hex.empty());
        }
    }
}

// PUBLIC_INTERFACE
/** Radio policy takes precedence over per-agent policy, which takes precedence over global policy. */
TEST_F(DppDatabaseTest, PolicySelectionPrecedenceAndFronthaulSsidMatch)
{
    auto agent      = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    const auto ruid = tlvf::mac_from_string("02:00:00:00:01:01");
    ASSERT_NE(nullptr, agent->radios.add(ruid));
    son::wireless_utils::sBssInfoConf bss{};
    bss.fronthaul = bss.backhaul = true;
    bss.authentication_type      = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
    bss.network_key              = "global-passphrase";
    bss.ssid                     = "Global";
    database->add_bss_info_configuration(bss);
    bss.ssid        = "Agent";
    bss.network_key = "agent-passphrase";
    database->add_bss_info_configuration(agent->al_mac, bss);
    bss.ssid        = "Radio";
    bss.network_key = "radio-passphrase";
    database->add_configured_bss_info(ruid, bss);
    dpp::DppStaConfiguration sta;
    dpp::DppBackhaulStaConfiguration backhaul;
    ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "", sta, error));
    EXPECT_EQ("Radio", sta.ssid);
    EXPECT_EQ("radio-passphrase", sta.passphrase);
    ASSERT_TRUE(dpp::get_dpp_backhaul_sta_configuration(*database, agent, "", "", backhaul, error));
    EXPECT_EQ("Radio", backhaul.ssid);
    database->clear_configured_bss_info(ruid);
    ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "", sta, error));
    EXPECT_EQ("Agent", sta.ssid);
    ASSERT_TRUE(dpp::get_dpp_backhaul_sta_configuration(*database, agent, "", "", backhaul, error));
    EXPECT_EQ("Agent", backhaul.ssid);
    // A requested SSID later in the same candidate list wins over the first fallback.
    bss.ssid = "Wanted";
    database->add_bss_info_configuration(agent->al_mac, bss);
    ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "57616e746564", sta, error));
    EXPECT_EQ("Wanted", sta.ssid);
    ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "416273656e74", sta, error));
    EXPECT_EQ("Agent", sta.ssid);
}

// PUBLIC_INTERFACE
/** Backhaul selection skips fronthaul-only and mismatched SSIDs and prefers a named candidate. */
TEST_F(DppDatabaseTest, BackhaulSsidFilteringAndEmptySsidFallback)
{
    auto agent = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    son::wireless_utils::sBssInfoConf bss{};
    bss.fronthaul           = true;
    bss.ssid                = "WrongRole";
    bss.authentication_type = WSC::eWscAuth::WSC_AUTH_SAE;
    database->add_bss_info_configuration(bss);
    bss.fronthaul = false;
    bss.backhaul  = true;
    bss.ssid      = "Mismatch";
    database->add_bss_info_configuration(bss);
    bss.ssid.clear();
    database->add_bss_info_configuration(bss);
    bss.ssid = "Wanted";
    database->add_bss_info_configuration(bss);
    dpp::DppBackhaulStaConfiguration selected;
    ASSERT_TRUE(dpp::get_dpp_backhaul_sta_configuration(*database, agent, "57616e746564", "sae",
                                                        selected, error));
    EXPECT_EQ("Wanted", selected.ssid);
    database->get_bss_info_configuration().pop_back();
    ASSERT_TRUE(dpp::get_dpp_backhaul_sta_configuration(*database, agent, "57616e746564", "sae",
                                                        selected, error));
    EXPECT_EQ("Wanted", selected.ssid); // Empty candidate takes its SSID from the request.
    EXPECT_EQ("sae", selected.akm);
    EXPECT_TRUE(selected.ssid_advertisement_valid);
}

// PUBLIC_INTERFACE
/** Hex PSKs require exactly 64 hexadecimal characters and a PSK-capable authentication type. */
TEST_F(DppDatabaseTest, PolicyCredentialFormatsAndHiddenSsid)
{
    auto agent      = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    const auto ruid = tlvf::mac_from_string("02:00:00:00:01:01");
    ASSERT_NE(nullptr, agent->radios.add(ruid));
    for (auto auth : {WSC::eWscAuth::WSC_AUTH_WPA2PSK, WSC::eWscAuth::WSC_AUTH_SAE}) {
        for (const auto &key : {std::string(""), std::string(64, 'A'), std::string(64, 'g'),
                                std::string(63, 'a'), std::string("passphrase")}) {
            for (bool hidden : {false, true}) {
                SCOPED_TRACE(static_cast<uint16_t>(auth));
                SCOPED_TRACE(key);
                SCOPED_TRACE(hidden);
                database->clear_configured_bss_info(ruid);
                son::wireless_utils::sBssInfoConf bss{};
                bss.ssid      = "Credentials";
                bss.fronthaul = bss.backhaul = true;
                bss.authentication_type      = auth;
                bss.network_key              = key;
                bss.hidden_ssid              = hidden ? WSC::eWscVendorExtHiddenSsid::ENABLED
                                         : WSC::eWscVendorExtHiddenSsid::DISABLED;
                database->add_configured_bss_info(ruid, bss);
                dpp::DppStaConfiguration sta;
                dpp::DppBackhaulStaConfiguration backhaul;
                ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "", sta, error));
                ASSERT_TRUE(dpp::get_dpp_backhaul_sta_configuration(*database, agent, "", "",
                                                                    backhaul, error));
                const bool hex_psk =
                    auth == WSC::eWscAuth::WSC_AUTH_WPA2PSK && key == std::string(64, 'A');
                EXPECT_EQ(hex_psk ? key : "", sta.psk_hex);
                EXPECT_EQ(hex_psk ? "" : key, sta.passphrase);
                EXPECT_EQ(sta.psk_hex, backhaul.psk_hex);
                EXPECT_EQ(sta.passphrase, backhaul.passphrase);
                EXPECT_TRUE(sta.ssid_advertisement_valid);
                EXPECT_TRUE(backhaul.ssid_advertisement_valid);
                EXPECT_EQ(!hidden, sta.ssid_advertisement);
                EXPECT_EQ(!hidden, backhaul.ssid_advertisement);
                EXPECT_TRUE(error.empty());
            }
        }
    }
}

// PUBLIC_INTERFACE
/** AKM token compatibility is case-insensitive, ignores empty tokens and requires overlap. */
TEST_F(DppDatabaseTest, BackhaulAkmCompatibilityMatrix)
{
    auto agent = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    son::wireless_utils::sBssInfoConf bss{};
    bss.ssid                = "Backhaul";
    bss.backhaul            = true;
    bss.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
    bss.network_key         = "passphrase";
    database->add_bss_info_configuration(bss);
    const std::vector<std::pair<std::string, bool>> cases{
        {"", true},     {"PSK", true},  {"dpp+PSK", true}, {"+psk++sae", true},
        {"sae", false}, {"dpp", false}, {"++", false}};
    for (const auto &entry : cases) {
        SCOPED_TRACE(entry.first);
        dpp::DppBackhaulStaConfiguration selected;
        EXPECT_EQ(entry.second, dpp::get_dpp_backhaul_sta_configuration(
                                    *database, agent, "", entry.first, selected, error));
        if (entry.second) {
            EXPECT_EQ("psk", selected.akm);
            EXPECT_EQ("passphrase", selected.passphrase);
            EXPECT_TRUE(error.empty());
        } else {
            EXPECT_EQ("Configured backhaul AKM is not compatible with DPP Configuration Request",
                      error);
            EXPECT_TRUE(selected.passphrase.empty());
            EXPECT_TRUE(selected.psk_hex.empty());
        }
    }
}

// PUBLIC_INTERFACE
/** STA never uses backhaul-only credentials; empty SSIDs and open authentication are rejected. */
TEST_F(DppDatabaseTest, FronthaulRoleSsidAndAuthenticationGuards)
{
    auto agent = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    son::wireless_utils::sBssInfoConf bss{};
    bss.backhaul            = true;
    bss.ssid                = "BackhaulOnly";
    bss.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
    database->add_bss_info_configuration(bss);
    dpp::DppStaConfiguration selected;
    EXPECT_FALSE(dpp::get_dpp_sta_configuration(*database, agent, "", selected, error));
    EXPECT_EQ("No configured fronthaul BSS available for DPP sta object", error);
    auto &candidate     = database->get_bss_info_configuration().front();
    candidate.fronthaul = true;
    candidate.ssid.clear();
    EXPECT_FALSE(dpp::get_dpp_sta_configuration(*database, agent, "", selected, error));
    EXPECT_EQ("Configured fronthaul BSS has empty SSID for DPP sta object", error);
    ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "54657374", selected, error));
    EXPECT_EQ("Test", selected.ssid);
    candidate.ssid                = "Fronthaul";
    candidate.authentication_type = WSC::eWscAuth::WSC_AUTH_OPEN;
    EXPECT_FALSE(dpp::get_dpp_sta_configuration(*database, agent, "", selected, error));
    EXPECT_EQ("Configured fronthaul BSS uses open authentication; DPP sta object requires "
              "DPP, PSK, SAE, or PSK+SAE credentials",
              error);
    candidate.authentication_type = WSC::eWscAuth::WSC_AUTH_INVALID;
    candidate.network_key.clear();
    ASSERT_TRUE(dpp::get_dpp_sta_configuration(*database, agent, "", selected, error));
    EXPECT_EQ("dpp", selected.akm);
    EXPECT_TRUE(selected.passphrase.empty());
}

// PUBLIC_INTERFACE
/** Invalid requests reset policy outputs and fail before any production keystore access. */
TEST_F(DppDatabaseTest, PolicyRequestValidationBeforeKeystore)
{
    const std::vector<std::pair<std::string, std::string>> cases{
        {"{", "Failed parsing DPP Configuration Request object"},
        {"{}", "DPP Configuration Request contained no netRole"},
        {"", "Empty DPP Configuration Request"}};
    dpp::DppConfiguratorSession session;
    for (const auto &entry : cases) {
        std::vector<std::string> objects{"stale"};
        bool send = true;
        EXPECT_FALSE(dpp::build_dpp_configuration_objects_from_policy(
            *database, session, tlvf::mac_from_string("02:00:00:00:00:02"), entry.first, objects,
            send, error));
        EXPECT_EQ(entry.second, error);
        EXPECT_TRUE(objects.empty());
        EXPECT_FALSE(send);
    }
    auto agent = std::make_shared<Agent>(tlvf::mac_from_string("02:00:00:00:00:02"));
    dpp::DppBackhaulStaConfiguration backhaul;
    backhaul.passphrase = "stale";
    EXPECT_FALSE(
        dpp::get_dpp_backhaul_sta_configuration(*database, agent, "0", "", backhaul, error));
    EXPECT_EQ("Invalid DPP onboarding SSID hex", error);
    EXPECT_TRUE(backhaul.passphrase.empty());
    EXPECT_FALSE(
        dpp::get_dpp_backhaul_sta_configuration(*database, agent, "", "", backhaul, error));
    EXPECT_EQ("No configured backhaul BSS available for DPP mapBackhaulSta object", error);
}
} // namespace
