/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include "../../controller_dpp_session.h"
#include "../../dpp/dpp_gas.h"
#include "../../dpp/dpp_internal.h"

#include <bcl/beerocks_string_utils.h>
#include <gtest/gtest.h>
#include <mapf/common/encryption.h>
#include <openssl/obj_mac.h>
#include <openssl/x509.h>

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

namespace {
namespace dpp = son::controller_dpp;

// PUBLIC_INTERFACE
/** Decode a literal known-answer vector, failing visibly on an invalid test vector. */
std::vector<uint8_t> hex_bytes(const std::string &hex)
{
    std::vector<uint8_t> result;
    for (size_t i = 0; i < hex.size(); i += 2) {
        result.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return result;
}

// PUBLIC_INTERFACE
/** Attribute headers use little-endian IDs and lengths, including zero-length values. */
TEST(DppAttributes, EncodesLittleEndianAndEmptyAttribute)
{
    std::vector<uint8_t> attrs;
    const uint8_t value[] = {0x00, 0xff};
    dpp::append_dpp_attr(attrs, 0x1234, value, sizeof(value));
    dpp::append_dpp_attr(attrs, 0x5678, nullptr, 0);
    EXPECT_EQ((std::vector<uint8_t>{0x34, 0x12, 2, 0, 0, 0xff, 0x78, 0x56, 0, 0}), attrs);
    dpp::DppAttributeView attr;
    ASSERT_TRUE(dpp::get_dpp_attr(attrs.data(), attrs.size(), 0x5678, attr));
    EXPECT_EQ(0U, attr.len);
    EXPECT_EQ(attrs.data() + 6, attr.header);
}

// PUBLIC_INTERFACE
/** Truncated headers and values never produce a stale attribute view. */
TEST(DppAttributes, RejectsTruncationAndClearsView)
{
    const std::vector<uint8_t> attrs{0x04, 0x10, 2, 0, 0xaa, 0xbb};
    for (size_t length = 0; length < attrs.size(); ++length) {
        SCOPED_TRACE(length);
        dpp::DppAttributeView attr;
        attr.data   = attrs.data();
        attr.header = attrs.data();
        attr.len    = 99;
        EXPECT_FALSE(dpp::get_dpp_attr(attrs.data(), length, 0x1004, attr));
        EXPECT_EQ(nullptr, attr.data);
        EXPECT_EQ(nullptr, attr.header);
        EXPECT_EQ(0U, attr.len);
    }
}

// PUBLIC_INTERFACE
/** Lookup returns the first matching attribute and enforces required lengths. */
TEST(DppAttributes, FirstMatchAndRequiredLength)
{
    std::vector<uint8_t> attrs;
    dpp::append_dpp_attr_u8(attrs, dpp::k_dpp_attr_status, 3);
    dpp::append_dpp_attr_u8(attrs, dpp::k_dpp_attr_status, 4);
    dpp::DppAttributeView attr;
    ASSERT_TRUE(
        dpp::parse_required_attr(attrs.data(), attrs.size(), dpp::k_dpp_attr_status, 1, attr));
    EXPECT_EQ(3, attr.data[0]);
    EXPECT_FALSE(
        dpp::parse_required_attr(attrs.data(), attrs.size(), dpp::k_dpp_attr_status, 2, attr));
    std::string value = "stale";
    EXPECT_FALSE(dpp::extract_dpp_attribute(attrs.data(), attrs.size(), 0xffff, value));
    EXPECT_TRUE(value.empty());
}

// PUBLIC_INTERFACE
/** Both supported header layouts are accepted; OUI, suite and frame type are checked. */
TEST(DppAttributes, SplitsFullAndStrippedPublicActionHeaders)
{
    std::vector<uint8_t> frame{0xff};
    dpp::build_dpp_public_action_prefix(dpp::k_dpp_authentication_response, frame);
    ASSERT_EQ(8U, frame.size());
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    for (size_t skip : {size_t(0), size_t(2)}) {
        const std::vector<uint8_t> input(frame.begin() + skip, frame.end());
        const uint8_t *header = nullptr, *attrs = nullptr;
        size_t length = 0;
        ASSERT_TRUE(dpp::split_dpp_public_action_frame(input, 1, header, attrs, length));
        EXPECT_EQ(input.data() + (skip == 0 ? 2 : 0), header);
        EXPECT_EQ(input.data() + 8 - skip, attrs);
        EXPECT_EQ(5U, length);
    }
    for (size_t offset : {size_t(2), size_t(5), size_t(6), size_t(7)}) {
        auto invalid = frame;
        invalid[offset] ^= 0x80;
        const uint8_t *header = nullptr, *attrs = nullptr;
        size_t length = 0;
        EXPECT_FALSE(dpp::split_dpp_public_action_frame(invalid, 1, header, attrs, length));
    }
}

// PUBLIC_INTERFACE
/** AES-SIV matches RFC 5297 Appendix A.1 independently of a round-trip oracle. */
TEST(DppAesSiv, Rfc5297DeterministicKnownAnswer)
{
    const auto key = hex_bytes("fffefdfcfbfaf9f8f7f6f5f4f3f2f1f0f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const auto ad  = hex_bytes("101112131415161718191a1b1c1d1e1f2021222324252627");
    const auto plain    = hex_bytes("112233445566778899aabbccddee");
    const auto expected = hex_bytes("85632d07c6e8f37f950acd320a2ecc9340c02b9690c4dc04daef7f6afe5c");
    std::vector<uint8_t> wrapped, decoded;
    ASSERT_TRUE(dpp::aes_siv_encrypt_vector(key, plain, {ad}, wrapped));
    EXPECT_EQ(expected, wrapped);
    ASSERT_TRUE(dpp::aes_siv_decrypt_vector(key, expected.data(), expected.size(), {ad}, decoded));
    EXPECT_EQ(plain, decoded);
}

// PUBLIC_INTERFACE
/** All supported key sizes work across block boundaries and reject tampering. */
TEST(DppAesSiv, KeySizesBlockBoundariesAndTampering)
{
    for (size_t key_length : {size_t(32), size_t(48), size_t(64)}) {
        const std::vector<uint8_t> key(key_length, 0x42);
        for (size_t length : {size_t(1), size_t(15), size_t(16), size_t(17), size_t(64)}) {
            SCOPED_TRACE(std::to_string(key_length) + "/" + std::to_string(length));
            const std::vector<uint8_t> plain(length, 0x37);
            const std::vector<std::vector<uint8_t>> ad{{1, 2, 3}, {4, 5}};
            std::vector<uint8_t> wrapped, decoded;
            ASSERT_TRUE(dpp::aes_siv_encrypt_vector(key, plain, ad, wrapped));
            ASSERT_EQ(length + 16, wrapped.size());
            ASSERT_TRUE(
                dpp::aes_siv_decrypt_vector(key, wrapped.data(), wrapped.size(), ad, decoded));
            EXPECT_EQ(plain, decoded);
            for (size_t offset : {size_t(0), wrapped.size() - 1}) {
                auto corrupt = wrapped;
                corrupt[offset] ^= 1;
                EXPECT_FALSE(
                    dpp::aes_siv_decrypt_vector(key, corrupt.data(), corrupt.size(), ad, decoded));
            }
            auto wrong_key = key;
            wrong_key.back() ^= 1;
            EXPECT_FALSE(dpp::aes_siv_decrypt_vector(wrong_key, wrapped.data(), wrapped.size(), ad,
                                                     decoded));
            EXPECT_FALSE(dpp::aes_siv_decrypt_vector(key, wrapped.data(), wrapped.size(),
                                                     {{1, 2, 4}, {4, 5}}, decoded));
        }
    }
}

// PUBLIC_INTERFACE
/** Regression: valid empty plaintext must decrypt from its 16-byte SIV tag. */
TEST(DppAesSiv, EmptyPlaintextRoundTripRegression)
{
    const std::vector<std::vector<std::vector<uint8_t>>> ad_cases{{}, {{1, 2, 3}, {4, 5}}};
    for (size_t key_length : {size_t(32), size_t(48), size_t(64)}) {
        const std::vector<uint8_t> key(key_length, 0x42);
        for (const auto &ad : ad_cases) {
            SCOPED_TRACE(std::to_string(key_length) + "/" + std::to_string(ad.size()));
            std::vector<uint8_t> wrapped, decoded{0xff};
            ASSERT_TRUE(dpp::aes_siv_encrypt_vector(key, {}, ad, wrapped));
            ASSERT_EQ(16U, wrapped.size());
            ASSERT_TRUE(
                dpp::aes_siv_decrypt_vector(key, wrapped.data(), wrapped.size(), ad, decoded));
            EXPECT_TRUE(decoded.empty());
            for (size_t offset = 0; offset < wrapped.size(); ++offset) {
                auto corrupt = wrapped;
                corrupt[offset] ^= 1;
                EXPECT_FALSE(
                    dpp::aes_siv_decrypt_vector(key, corrupt.data(), corrupt.size(), ad, decoded));
                EXPECT_TRUE(decoded.empty());
            }
            auto wrong_key = key;
            wrong_key[0] ^= 1; // Change the S2V half: empty messages have no CTR payload.
            EXPECT_FALSE(dpp::aes_siv_decrypt_vector(wrong_key, wrapped.data(), wrapped.size(), ad,
                                                     decoded));
            auto wrong_ad = ad;
            wrong_ad.push_back({0xff});
            EXPECT_FALSE(dpp::aes_siv_decrypt_vector(key, wrapped.data(), wrapped.size(), wrong_ad,
                                                     decoded));
        }
    }
}

// PUBLIC_INTERFACE
/** Unsupported key sizes and short ciphertext are rejected. */
TEST(DppAesSiv, RejectsInvalidSizes)
{
    std::vector<uint8_t> out;
    for (size_t length : {size_t(0), size_t(16), size_t(31), size_t(33)}) {
        EXPECT_FALSE(dpp::aes_siv_encrypt_vector(std::vector<uint8_t>(length), {1}, {}, out));
    }
    const std::vector<uint8_t> key(32), wrapped(15);
    EXPECT_FALSE(dpp::aes_siv_decrypt_vector(key, wrapped.data(), wrapped.size(), {}, out));
}

// PUBLIC_INTERFACE
/** Curve-specific coordinates survive public-key reconstruction and symmetric ECDH. */
TEST(DppCrypto, SupportedCurvesAndEcdh)
{
    for (int nid : {NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1}) {
        SCOPED_TRACE(nid);
        dpp::DppKeyPtr a, b, public_a;
        ASSERT_TRUE(dpp::generate_ec_key(nid, a));
        ASSERT_TRUE(dpp::generate_ec_key(nid, b));
        int actual_nid   = 0;
        const EVP_MD *md = nullptr;
        size_t hash = 0, nonce = 0, coord = 0;
        std::string error;
        ASSERT_TRUE(dpp::curve_info_from_key(a.get(), actual_nid, md, hash, nonce, coord, error));
        EXPECT_EQ(nid, actual_nid);
        EXPECT_EQ(nid == NID_X9_62_prime256v1 ? 32U : nid == NID_secp384r1 ? 48U : 64U, hash);
        EXPECT_EQ(hash / 2, nonce);
        std::vector<uint8_t> xy, secret_a, secret_b, reconstructed;
        ASSERT_TRUE(dpp::ec_public_xy(a.get(), coord, xy));
        ASSERT_EQ(2 * coord, xy.size());
        ASSERT_TRUE(dpp::ec_public_from_xy(nid, xy.data(), xy.size(), public_a));
        ASSERT_TRUE(dpp::ec_public_xy(public_a.get(), coord, reconstructed));
        EXPECT_EQ(xy, reconstructed);
        ASSERT_TRUE(dpp::ecdh_secret(a.get(), b.get(), coord, secret_a));
        ASSERT_TRUE(dpp::ecdh_secret(b.get(), public_a.get(), coord, secret_b));
        EXPECT_EQ(secret_a, secret_b);
        EXPECT_EQ(coord, secret_a.size());
        EXPECT_FALSE(dpp::ec_public_from_xy(nid, xy.data(), xy.size() - 1, public_a));
    }
}

// PUBLIC_INTERFACE
/** GAS request extraction handles optional action byte and ignores trailing encapsulation bytes. */
TEST(DppGas, ExtractsDeclaredQueryWithAndWithoutAction)
{
    for (bool action : {false, true}) {
        std::vector<uint8_t> input;
        if (action) {
            input.push_back(0x0a);
        }
        // Token, Advertisement Protocol IE, LE16 query length, query, trailing byte.
        input.insert(input.end(), {0x42, 0x6c, 8, 0x7f, 0xdd, 5, 0x50, 0x6f, 0x9a, 0x1a, 1, 3, 0, 1,
                                   2, 3, 0xff});
        std::vector<uint8_t> attrs;
        uint8_t token = 0;
        ASSERT_TRUE(dpp::extract_dpp_query_from_gas_encap(input, attrs, &token));
        EXPECT_EQ(0x42, token);
        EXPECT_EQ((std::vector<uint8_t>{1, 2, 3}), attrs);
    }
    std::vector<uint8_t> attrs;
    EXPECT_FALSE(dpp::extract_dpp_query_from_gas_encap({}, attrs));
    EXPECT_FALSE(dpp::extract_dpp_query_from_gas_encap({0x0a}, attrs));
}

// PUBLIC_INTERFACE
/** GAS response framing preserves binary payload and little-endian query length. */
TEST(DppGas, EncodesResponseHeaderAndPayload)
{
    const std::vector<uint8_t> attrs(258, 0xab);
    const auto result = dpp::wrap_dpp_response_in_gas(attrs, 0x42);
    const std::vector<uint8_t> header{0x0b, 0x42, 0,    0,    0,    0,    0x6c, 8, 0x7f,
                                      0xdd, 5,    0x50, 0x6f, 0x9a, 0x1a, 1,    2, 1};
    ASSERT_EQ(header.size() + attrs.size(), result.size());
    EXPECT_TRUE(std::equal(header.begin(), header.end(), result.begin()));
    EXPECT_TRUE(std::equal(attrs.begin(), attrs.end(), result.begin() + header.size()));
}

// PUBLIC_INTERFACE
/** URL-safe base64 is unpadded and binary-safe; invalid input clears output. */
TEST(DppUtilities, Base64UrlAndHex)
{
    const std::vector<uint8_t> bytes{0xfb, 0xff, 0, 0x80};
    const auto encoded = dpp::base64url_encode(bytes.data(), bytes.size());
    EXPECT_EQ(std::string::npos, encoded.find_first_of("+/="));
    std::string decoded;
    ASSERT_TRUE(beerocks::string_utils::base64_url_decode(encoded, decoded));
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()), decoded);
    EXPECT_FALSE(beerocks::string_utils::base64_url_decode("!invalid", decoded));
    EXPECT_TRUE(decoded.empty());
    EXPECT_TRUE(dpp::is_hex_string("aB00"));
    EXPECT_FALSE(dpp::is_hex_string(""));
    EXPECT_FALSE(dpp::is_hex_string("abc"));
    EXPECT_FALSE(dpp::is_hex_string("gg"));
    EXPECT_EQ("fbff0080", dpp::bytes_to_hex_string(bytes.data(), bytes.size()));
}

// PUBLIC_INTERFACE
/** The mapf entry point supports nonempty plaintext without any associated data. */
TEST(DppAesSiv, RoundTripWithoutAssociatedData)
{
    const std::vector<uint8_t> key(32, 0x42);
    const std::vector<uint8_t> plain{'d', 'p', 'p', 0, 0xff};
    std::vector<uint8_t> wrapped, recovered, repeated;
    ASSERT_TRUE(mapf::encryption::aes_siv_encrypt(key.data(), key.size(), plain.data(),
                                                  plain.size(), nullptr, nullptr, 0, wrapped));
    ASSERT_EQ(plain.size() + 16, wrapped.size());
    ASSERT_TRUE(mapf::encryption::aes_siv_decrypt(key.data(), key.size(), wrapped.data(),
                                                  wrapped.size(), nullptr, nullptr, 0, recovered));
    EXPECT_EQ(plain, recovered);
    ASSERT_TRUE(mapf::encryption::aes_siv_encrypt(key.data(), key.size(), plain.data(),
                                                  plain.size(), nullptr, nullptr, 0, repeated));
    EXPECT_EQ(wrapped, repeated); // SIV is deterministic for the same key, AD and plaintext.
}

// PUBLIC_INTERFACE
/** The public-action finder returns the exact attribute offset in both wire layouts. */
TEST(DppPublicAction, FindsFullAndStrippedHeaders)
{
    const std::vector<uint8_t> full{0x04, 0x09, 0x50, 0x6f, 0x9a, 0x1a, 0x01, 0x0b, 0xaa, 0xbb};
    for (size_t skip : {size_t(0), size_t(2)}) {
        SCOPED_TRACE(skip);
        uint8_t type  = 0xff;
        size_t offset = 99;
        ASSERT_TRUE(dpp::find_dpp_public_action_attributes(full.data() + skip, full.size() - skip,
                                                           type, offset));
        EXPECT_EQ(0x0b, type);
        EXPECT_EQ(8U - skip, offset);
        EXPECT_EQ(0xaa, full[skip + offset]);

        // An empty attribute section is valid as long as the header is complete.
        ASSERT_TRUE(
            dpp::find_dpp_public_action_attributes(full.data() + skip, 8U - skip, type, offset));
        EXPECT_EQ(8U - skip, offset);
    }
}

// PUBLIC_INTERFACE
/** Null, truncated and non-DPP inputs reset finder outputs instead of retaining stale values. */
TEST(DppPublicAction, RejectsInvalidHeadersAndClearsOutputs)
{
    const std::vector<uint8_t> full{0x04, 0x09, 0x50, 0x6f, 0x9a, 0x1a, 0x01, 0x01};
    for (size_t skip : {size_t(0), size_t(2)}) {
        for (size_t length = 0; length < full.size() - skip; ++length) {
            SCOPED_TRACE(std::to_string(skip) + "/" + std::to_string(length));
            uint8_t type  = 0xff;
            size_t offset = 99;
            EXPECT_FALSE(
                dpp::find_dpp_public_action_attributes(full.data() + skip, length, type, offset));
            EXPECT_EQ(0, type);
            EXPECT_EQ(0U, offset);
        }
        for (size_t index = 2; index <= 5; ++index) {
            auto invalid = full;
            invalid[index] ^= 0x80;
            uint8_t type  = 0xff;
            size_t offset = 99;
            EXPECT_FALSE(dpp::find_dpp_public_action_attributes(
                invalid.data() + skip, invalid.size() - skip, type, offset));
            EXPECT_EQ(0, type);
            EXPECT_EQ(0U, offset);
        }
    }
    uint8_t type  = 0xff;
    size_t offset = 99;
    EXPECT_FALSE(dpp::find_dpp_public_action_attributes(nullptr, full.size(), type, offset));
    EXPECT_EQ(0, type);
    EXPECT_EQ(0U, offset);
}

// PUBLIC_INTERFACE
/** Agent-style GAS payloads without a query length preserve all attribute bytes. */
TEST(DppGas, ExtractsAgentStyleFallbackWithAndWithoutAction)
{
    // First two attribute bytes imply a length larger than the remaining buffer.
    const std::vector<uint8_t> attrs{0x04, 0x10, 0x02, 0x00, 0xaa, 0xbb};
    for (bool action : {false, true}) {
        std::vector<uint8_t> encap;
        if (action) {
            encap.push_back(0x0a);
        }
        encap.insert(encap.end(), attrs.begin(), attrs.end());
        std::vector<uint8_t> output{0xff};
        ASSERT_TRUE(dpp::extract_dpp_query_from_gas_encap(encap, output));
        EXPECT_EQ(attrs, output);
    }
}

// PUBLIC_INTERFACE
/** Empty response payloads still have a complete GAS response header and a zero length. */
TEST(DppGas, WrapsEmptyResponse)
{
    const std::vector<uint8_t> expected{0x0b, 0, 0,    0,    0,    0,    0x6c, 8, 0x7f,
                                        0xdd, 5, 0x50, 0x6f, 0x9a, 0x1a, 1,    0, 0};
    EXPECT_EQ(expected, dpp::wrap_dpp_response_in_gas({}));
}

// PUBLIC_INTERFACE
/** Standard and URL-safe alphabets decode to independently specified bytes, with optional padding. */
TEST(DppUtilities, Base64KnownAnswersAndPadding)
{
    const std::string binary("\xfb\xef\xff", 3);
    for (const auto &encoded : {std::string("--__"), std::string("++//")}) {
        std::string decoded = "stale";
        ASSERT_TRUE(beerocks::string_utils::base64_url_decode(encoded, decoded));
        EXPECT_EQ(binary, decoded);
    }
    for (const auto &encoded : {std::string("YQ"), std::string("YQ==")}) {
        std::string decoded = "stale";
        ASSERT_TRUE(beerocks::string_utils::base64_url_decode(encoded, decoded));
        EXPECT_EQ("a", decoded);
    }
    const uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
    EXPECT_EQ("aGVsbG8", dpp::base64url_encode(hello, sizeof(hello)));
    std::string decoded = "stale";
    ASSERT_TRUE(beerocks::string_utils::base64_url_decode("aGVsbG8", decoded));
    EXPECT_EQ("hello", decoded);
    // A late invalid character must also discard bytes already decoded.
    EXPECT_FALSE(beerocks::string_utils::base64_url_decode("aGVs!G8", decoded));
    EXPECT_TRUE(decoded.empty());
}

// PUBLIC_INTERFACE
/** RFC3339 parsing uses a fixed UTC timestamp and resets the epoch on empty or invalid input. */
TEST(DppUtilities, ParsesRfc3339Utc)
{
    std::time_t epoch = 99;
    ASSERT_TRUE(dpp::parse_rfc3339_utc("2020-01-01T00:00:00Z", epoch));
    EXPECT_EQ(1577836800U, epoch);
    ASSERT_TRUE(dpp::parse_rfc3339_utc("", epoch));
    EXPECT_EQ(0U, epoch);
    epoch = 99;
    EXPECT_FALSE(dpp::parse_rfc3339_utc("not-a-date", epoch));
    EXPECT_EQ(0U, epoch);
}

// PUBLIC_INTERFACE
/** Compact JWS splitting preserves each component and rejects absent or empty components. */
TEST(DppUtilities, SplitsJwsAndRejectsMissingComponents)
{
    std::string header, payload, signature, error;
    ASSERT_TRUE(dpp::split_jws("aaa.bbb.ccc", header, payload, signature, error));
    EXPECT_EQ("aaa", header);
    EXPECT_EQ("bbb", payload);
    EXPECT_EQ("ccc", signature);
    for (const auto &input : {std::string("aaa"), std::string("aaa.bbb"), std::string("aaa..ccc"),
                              std::string(".bbb.ccc"), std::string("aaa.bbb.")}) {
        SCOPED_TRACE(input);
        header = payload = signature = "stale";
        error.clear();
        EXPECT_FALSE(dpp::split_jws(input, header, payload, signature, error));
        EXPECT_FALSE(error.empty());
        // Failure after splitting may retain parsed components, but never stale caller data.
        EXPECT_NE("stale", header);
        EXPECT_NE("stale", payload);
        EXPECT_NE("stale", signature);
    }
}

// PUBLIC_INTERFACE
/** A padded standard-base64 public key starts a session and produces an Authentication Request. */
TEST(DppSessionStart, AcceptsPaddedStandardBase64BootstrapKey)
{
    const std::string key =
        "MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgADsfoRJwgG60DFEEBdykMrDQIMlcDYlI0EyRn7ie8Zi54=";
    dpp::DppConfiguratorSession session;
    std::vector<uint8_t> frame{0xff};
    std::string error = "stale";
    ASSERT_TRUE(session.start(key, 2, frame, error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(session.authentication_started());
    EXPECT_FALSE(session.authentication_success());
    uint8_t type  = 0xff;
    size_t offset = 0;
    ASSERT_TRUE(dpp::find_dpp_public_action_attributes(frame.data(), frame.size(), type, offset));
    EXPECT_EQ(dpp::k_dpp_authentication_request, type);
    EXPECT_EQ(8U, offset);
    ASSERT_GT(frame.size(), offset);
    dpp::DppAttributeView version;
    ASSERT_TRUE(dpp::parse_required_attr(frame.data() + offset, frame.size() - offset,
                                         dpp::k_dpp_attr_protocol_version, 1, version));
    EXPECT_EQ(2, version.data[0]);
    session.reset();
    EXPECT_FALSE(session.authentication_started());
    EXPECT_FALSE(session.authentication_success());
}

// PUBLIC_INTERFACE
/** Every session field starts cleared, including the direct-encapsulation selector. */
TEST(DppSessionState, DefaultsAreClear)
{
    dpp::SessionState state;
    EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, state.proxy_agent);
    EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, state.last_chirp_enrollee);
    EXPECT_FALSE(state.last_chirp_enrollee_valid);
    EXPECT_FALSE(state.authentication_confirm_sent);
    EXPECT_FALSE(state.configuration_request_received);
    EXPECT_TRUE(state.configuration_request_json.empty());
    EXPECT_TRUE(state.requested_net_role.empty());
    EXPECT_FALSE(state.configuration_result_received);
    EXPECT_EQ(0, state.configuration_result_status);
    EXPECT_FALSE(state.connection_status_result_received);
    EXPECT_EQ(0, state.connection_status_result);
    EXPECT_TRUE(state.pending_configuration_objects.empty());
    EXPECT_FALSE(state.pending_send_conn_status);
    EXPECT_FALSE(state.configuration_response_sent);
    EXPECT_FALSE(state.use_direct_encap);
}

// PUBLIC_INTERFACE
/** Direct and proxied sessions retain independent configuration-request fields. */
TEST(DppSessionState, StoresRequestFieldsWithoutChangingOtherSessions)
{
    dpp::SessionState direct, proxied;
    direct.configuration_request_received = true;
    direct.configuration_request_json     = R"({"netRole":"mapAgent"})";
    direct.requested_net_role             = "mapAgent";
    direct.use_direct_encap               = true;
    EXPECT_TRUE(direct.configuration_request_received);
    EXPECT_EQ(R"({"netRole":"mapAgent"})", direct.configuration_request_json);
    EXPECT_EQ("mapAgent", direct.requested_net_role);
    EXPECT_TRUE(direct.use_direct_encap);
    EXPECT_FALSE(proxied.configuration_request_received);
    EXPECT_TRUE(proxied.configuration_request_json.empty());
    EXPECT_TRUE(proxied.requested_net_role.empty());
    EXPECT_FALSE(proxied.use_direct_encap);
}

// PUBLIC_INTERFACE
/** RFC 4231 case 1 and RFC 5869 case 1 verify HMAC and one-block HKDF independently. */
TEST(DppCrypto, HmacAndHkdfKnownAnswers)
{
    const std::vector<uint8_t> key(20, 0x0b);
    const std::string data = "Hi There";
    std::vector<uint8_t> digest;
    ASSERT_TRUE(dpp::hmac_digest(EVP_sha256(), key.data(), key.size(),
                                 reinterpret_cast<const uint8_t *>(data.data()), data.size(),
                                 digest));
    EXPECT_EQ(hex_bytes("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
              digest);
    const auto prk = hex_bytes("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
    const char info[] = "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9";
    ASSERT_TRUE(dpp::hkdf_expand_one_block(EVP_sha256(), prk, info, 32, digest));
    EXPECT_EQ(hex_bytes("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"),
              digest);
    ASSERT_TRUE(dpp::hash_vector(EVP_sha256(), {{}, {'a'}, {}, {'b', 'c'}}, digest));
    EXPECT_EQ(hex_bytes("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
              digest);
    ASSERT_TRUE(dpp::hash_vector(EVP_sha256(), {}, digest));
    EXPECT_EQ(hex_bytes("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
              digest);
}

// PUBLIC_INTERFACE
/** Each HMAC/HKDF/derivation precondition is varied independently and clears stale output. */
TEST(DppCrypto, DigestAndDerivationParameterGuards)
{
    const std::vector<uint8_t> bytes(32, 0x42);
    std::vector<uint8_t> out{0xff}, bk, ke;
    EXPECT_FALSE(dpp::hmac_digest(nullptr, bytes.data(), bytes.size(), nullptr, 0, out));
    EXPECT_TRUE(out.empty());
    out = {0xff};
    EXPECT_FALSE(dpp::hmac_digest(EVP_sha256(), nullptr, 0, nullptr, 0, out));
    EXPECT_TRUE(out.empty());
    out = {0xff};
    EXPECT_FALSE(dpp::hmac_digest(EVP_sha256(), bytes.data(), bytes.size(), nullptr, 1, out));
    EXPECT_TRUE(out.empty());
    ASSERT_TRUE(dpp::hmac_digest(EVP_sha256(), bytes.data(), bytes.size(), nullptr, 0, out));
    EXPECT_EQ(32U, out.size());
    for (size_t variant = 0; variant < 5; ++variant) {
        SCOPED_TRACE(variant);
        out = {0xff};
        EXPECT_FALSE(dpp::hkdf_expand_one_block(
            variant == 0 ? nullptr : EVP_sha256(), variant == 1 ? std::vector<uint8_t>{} : bytes,
            variant == 2 ? nullptr : "info", variant == 3 ? 0 : variant == 4 ? 33 : 32, out));
        EXPECT_TRUE(out.empty());
    }
    for (size_t variant = 0; variant < 3; ++variant) {
        out = {0xff};
        EXPECT_FALSE(dpp::derive_intermediate_key(variant == 0 ? nullptr : EVP_sha256(),
                                                  variant == 1 ? std::vector<uint8_t>{} : bytes,
                                                  "info", variant == 2 ? 0 : 32, out));
        EXPECT_TRUE(out.empty());
    }
    const std::vector<uint8_t> empty;
    for (size_t variant = 0; variant < 6; ++variant) {
        SCOPED_TRACE(variant);
        bk = ke = {0xff};
        EXPECT_FALSE(dpp::derive_bk_ke(variant == 0 ? nullptr : EVP_sha256(),
                                       variant == 1 ? empty : bytes, variant == 2 ? empty : bytes,
                                       variant == 3 ? empty : bytes, variant == 4 ? empty : bytes,
                                       variant == 5 ? 0 : 32, bk, ke));
        EXPECT_TRUE(bk.empty());
        EXPECT_TRUE(ke.empty());
    }
    out = {0xff};
    EXPECT_FALSE(dpp::hash_vector(nullptr, {bytes}, out));
    EXPECT_TRUE(out.empty());
}

// PUBLIC_INTERFACE
/** ECDH's requested coordinate size left-pads or keeps the least-significant secret octets. */
TEST(DppCrypto, EcdhPaddingTruncationAndKeyGuards)
{
    dpp::DppKeyPtr a, b;
    ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, a));
    ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, b));
    std::vector<uint8_t> full, padded, truncated;
    ASSERT_TRUE(dpp::ecdh_secret(a.get(), b.get(), 32, full));
    ASSERT_TRUE(dpp::ecdh_secret(a.get(), b.get(), 33, padded));
    auto expected = full;
    expected.insert(expected.begin(), 0);
    EXPECT_EQ(expected, padded);
    ASSERT_TRUE(dpp::ecdh_secret(a.get(), b.get(), 31, truncated));
    EXPECT_EQ(std::vector<uint8_t>(full.begin() + 1, full.end()), truncated);
    for (size_t variant = 0; variant < 3; ++variant) {
        padded = {0xff};
        EXPECT_FALSE(dpp::ecdh_secret(variant == 0 ? nullptr : a.get(),
                                      variant == 1 ? nullptr : b.get(), variant == 2 ? 0 : 32,
                                      padded));
        EXPECT_TRUE(padded.empty());
    }
    padded = {0xff};
    EXPECT_FALSE(dpp::ec_public_xy(nullptr, 32, padded));
    EXPECT_TRUE(padded.empty());
    EXPECT_FALSE(dpp::ec_public_xy(a.get(), 0, padded));
    EXPECT_FALSE(dpp::ec_public_x(nullptr, 32, padded));
    dpp::DppKeyPtr result;
    EXPECT_FALSE(dpp::ec_public_from_xy(NID_X9_62_prime256v1, nullptr, 64, result));
    const std::vector<uint8_t> xy(64, 0);
    EXPECT_FALSE(dpp::ec_public_from_xy(NID_X9_62_prime256v1, xy.data(), 0, result));
    EXPECT_FALSE(dpp::ec_public_from_xy(NID_X9_62_prime256v1, xy.data(), 63, result));
    EXPECT_FALSE(dpp::ec_public_from_xy(NID_undef, xy.data(), xy.size(), result));
    EXPECT_FALSE(dpp::generate_ec_key(NID_undef, result));
    EXPECT_EQ(nullptr, result.get());
}

// PUBLIC_INTERFACE
/** DER parsing requires the entire input and releases the previous output on failure. */
TEST(DppCrypto, PublicDerRejectsTruncationAndTrailingBytes)
{
    dpp::DppKeyPtr key, parsed;
    ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, key));
    const int length = i2d_PUBKEY(key.get(), nullptr);
    ASSERT_GT(length, 0);
    std::vector<uint8_t> der(static_cast<size_t>(length));
    auto *ptr = der.data();
    ASSERT_EQ(length, i2d_PUBKEY(key.get(), &ptr));
    ASSERT_TRUE(dpp::parse_ec_pubkey_der(der.data(), der.size(), parsed));
    EXPECT_FALSE(dpp::parse_ec_pubkey_der(nullptr, der.size(), parsed));
    EXPECT_EQ(nullptr, parsed.get());
    EXPECT_FALSE(dpp::parse_ec_pubkey_der(der.data(), 0, parsed));
    EXPECT_FALSE(dpp::parse_ec_pubkey_der(der.data(), der.size() - 1, parsed));
    der.push_back(0);
    EXPECT_FALSE(dpp::parse_ec_pubkey_der(der.data(), der.size(), parsed));
    EXPECT_EQ(nullptr, parsed.get());
    int nid          = 99;
    const EVP_MD *md = EVP_sha256();
    size_t hash = 99, nonce = 99, coord = 99;
    std::string error;
    EXPECT_FALSE(dpp::curve_info_from_key(nullptr, nid, md, hash, nonce, coord, error));
    EXPECT_EQ("Missing EC key", error);
    EXPECT_EQ(NID_undef, nid);
    EXPECT_EQ(nullptr, md);
    EXPECT_EQ(0U, hash);
    EXPECT_EQ(0U, nonce);
    EXPECT_EQ(0U, coord);
    std::string alg;
    EXPECT_FALSE(dpp::ec_key_info(nullptr, alg, md, coord));
}

