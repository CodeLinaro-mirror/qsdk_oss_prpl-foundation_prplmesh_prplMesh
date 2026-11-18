/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "controller_dpp_protocol.h"
#include "dpp/dpp_internal.h"
#include "dpp/dpp_secure_clear.h"

#include <gtest/gtest.h>

#include <openssl/crypto.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cstddef>
#include <new>
#include <string>
#include <vector>

namespace {

using namespace son::controller_dpp;

constexpr int k_curve_nid                 = NID_X9_62_prime256v1;
constexpr size_t k_coord_len              = 32;
constexpr size_t k_hash_len               = 32;
constexpr size_t k_nonce_len              = 16;
constexpr uint8_t k_status_not_compatible = 0x01;

std::vector<uint8_t> view_bytes(const DppAttributeView &view)
{
    return std::vector<uint8_t>(view.data, view.data + view.len);
}

// Allocator that records what is left in a heap block when it is released.
size_t g_deallocations        = 0;
size_t g_non_zero_bytes_freed = 0;

template <class T> struct RecordingAllocator {
    using value_type     = T;
    RecordingAllocator() = default;
    template <class U> RecordingAllocator(const RecordingAllocator<U> &) {}
    T *allocate(std::size_t n) { return static_cast<T *>(::operator new(n * sizeof(T))); }
    void deallocate(T *p, std::size_t n)
    {
        const auto *bytes = reinterpret_cast<const uint8_t *>(p);
        g_non_zero_bytes_freed += static_cast<size_t>(
            std::count_if(bytes, bytes + n * sizeof(T), [](uint8_t b) { return b != 0; }));
        ++g_deallocations;
        ::operator delete(p);
    }
};
template <class T, class U>
bool operator==(const RecordingAllocator<T> &, const RecordingAllocator<U> &)
{
    return true;
}
template <class T, class U>
bool operator!=(const RecordingAllocator<T> &, const RecordingAllocator<U> &)
{
    return false;
}
using RecordedBytes = std::vector<uint8_t, RecordingAllocator<uint8_t>>;

class SecureClearTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        g_deallocations        = 0;
        g_non_zero_bytes_freed = 0;
    }
};

// Characterises the bug: clear() hands the secret back to the heap intact.
TEST_F(SecureClearTest, PlainClearLeavesSecretInFreedMemory)
{
    {
        RecordedBytes secret(32, 0xA5);
        secret.clear();
    }
    EXPECT_EQ(g_deallocations, 1u);
    EXPECT_GE(g_non_zero_bytes_freed, 32u);
}

TEST_F(SecureClearTest, WipesWholeCapacityBeforeRelease)
{
    RecordedBytes secret(48, 0xA5);
    secret.resize(16); // bytes 16..47 stay in the block
    secure_clear(secret);
    EXPECT_TRUE(secret.empty());
    EXPECT_EQ(secret.capacity(), 0u);
    EXPECT_EQ(g_deallocations, 1u);
    EXPECT_EQ(g_non_zero_bytes_freed, 0u);
}

TEST_F(SecureClearTest, EmptyVectorIsANoOp)
{
    RecordedBytes secret;
    secure_clear(secret);
    EXPECT_TRUE(secret.empty());
    EXPECT_EQ(g_deallocations, 0u);
}

TEST(SecureClearGuardTest, ClearsOnScopeExit)
{
    std::vector<uint8_t> secret(32, 0xA5);
    {
        SecureClearGuard guard(secret);
    }
    EXPECT_TRUE(secret.empty());
    EXPECT_EQ(secret.capacity(), 0u);
}

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

// Minimal P-256 DPP Responder/Enrollee (Easy Connect v3.0 6.3 and 6.4),
// built from the same primitives, to drive DppConfiguratorSession.
class TestResponder {
public:
    AssertionResult init(std::string &bootstrap_key_b64)
    {
        if (!generate_ec_key(k_curve_nid, m_bootstrap_key) ||
            !generate_ec_key(k_curve_nid, m_protocol_key)) {
            return AssertionFailure() << "key generation failed";
        }
        unsigned char *der = nullptr;
        const int der_len  = i2d_PUBKEY(m_bootstrap_key.get(), &der);
        if (der_len <= 0 || !der) {
            return AssertionFailure() << "i2d_PUBKEY failed";
        }
        m_bootstrap_hash.resize(SHA256_DIGEST_LENGTH);
        SHA256(der, static_cast<size_t>(der_len), m_bootstrap_hash.data());
        bootstrap_key_b64 = base64url_encode(der, static_cast<size_t>(der_len));
        OPENSSL_free(der);
        return AssertionSuccess();
    }

