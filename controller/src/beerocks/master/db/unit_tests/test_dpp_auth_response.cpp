/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include "../../controller_dpp_protocol.h"
#include "../../dpp/dpp_internal.h"

#include <gtest/gtest.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <string>
#include <vector>

namespace {
namespace dpp = son::controller_dpp;

// PUBLIC_INTERFACE
/** Exercises response validation through the public configurator-session API. */
class DppAuthResponseTest : public ::testing::Test {
protected:
    /** Start an independent P-256 session for each test. */
    void SetUp() override
    {
        ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, bootstrap_key));
        const int len = i2d_PUBKEY(bootstrap_key.get(), nullptr);
        ASSERT_GT(len, 0);
        std::vector<uint8_t> der(static_cast<size_t>(len));
        auto *out = der.data();
        ASSERT_EQ(len, i2d_PUBKEY(bootstrap_key.get(), &out));
        bootstrap_hash.resize(SHA256_DIGEST_LENGTH);
        ASSERT_NE(nullptr, SHA256(der.data(), der.size(), bootstrap_hash.data()));
        ASSERT_TRUE(dpp::ec_public_xy(bootstrap_key.get(), 32, protocol_xy));
        std::vector<uint8_t> request;
        std::string error;
        ASSERT_TRUE(session.start(dpp::base64url_encode(der.data(), der.size()), 2, request, error))
            << error;
        const uint8_t *header = nullptr, *attrs = nullptr;
        size_t attrs_len = 0;
        ASSERT_TRUE(dpp::split_dpp_public_action_frame(request, dpp::k_dpp_authentication_request,
                                                       header, attrs, attrs_len));
        dpp::DppAttributeView hash;
        ASSERT_TRUE(dpp::parse_required_attr(attrs, attrs_len, dpp::k_dpp_attr_i_bootstrap_hash,
                                             SHA256_DIGEST_LENGTH, hash));
        initiator_hash.assign(hash.data, hash.data + hash.len);
    }

    /** Construct the unencrypted response prefix. */
    std::vector<uint8_t> response_prefix()
    {
        std::vector<uint8_t> frame;
        dpp::build_dpp_public_action_prefix(dpp::k_dpp_authentication_response, frame);
        return frame;
    }

    /** Append the session's actual responder bootstrap hash. */
    void append_hash(std::vector<uint8_t> &frame)
    {
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_bootstrap_hash, bootstrap_hash.data(),
                             bootstrap_hash.size());
    }

    /** Append an unauthentic payload suitable for pre-decryption guard tests. */
    void append_dummy_wrapped(std::vector<uint8_t> &frame, size_t len = 17)
    {
        const std::vector<uint8_t> wrapped(len, 0);
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_wrapped_data, wrapped.data(), wrapped.size());
    }

    /** Check both the failure reason and unchanged authentication state. */
    void expect_rejection(const std::vector<uint8_t> &frame, const std::string &expected_error)
    {
        std::vector<uint8_t> confirm{0xff};
        std::string error = "stale";
        EXPECT_FALSE(session.handle_authentication_response(frame, confirm, error));
        EXPECT_EQ(expected_error, error);
        EXPECT_TRUE(confirm.empty());
        EXPECT_TRUE(session.authentication_started());
        EXPECT_FALSE(session.authentication_success());
    }

    dpp::DppConfiguratorSession session;
    dpp::DppKeyPtr bootstrap_key;
    std::vector<uint8_t> bootstrap_hash, initiator_hash, protocol_xy;
};

// PUBLIC_INTERFACE
/** Wrong frame types and truncated headers fail before attribute processing. */
TEST_F(DppAuthResponseTest, RejectsInvalidResponseHeader)
{
    auto frame   = response_prefix();
    frame.back() = dpp::k_dpp_authentication_request;
    expect_rejection(frame, "Invalid DPP Authentication Response frame");
    frame = response_prefix();
    frame.resize(7);
    expect_rejection(frame, "Invalid DPP Authentication Response frame");
}

// PUBLIC_INTERFACE
/** Missing, short and truncated Wrapped Data cannot advance authentication. */
TEST_F(DppAuthResponseTest, RejectsMissingOrMalformedWrappedData)
{
    const std::string expected = "DPP Authentication Response missing Wrapped Data";
    expect_rejection(response_prefix(), expected);
    auto frame = response_prefix();
    append_dummy_wrapped(frame, 15);
    expect_rejection(frame, expected);
    frame = response_prefix();
    append_dummy_wrapped(frame);
    frame.pop_back();
    expect_rejection(frame, expected);
}

// PUBLIC_INTERFACE
/** Status must contain one byte and precede Wrapped Data. */
TEST_F(DppAuthResponseTest, RejectsMissingWrongLengthOrLateStatus)
{
    const std::string expected = "DPP Authentication Response missing status";
    auto frame                 = response_prefix();
    append_dummy_wrapped(frame);
    expect_rejection(frame, expected);
    frame                  = response_prefix();
    const uint8_t status[] = {0, 0};
    dpp::append_dpp_attr(frame, dpp::k_dpp_attr_status, status, sizeof(status));
    append_dummy_wrapped(frame);
    expect_rejection(frame, expected);
    frame = response_prefix();
    append_dummy_wrapped(frame);
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    expect_rejection(frame, expected);
}