// PUBLIC_INTERFACE
/** Connector extraction handles action-only framing, binary attributes and malformed lengths. */
TEST(DppAttributes, ConnectorExtractionAndActionOnlyHeader)
{
    std::vector<uint8_t> frame;
    dpp::build_dpp_public_action_prefix(dpp::k_dpp_configuration_result, frame);
    const auto header_only = frame;
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    const std::string expected("a\0b", 3);
    dpp::append_dpp_attr(frame, 0x100d, reinterpret_cast<const uint8_t *>(expected.data()),
                         expected.size());
    for (size_t skip : {size_t(0), size_t(1), size_t(2)}) {
        const std::vector<uint8_t> input(frame.begin() + skip, frame.end());
        uint8_t type = 99;
        std::string connector;
        ASSERT_TRUE(dpp::extract_dpp_connector_from_frame(input, type, connector));
        EXPECT_EQ(dpp::k_dpp_configuration_result, type);
        EXPECT_EQ(expected, connector);
        const uint8_t *header = nullptr, *attrs = nullptr;
        size_t length = 0;
        ASSERT_TRUE(dpp::split_dpp_public_action_frame(input, type, header, attrs, length));
        EXPECT_EQ(input.data() + 2 - skip, header);
    }
    uint8_t type          = 99;
    std::string connector = "stale";
    EXPECT_FALSE(dpp::extract_dpp_connector_from_frame({}, type, connector));
    EXPECT_TRUE(connector.empty());
    EXPECT_FALSE(dpp::extract_dpp_connector_from_frame(header_only, type, connector));
    frame.pop_back();
    EXPECT_FALSE(dpp::extract_dpp_connector_from_frame(frame, type, connector));
    EXPECT_TRUE(connector.empty());
    const uint8_t invalid[] = {0x0d, 0x10, 2, 0, 1};
    EXPECT_FALSE(dpp::extract_dpp_attribute(invalid, sizeof(invalid), 0x100d, connector));
    EXPECT_FALSE(dpp::extract_dpp_attribute(nullptr, 4, 0x100d, connector));
    EXPECT_FALSE(dpp::extract_dpp_attribute(invalid, 3, 0x100d, connector));
    dpp::DppAttributeView view;
    EXPECT_FALSE(dpp::get_dpp_attr(nullptr, 4, 0x100d, view));
}