    AssertionResult process_request(const std::vector<uint8_t> &request)
    {
        const uint8_t *hdr   = nullptr;
        const uint8_t *attrs = nullptr;
        size_t attrs_len     = 0;
        DppAttributeView wrapped;
        if (!split_dpp_public_action_frame(request, k_dpp_authentication_request, hdr, attrs,
                                           attrs_len) ||
            !get_dpp_attr(attrs, attrs_len, k_dpp_attr_wrapped_data, wrapped) || !wrapped.header) {
            return AssertionFailure() << "malformed DPP Authentication Request";
        }
        const size_t before_len = static_cast<size_t>(wrapped.header - attrs);
        DppAttributeView r_hash;
        if (!parse_required_attr(attrs, before_len, k_dpp_attr_r_bootstrap_hash, k_hash_len,
                                 r_hash) ||
            view_bytes(r_hash) != m_bootstrap_hash) {
            return AssertionFailure() << "R-bootstrap hash mismatch";
        }
        DppAttributeView i_key;
        if (!parse_required_attr(attrs, before_len, k_dpp_attr_i_protocol_key, 2 * k_coord_len,
                                 i_key) ||
            !ec_public_from_xy(k_curve_nid, i_key.data, i_key.len, m_peer_protocol_key)) {
            return AssertionFailure() << "invalid I-protocol key";
        }
        std::vector<uint8_t> k1;
        if (!ecdh_secret(m_bootstrap_key.get(), m_peer_protocol_key.get(), k_coord_len, m_mx) ||
            !derive_intermediate_key(EVP_sha256(), m_mx, "first intermediate key", k_hash_len,
                                     k1)) {
            return AssertionFailure() << "k1 derivation failed";
        }
        std::vector<uint8_t> clear;
        if (!aes_siv_decrypt_vector(k1, wrapped.data, wrapped.len,
                                    {std::vector<uint8_t>(hdr, hdr + 6),
                                     std::vector<uint8_t>(attrs, attrs + before_len)},
                                    clear)) {
            return AssertionFailure() << "cannot unwrap DPP Authentication Request";
        }
        DppAttributeView i_nonce;
        if (!parse_required_attr(clear.data(), clear.size(), k_dpp_attr_i_nonce, k_nonce_len,
                                 i_nonce)) {
            return AssertionFailure() << "missing I-nonce";
        }
        m_i_nonce = view_bytes(i_nonce);
        return AssertionSuccess();
    }

    AssertionResult build_response(uint8_t status, std::vector<uint8_t> &frame)
    {
        std::vector<uint8_t> nx;
        std::vector<uint8_t> k2;
        std::vector<uint8_t> bk;
        if (!ecdh_secret(m_protocol_key.get(), m_peer_protocol_key.get(), k_coord_len, nx) ||
            !derive_intermediate_key(EVP_sha256(), nx, "second intermediate key", k_hash_len, k2)) {
            return AssertionFailure() << "k2 derivation failed";
        }
        m_r_nonce.resize(k_nonce_len);
        if (RAND_bytes(m_r_nonce.data(), static_cast<int>(m_r_nonce.size())) != 1 ||
            !derive_bk_ke(EVP_sha256(), m_i_nonce, m_r_nonce, m_mx, nx, k_hash_len, bk, m_ke)) {
            return AssertionFailure() << "ke derivation failed";
        }
        std::vector<uint8_t> pi_x, pr_x, br_x, r_auth;
        if (!public_x(pi_x, pr_x, br_x) ||
            !hash_vector(EVP_sha256(),
                         {m_i_nonce, m_r_nonce, pi_x, pr_x, br_x, std::vector<uint8_t>{0}},
                         r_auth)) {
            return AssertionFailure() << "R-auth computation failed";
        }
        std::vector<uint8_t> secondary_clear;
        std::vector<uint8_t> secondary;
        append_dpp_attr(secondary_clear, k_dpp_attr_r_auth_tag, r_auth.data(), r_auth.size());
        if (!aes_siv_encrypt_vector(m_ke, secondary_clear, {}, secondary)) {
            return AssertionFailure() << "cannot wrap R-auth";
        }
        std::vector<uint8_t> clear;
        append_dpp_attr(clear, k_dpp_attr_r_nonce, m_r_nonce.data(), m_r_nonce.size());
        append_dpp_attr(clear, k_dpp_attr_i_nonce, m_i_nonce.data(), m_i_nonce.size());
        append_dpp_attr_u8(clear, k_dpp_attr_r_capabilities, k_dpp_capab_enrollee);
        append_dpp_attr(clear, k_dpp_attr_wrapped_data, secondary.data(), secondary.size());

        std::vector<uint8_t> pr_xy;
        if (!ec_public_xy(m_protocol_key.get(), k_coord_len, pr_xy)) {
            return AssertionFailure() << "cannot encode R-protocol key";
        }
        build_dpp_public_action_prefix(k_dpp_authentication_response, frame);
        append_dpp_attr_u8(frame, k_dpp_attr_status, status);
        append_dpp_attr(frame, k_dpp_attr_r_bootstrap_hash, m_bootstrap_hash.data(),
                        m_bootstrap_hash.size());
        append_dpp_attr(frame, k_dpp_attr_r_protocol_key, pr_xy.data(), pr_xy.size());
        std::vector<uint8_t> wrapped;
        if (!aes_siv_encrypt_vector(k2, clear,
                                    {std::vector<uint8_t>(frame.begin() + 2, frame.begin() + 8),
                                     std::vector<uint8_t>(frame.begin() + 8, frame.end())},
                                    wrapped)) {
            return AssertionFailure() << "cannot wrap DPP Authentication Response";
        }
        append_dpp_attr(frame, k_dpp_attr_wrapped_data, wrapped.data(), wrapped.size());
        return AssertionSuccess();
    }