// PUBLIC_INTERFACE
/** Non-OK peer status cannot produce an authentication confirmation. */
TEST_F(DppAuthResponseTest, RejectsNonOkStatus)
{
    auto frame = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 1);
    append_dummy_wrapped(frame);
    expect_rejection(frame, "DPP Authentication Response returned non-OK status");
}

// PUBLIC_INTERFACE
/** Missing, short and incorrect bootstrap hashes are rejected. */
TEST_F(DppAuthResponseTest, RejectsBootstrapHashMismatch)
{
    const std::string expected = "DPP Authentication Response bootstrap hash mismatch";
    auto frame                 = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    append_dummy_wrapped(frame);
    expect_rejection(frame, expected);
    frame = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_bootstrap_hash, bootstrap_hash.data(),
                         bootstrap_hash.size() - 1);
    append_dummy_wrapped(frame);
    expect_rejection(frame, expected);
    frame = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    auto wrong_hash = bootstrap_hash;
    wrong_hash[0] ^= 1;
    dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_bootstrap_hash, wrong_hash.data(),
                         wrong_hash.size());
    append_dummy_wrapped(frame);
    expect_rejection(frame, expected);
}

// PUBLIC_INTERFACE
/** An echoed initiator identity must match the actual request, not the responder's hash. */
TEST_F(DppAuthResponseTest, RejectsIncorrectInitiatorBootstrapHash)
{
    auto wrong_hash = initiator_hash;
    wrong_hash[0] ^= 1;
    for (const auto &hash : {wrong_hash, bootstrap_hash}) {
        auto frame = response_prefix();
        dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
        append_hash(frame);
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_i_bootstrap_hash, hash.data(), hash.size());
        append_dummy_wrapped(frame);
        expect_rejection(frame, "DPP Authentication Response initiator bootstrap hash mismatch");
    }
}

// PUBLIC_INTERFACE
/** Empty and short optional initiator hashes are not treated as an absent identity. */
TEST_F(DppAuthResponseTest, RejectsWrongLengthInitiatorBootstrapHash)
{
    for (size_t length : {size_t(0), initiator_hash.size() - 1}) {
        auto frame = response_prefix();
        dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
        append_hash(frame);
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_i_bootstrap_hash, initiator_hash.data(),
                             length);
        append_dummy_wrapped(frame);
        expect_rejection(frame, "DPP Authentication Response initiator bootstrap hash mismatch");
    }
}

// PUBLIC_INTERFACE
/** Correct identity alone cannot authenticate a forged encrypted response. */
TEST_F(DppAuthResponseTest, CorrectInitiatorHashStillRequiresAuthenticatedPayload)
{
    auto frame = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    append_hash(frame);
    dpp::append_dpp_attr(frame, dpp::k_dpp_attr_i_bootstrap_hash, initiator_hash.data(),
                         initiator_hash.size());
    dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_protocol_key, protocol_xy.data(),
                         protocol_xy.size());
    append_dummy_wrapped(frame);
    expect_rejection(frame, "Failed unwrapping DPP Authentication Response");
}

// PUBLIC_INTERFACE
/** Responder protocol coordinates must have the curve's exact length. */
TEST_F(DppAuthResponseTest, RejectsMissingOrWrongLengthProtocolKey)
{
    const std::string expected = "Invalid DPP responder protocol key";
    auto frame                 = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    append_hash(frame);
    append_dummy_wrapped(frame);
    expect_rejection(frame, expected);
    frame = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    append_hash(frame);
    dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_protocol_key, protocol_xy.data(),
                         protocol_xy.size() - 1);
    append_dummy_wrapped(frame);
    expect_rejection(frame, expected);
}

// PUBLIC_INTERFACE
/** Valid attribute shapes do not authenticate an unauthentic encrypted response. */
TEST_F(DppAuthResponseTest, RejectsUnauthenticWrappedResponse)
{
    auto frame = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    append_hash(frame);
    dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_protocol_key, protocol_xy.data(),
                         protocol_xy.size());
    append_dummy_wrapped(frame);
    expect_rejection(frame, "Failed unwrapping DPP Authentication Response");
}

// PUBLIC_INTERFACE
/** Optional version lengths and values never bypass authentication of the wrapped response. */
TEST_F(DppAuthResponseTest, OptionalVersionBoundariesStillRequireValidCiphertext)
{
    const std::vector<std::vector<uint8_t>> versions{{}, {0}, {1}, {2}, {3}, {2, 0xff}};
    for (const auto &version : versions) {
        SCOPED_TRACE(::testing::PrintToString(version));
        auto frame = response_prefix();
        dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
        append_hash(frame);
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_protocol_version, version.data(),
                             version.size());
        dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_protocol_key, protocol_xy.data(),
                             protocol_xy.size());
        append_dummy_wrapped(frame);
        expect_rejection(frame, "Failed unwrapping DPP Authentication Response");
    }
}

// PUBLIC_INTERFACE
/** A correctly sized but off-curve responder point fails before wrapped-data decryption. */
TEST_F(DppAuthResponseTest, RejectsOffCurveProtocolKey)
{
    auto frame = response_prefix();
    dpp::append_dpp_attr_u8(frame, dpp::k_dpp_attr_status, 0);
    append_hash(frame);
    const std::vector<uint8_t> off_curve(64, 0);
    dpp::append_dpp_attr(frame, dpp::k_dpp_attr_r_protocol_key, off_curve.data(), off_curve.size());
    append_dummy_wrapped(frame);
    expect_rejection(frame, "Invalid DPP responder protocol key");
}
} // namespace