// PUBLIC_INTERFACE
/** Empty encodings and pre-epoch dates take deterministic utility guard paths. */
TEST(DppUtilities, EmptyEncodingAndPreEpochDate)
{
    const uint8_t byte = 0;
    EXPECT_TRUE(dpp::base64url_encode(nullptr, 1).empty());
    EXPECT_TRUE(dpp::base64url_encode(&byte, 0).empty());
    EXPECT_TRUE(dpp::bytes_to_hex_string(&byte, 0).empty());
    std::time_t epoch = 99;
    EXPECT_FALSE(dpp::parse_rfc3339_utc("1969-12-31T23:59:59Z", epoch));
    EXPECT_EQ(0U, epoch);
}

// PUBLIC_INTERFACE
/** Unsupported bootstrap curves reset an existing session and allow a subsequent valid start. */
TEST(DppSessionStart, UnsupportedCurveResetsSessionAndCanRecover)
{
    dpp::DppConfiguratorSession session;
    std::string error;
    std::vector<uint8_t> frame;
    for (int nid : {NID_X9_62_prime256v1, NID_secp224r1, NID_X9_62_prime256v1}) {
        SCOPED_TRACE(nid);
        dpp::DppKeyPtr key;
        ASSERT_TRUE(dpp::generate_ec_key(nid, key));
        const int length = i2d_PUBKEY(key.get(), nullptr);
        ASSERT_GT(length, 0);
        std::vector<uint8_t> der(static_cast<size_t>(length));
        auto *ptr = der.data();
        ASSERT_EQ(length, i2d_PUBKEY(key.get(), &ptr));
        frame                = {0xff};
        error                = "stale";
        const bool supported = nid == NID_X9_62_prime256v1;
        EXPECT_EQ(supported,
                  session.start(dpp::base64url_encode(der.data(), der.size()), 2, frame, error));
        EXPECT_EQ(supported, session.authentication_started());
        EXPECT_FALSE(session.authentication_success());
        if (supported) {
            EXPECT_FALSE(frame.empty());
            EXPECT_TRUE(error.empty());
        } else {
            EXPECT_TRUE(frame.empty());
            EXPECT_EQ("Unsupported DPP bootstrap curve", error);
        }
    }
}