    AssertionResult verify_confirm(const std::vector<uint8_t> &confirm)
    {
        const uint8_t *hdr   = nullptr;
        const uint8_t *attrs = nullptr;
        size_t attrs_len     = 0;
        DppAttributeView wrapped;
        DppAttributeView status;
        if (!split_dpp_public_action_frame(confirm, k_dpp_authentication_confirm, hdr, attrs,
                                           attrs_len) ||
            !get_dpp_attr(attrs, attrs_len, k_dpp_attr_wrapped_data, wrapped) || !wrapped.header ||
            !parse_required_attr(attrs, attrs_len, k_dpp_attr_status, 1, status) ||
            status.data[0] != k_dpp_status_ok) {
            return AssertionFailure() << "malformed DPP Authentication Confirm";
        }
        const size_t before_len = static_cast<size_t>(wrapped.header - attrs);
        std::vector<uint8_t> clear;
        if (!aes_siv_decrypt_vector(m_ke, wrapped.data, wrapped.len,
                                    {std::vector<uint8_t>(hdr, hdr + 6),
                                     std::vector<uint8_t>(attrs, attrs + before_len)},
                                    clear)) {
            return AssertionFailure() << "cannot unwrap DPP Authentication Confirm";
        }
        DppAttributeView i_auth;
        std::vector<uint8_t> pi_x, pr_x, br_x, expected;
        if (!parse_required_attr(clear.data(), clear.size(), k_dpp_attr_i_auth_tag, k_hash_len,
                                 i_auth) ||
            !public_x(pi_x, pr_x, br_x) ||
            !hash_vector(EVP_sha256(),
                         {m_r_nonce, m_i_nonce, pr_x, pi_x, br_x, std::vector<uint8_t>{1}},
                         expected) ||
            view_bytes(i_auth) != expected) {
            return AssertionFailure() << "I-auth mismatch";
        }
        return AssertionSuccess();
    }

    // Raw DPP attributes, as carried in a GAS Configuration Request query.
    AssertionResult build_config_request(std::vector<uint8_t> &request)
    {
        static const std::string config_attr =
            R"({"name":"Test Agent","wi-fi_tech":"map","netRole":"mapAgent"})";
        m_e_nonce.resize(k_nonce_len);
        if (RAND_bytes(m_e_nonce.data(), static_cast<int>(m_e_nonce.size())) != 1) {
            return AssertionFailure() << "RAND_bytes failed";
        }
        std::vector<uint8_t> clear;
        append_dpp_attr(clear, k_dpp_attr_enrollee_nonce, m_e_nonce.data(), m_e_nonce.size());
        append_dpp_attr(clear, k_dpp_attr_config_attr_obj,
                        reinterpret_cast<const uint8_t *>(config_attr.data()), config_attr.size());
        std::vector<uint8_t> wrapped;
        if (!aes_siv_encrypt_vector(m_ke, clear, {}, wrapped)) {
            return AssertionFailure() << "cannot wrap DPP Configuration Request";
        }
        request.clear();
        append_dpp_attr(request, k_dpp_attr_wrapped_data, wrapped.data(), wrapped.size());
        return AssertionSuccess();
    }