// PUBLIC_INTERFACE
/** Valid non-EC DER reaches key-type guards rather than failing at ASN.1 decoding. */
TEST(DppCrypto, RejectsRsaKeysAtEcInterfaces)
{
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    ASSERT_NE(nullptr, context.get());
    ASSERT_EQ(1, EVP_PKEY_keygen_init(context.get()));
    // The default RSA size is sufficient: this key is never used to sign or encrypt.
    EVP_PKEY *raw = nullptr;
    ASSERT_EQ(1, EVP_PKEY_keygen(context.get(), &raw));
    dpp::DppKeyPtr rsa(raw);
    const int length = i2d_PUBKEY(rsa.get(), nullptr);
    ASSERT_GT(length, 0);
    std::vector<uint8_t> der(static_cast<size_t>(length));
    auto *ptr = der.data();
    ASSERT_EQ(length, i2d_PUBKEY(rsa.get(), &ptr));
    const auto hex = dpp::bytes_to_hex_string(der.data(), der.size());
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> parsed(nullptr, EVP_PKEY_free);
    EXPECT_FALSE(dpp::parse_ec_pubkey_hex(hex, parsed));
    EXPECT_EQ(nullptr, parsed.get());
    std::string kid = "stale", error, alg;
    EXPECT_FALSE(dpp::compute_csign_kid(hex, kid));
    EXPECT_TRUE(kid.empty());
    const EVP_MD *md = nullptr;
    size_t hash = 0, nonce = 0, coord = 0;
    int nid = NID_undef;
    EXPECT_FALSE(dpp::curve_info_from_key(rsa.get(), nid, md, hash, nonce, coord, error));
    EXPECT_FALSE(error.empty()); // OpenSSL 1.1 and 3 report different key-type errors.
    EXPECT_EQ(NID_undef, nid);
    EXPECT_EQ(nullptr, md);
    EXPECT_EQ(0U, hash);
    EXPECT_EQ(0U, nonce);
    EXPECT_EQ(0U, coord);
    EXPECT_FALSE(dpp::ec_key_info(rsa.get(), alg, md, coord));
    std::vector<uint8_t> xy{0xff};
    EXPECT_FALSE(dpp::ec_public_xy(rsa.get(), 32, xy));
    EXPECT_TRUE(xy.empty());
}

// PUBLIC_INTERFACE
/** Valid coordinate length does not make an off-curve point valid, and failure clears the key. */
TEST(DppCrypto, RejectsOffCurvePointAndClearsPreviousKey)
{
    dpp::DppKeyPtr key;
    ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, key));
    const std::vector<uint8_t> zero_point(64, 0); // (0, 0) is not on P-256.
    EXPECT_FALSE(
        dpp::ec_public_from_xy(NID_X9_62_prime256v1, zero_point.data(), zero_point.size(), key));
    EXPECT_EQ(nullptr, key.get());
}

// PUBLIC_INTERFACE
/** The standard P-256 generator cannot fit into a one-octet coordinate buffer. */
TEST(DppCrypto, RejectsUndersizedCoordinateOutput)
{
    const auto generator =
        hex_bytes("6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
                  "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");
    dpp::DppKeyPtr key;
    ASSERT_TRUE(
        dpp::ec_public_from_xy(NID_X9_62_prime256v1, generator.data(), generator.size(), key));
    std::vector<uint8_t> xy{0xff};
    EXPECT_FALSE(dpp::ec_public_xy(key.get(), 1, xy));
    EXPECT_TRUE(xy.empty());
}

// PUBLIC_INTERFACE
/** ECDH refuses different groups and a public-only local key without returning stale secrets. */
TEST(DppCrypto, RejectsCrossCurveAndPublicOnlyEcdh)
{
    dpp::DppKeyPtr local, peer, other_curve, public_only;
    ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, local));
    ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, peer));
    ASSERT_TRUE(dpp::generate_ec_key(NID_secp384r1, other_curve));
    std::vector<uint8_t> secret{0xff}, xy;
    EXPECT_FALSE(dpp::ecdh_secret(local.get(), other_curve.get(), 32, secret));
    EXPECT_TRUE(secret.empty());
    ASSERT_TRUE(dpp::ec_public_xy(local.get(), 32, xy));
    ASSERT_TRUE(dpp::ec_public_from_xy(NID_X9_62_prime256v1, xy.data(), xy.size(), public_only));
    secret = {0xff};
    EXPECT_FALSE(dpp::ecdh_secret(public_only.get(), peer.get(), 32, secret));
    EXPECT_TRUE(secret.empty());
}