    AssertionResult verify_config_response(const std::vector<uint8_t> &response)
    {
        DppAttributeView status;
        DppAttributeView wrapped;
        if (!get_dpp_attr(response.data(), response.size(), k_dpp_attr_wrapped_data, wrapped) ||
            !wrapped.header ||
            !parse_required_attr(response.data(), response.size(), k_dpp_attr_status, 1, status) ||
            status.data[0] != k_dpp_status_ok) {
            return AssertionFailure() << "malformed DPP Configuration Response";
        }
        const std::vector<uint8_t> ad(response.data(), wrapped.header);
        std::vector<uint8_t> clear;
        DppAttributeView e_nonce;
        DppAttributeView config_obj;
        if (!aes_siv_decrypt_vector(m_ke, wrapped.data, wrapped.len, {ad}, clear) ||
            !parse_required_attr(clear.data(), clear.size(), k_dpp_attr_enrollee_nonce, k_nonce_len,
                                 e_nonce) ||
            view_bytes(e_nonce) != m_e_nonce ||
            !get_dpp_attr(clear.data(), clear.size(), k_dpp_attr_config_obj, config_obj)) {
            return AssertionFailure() << "cannot verify DPP Configuration Response";
        }
        return AssertionSuccess();
    }

private:
    bool public_x(std::vector<uint8_t> &pi_x, std::vector<uint8_t> &pr_x,
                  std::vector<uint8_t> &br_x) const
    {
        return ec_public_x(m_peer_protocol_key.get(), k_coord_len, pi_x) &&
               ec_public_x(m_protocol_key.get(), k_coord_len, pr_x) &&
               ec_public_x(m_bootstrap_key.get(), k_coord_len, br_x);
    }

    DppKeyPtr m_bootstrap_key;
    DppKeyPtr m_protocol_key;
    DppKeyPtr m_peer_protocol_key;
    std::vector<uint8_t> m_bootstrap_hash;
    std::vector<uint8_t> m_mx;
    std::vector<uint8_t> m_i_nonce;
    std::vector<uint8_t> m_r_nonce;
    std::vector<uint8_t> m_e_nonce;
    std::vector<uint8_t> m_ke;
};

class DppConfiguratorSessionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_responder.init(m_bootstrap_key));
        std::string error;
        ASSERT_TRUE(m_session.start(m_bootstrap_key, k_dpp_default_version, m_auth_request, error))
            << error;
        ASSERT_TRUE(m_responder.process_request(m_auth_request));
    }

    // Valid Authentication Response -> Confirm the Responder accepts.
    void authenticate()
    {
        ASSERT_TRUE(m_responder.build_response(k_dpp_status_ok, m_auth_response));
        std::vector<uint8_t> confirm;
        std::string error;
        ASSERT_TRUE(m_session.handle_authentication_response(m_auth_response, confirm, error))
            << error;
        ASSERT_TRUE(m_responder.verify_confirm(confirm));
    }

    // Authentication Response with a non-OK status, as from an Enrollee
    // that cannot act in the requested role.
    void fail_authentication(std::string &error)
    {
        std::vector<uint8_t> response;
        ASSERT_TRUE(m_responder.build_response(k_status_not_compatible, response));
        std::vector<uint8_t> confirm;
        EXPECT_FALSE(m_session.handle_authentication_response(response, confirm, error));
        EXPECT_TRUE(confirm.empty());
    }

    TestResponder m_responder;
    DppConfiguratorSession m_session;
    std::string m_bootstrap_key;
    std::vector<uint8_t> m_auth_request;
    std::vector<uint8_t> m_auth_response;
};

TEST_F(DppConfiguratorSessionTest, StartHoldsIntermediatesButNoSessionKey)
{
    EXPECT_TRUE(m_session.authentication_started());
    EXPECT_FALSE(m_session.authentication_success());
    EXPECT_TRUE(m_session.has_authentication_intermediates());
    EXPECT_FALSE(m_session.has_session_key());
}

TEST_F(DppConfiguratorSessionTest, ResetErasesIntermediates)
{
    m_session.reset();
    EXPECT_FALSE(m_session.authentication_started());
    EXPECT_FALSE(m_session.has_authentication_intermediates());
    EXPECT_FALSE(m_session.has_session_key());
}

// Easy Connect v3.0 6.3.4: M, N, k1, k2 deleted once authentication completes.
TEST_F(DppConfiguratorSessionTest, SuccessErasesIntermediatesAndKeepsSessionKey)
{
    ASSERT_NO_FATAL_FAILURE(authenticate());
    EXPECT_TRUE(m_session.authentication_success());
    EXPECT_FALSE(m_session.has_authentication_intermediates());
    EXPECT_TRUE(m_session.has_session_key());
}