// PUBLIC_INTERFACE
/** Digest/output-size mismatches fail after preconditions, including partial BK derivation. */
TEST(DppCrypto, DigestLengthFailurePropagation)
{
    const std::vector<uint8_t> bytes(64, 0x42);
    std::vector<uint8_t> output{0xff}, bk, ke;
    EXPECT_FALSE(dpp::hmac_digest(EVP_md_null(), bytes.data(), bytes.size(), bytes.data(),
                                  bytes.size(), output));
    EXPECT_TRUE(output.empty());
    // The requested size fits the PRK but exceeds the SHA-256 digest.
    output = {0xff};
    EXPECT_FALSE(dpp::hkdf_expand_one_block(EVP_sha256(), bytes, "info", 33, output));
    EXPECT_TRUE(output.empty());
    EXPECT_FALSE(dpp::derive_intermediate_key(EVP_sha256(), bytes, "info", 33, output));
    EXPECT_TRUE(output.empty());
    EXPECT_FALSE(dpp::derive_intermediate_key(EVP_sha256(), bytes, nullptr, 32, output));
    EXPECT_TRUE(output.empty());
    bk = ke = {0xff};
    EXPECT_FALSE(dpp::derive_bk_ke(EVP_md_null(), bytes, bytes, bytes, bytes, 32, bk, ke));
    EXPECT_TRUE(bk.empty());
    EXPECT_TRUE(ke.empty());
    EXPECT_FALSE(dpp::derive_bk_ke(EVP_sha256(), bytes, bytes, bytes, bytes, 33, bk, ke));
    // BK extraction succeeds before KE expansion fails; do not assert transactional clearing.
    EXPECT_EQ(32U, bk.size());
    EXPECT_TRUE(ke.empty());
}

// PUBLIC_INTERFACE
/** Empty AD elements are authenticated as elements, not silently dropped from the vector. */
TEST(DppAesSiv, PreservesEmptyAssociatedDataElements)
{
    for (size_t size : {size_t(32), size_t(48), size_t(64)}) {
        SCOPED_TRACE(size);
        const std::vector<uint8_t> key(size, 0x42), plain{1, 2, 3};
        const std::vector<std::vector<uint8_t>> ad{{}, {0x10, 0x20}, {}};
        std::vector<uint8_t> wrapped, decoded, without_empty;
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(key, plain, ad, wrapped));
        ASSERT_TRUE(dpp::aes_siv_decrypt_vector(key, wrapped.data(), wrapped.size(), ad, decoded));
        EXPECT_EQ(plain, decoded);
        ASSERT_TRUE(dpp::aes_siv_encrypt_vector(key, plain, {{0x10, 0x20}}, without_empty));
        EXPECT_NE(wrapped, without_empty);
        EXPECT_FALSE(dpp::aes_siv_decrypt_vector(key, wrapped.data(), wrapped.size(),
                                                 {{0x10, 0x20}}, decoded));
        EXPECT_TRUE(decoded.empty());
    }
}
} // namespace