TEST_F(DppConfiguratorSessionTest, ConfigurationExchangeWorksAfterErase)
{
    ASSERT_NO_FATAL_FAILURE(authenticate());

    std::vector<uint8_t> request;
    ASSERT_TRUE(m_responder.build_config_request(request));
    std::string request_json;
    std::string net_role;
    std::string error;
    ASSERT_TRUE(m_session.unwrap_configuration_request(request, request_json, net_role, error))
        << error;
    EXPECT_EQ(net_role, "mapAgent");

    std::vector<uint8_t> response;
    ASSERT_TRUE(
        m_session.build_configuration_response(std::vector<std::string>{"{}"}, response, error))
        << error;
    EXPECT_TRUE(m_responder.verify_config_response(response));
}

TEST_F(DppConfiguratorSessionTest, NonOkStatusErasesIntermediates)
{
    std::string error;
    ASSERT_NO_FATAL_FAILURE(fail_authentication(error));
    EXPECT_NE(error.find("non-OK"), std::string::npos) << error;
    EXPECT_FALSE(m_session.authentication_success());
    EXPECT_FALSE(m_session.has_authentication_intermediates());
    EXPECT_FALSE(m_session.has_session_key());
}

// Once k1 is gone the session cannot be resumed; callers restart it.
TEST_F(DppConfiguratorSessionTest, ResponseAfterFailedResponseIsRejected)
{
    std::string error;
    ASSERT_NO_FATAL_FAILURE(fail_authentication(error));

    std::vector<uint8_t> response;
    ASSERT_TRUE(m_responder.build_response(k_dpp_status_ok, response));
    std::vector<uint8_t> confirm;
    EXPECT_FALSE(m_session.handle_authentication_response(response, confirm, error));
    EXPECT_NE(error.find("erased"), std::string::npos) << error;
    EXPECT_TRUE(confirm.empty());
    EXPECT_FALSE(m_session.authentication_success());
    EXPECT_FALSE(m_session.has_session_key());
}

TEST_F(DppConfiguratorSessionTest, TamperedResponseErasesIntermediates)
{
    std::vector<uint8_t> response;
    ASSERT_TRUE(m_responder.build_response(k_dpp_status_ok, response));
    response.back() ^= 0x01; // corrupt the AES-SIV ciphertext

    std::vector<uint8_t> confirm;
    std::string error;
    EXPECT_FALSE(m_session.handle_authentication_response(response, confirm, error));
    EXPECT_NE(error.find("unwrapping"), std::string::npos) << error;
    EXPECT_FALSE(m_session.has_authentication_intermediates());
    EXPECT_FALSE(m_session.has_session_key());
}

// hostapd may deliver the Authentication Response twice; the duplicate must
// not disturb the established session key.
TEST_F(DppConfiguratorSessionTest, DuplicateResponseAfterSuccessKeepsSessionKey)
{
    ASSERT_NO_FATAL_FAILURE(authenticate());

    std::vector<uint8_t> confirm;
    std::string error;
    EXPECT_FALSE(m_session.handle_authentication_response(m_auth_response, confirm, error));
    EXPECT_NE(error.find("already complete"), std::string::npos) << error;
    EXPECT_TRUE(confirm.empty());
    EXPECT_TRUE(m_session.authentication_success());
    EXPECT_TRUE(m_session.has_session_key());
}

TEST_F(DppConfiguratorSessionTest, ResetAfterSuccessErasesSessionKey)
{
    ASSERT_NO_FATAL_FAILURE(authenticate());
    std::vector<uint8_t> request;
    ASSERT_TRUE(m_responder.build_config_request(request));

    m_session.reset();
    EXPECT_FALSE(m_session.authentication_success());
    EXPECT_FALSE(m_session.has_session_key());

    std::string request_json;
    std::string net_role;
    std::string error;
    EXPECT_FALSE(m_session.unwrap_configuration_request(request, request_json, net_role, error));
    EXPECT_NE(error.find("without authenticated"), std::string::npos) << error;
}

TEST_F(DppConfiguratorSessionTest, RestartAfterFailureAuthenticates)
{
    std::string error;
    ASSERT_NO_FATAL_FAILURE(fail_authentication(error));

    ASSERT_TRUE(m_session.start(m_bootstrap_key, k_dpp_default_version, m_auth_request, error))
        << error;
    EXPECT_TRUE(m_session.has_authentication_intermediates());
    ASSERT_TRUE(m_responder.process_request(m_auth_request));
    ASSERT_NO_FATAL_FAILURE(authenticate());
    EXPECT_TRUE(m_session.authentication_success());
    EXPECT_FALSE(m_session.has_authentication_intermediates());
}

} // namespace
