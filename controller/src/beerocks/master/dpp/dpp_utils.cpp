/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "controller_dpp_protocol.h"
#include "dpp_internal.h"

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

std::string bytes_to_hex_string(const uint8_t *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[(data[i] >> 4) & 0xF]);
        out.push_back(hex[data[i] & 0xF]);
    }
    return out;
}

bool is_hex_string(const std::string &value)
{
    if (value.empty() || (value.size() % 2) != 0) {
        return false;
    }
    for (const auto ch : value) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

std::string base64url_encode(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return {};
    }

    std::string out;
    out.resize(4 * ((len + 2) / 3));
    int out_len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&out[0]), data, len);
    if (out_len <= 0) {
        return {};
    }
    out.resize(static_cast<size_t>(out_len));

    for (auto &ch : out) {
        if (ch == '+') {
            ch = '-';
        } else if (ch == '/') {
            ch = '_';
        }
    }
    while (!out.empty() && out.back() == '=') {
        out.pop_back();
    }

    return out;
}

bool parse_rfc3339_utc(const std::string &value, uint32_t &epoch)
{
    epoch = 0;
    if (value.empty()) {
        return true;
    }

    std::tm tm{};
    if (!strptime(value.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm)) {
        return false;
    }

    time_t when = timegm(&tm);
    if (when < 0) {
        return false;
    }
    epoch = static_cast<uint32_t>(when);
    return true;
}

bool parse_ec_pubkey_hex(const std::string &hex,
                         std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> &out)
{
    if (!is_hex_string(hex)) {
        return false;
    }

    auto key_bytes           = beerocks::string_utils::hex_to_bytes<std::vector<uint8_t>>(hex);
    const unsigned char *ptr = key_bytes.data();
    EVP_PKEY *pkey           = d2i_PUBKEY(nullptr, &ptr, key_bytes.size());
    if (!pkey || EVP_PKEY_base_id(pkey) != EVP_PKEY_EC) {
        if (pkey) {
            EVP_PKEY_free(pkey);
        }
        return false;
    }
    out.reset(pkey);
    return true;
}

bool derive_pubkey_hex_from_private(const std::string &hex, std::string &out_hex)
{
    out_hex.clear();
    if (!is_hex_string(hex)) {
        return false;
    }

    auto key_bytes           = beerocks::string_utils::hex_to_bytes<std::vector<uint8_t>>(hex);
    const unsigned char *ptr = key_bytes.data();
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(nullptr, EVP_PKEY_free);

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = d2i_ECPrivateKey(nullptr, &ptr, key_bytes.size());
    if (!ec_key) {
        return false;
    }

    if (!EC_KEY_get0_public_key(ec_key)) {
        const EC_GROUP *group = EC_KEY_get0_group(ec_key);
        const BIGNUM *priv    = EC_KEY_get0_private_key(ec_key);
        if (!group || !priv) {
            EC_KEY_free(ec_key);
            return false;
        }

        std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> pub(EC_POINT_new(group), EC_POINT_free);
        if (!pub || EC_POINT_mul(group, pub.get(), priv, nullptr, nullptr, nullptr) != 1 ||
            EC_KEY_set_public_key(ec_key, pub.get()) != 1) {
            EC_KEY_free(ec_key);
            return false;
        }
    }

    pkey.reset(EVP_PKEY_new());
    if (!pkey || EVP_PKEY_set1_EC_KEY(pkey.get(), ec_key) != 1) {
        EC_KEY_free(ec_key);
        return false;
    }
    EC_KEY_free(ec_key);
#else
    EVP_PKEY *raw = d2i_PrivateKey(EVP_PKEY_EC, nullptr, &ptr, key_bytes.size());
    if (!raw) {
        return false;
    }
    pkey.reset(raw);

    size_t pub_len = 0;
    if (EVP_PKEY_get_octet_string_param(pkey.get(), OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0,
                                        &pub_len) != 1 ||
        pub_len == 0) {
        return false;
    }
#endif

    int len = i2d_PUBKEY(pkey.get(), nullptr);
    if (len <= 0) {
        return false;
    }

    std::vector<uint8_t> der(static_cast<size_t>(len));
    unsigned char *ptr_out = der.data();
    if (i2d_PUBKEY(pkey.get(), &ptr_out) != len) {
        return false;
    }

    out_hex = bytes_to_hex_string(der.data(), der.size());
    return !out_hex.empty();
}

bool ec_key_info(EVP_PKEY *key, std::string &alg, const EVP_MD *&md, size_t &sig_part_len)
{
    if (!key) {
        return false;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(key);
    if (!ec_key) {
        return false;
    }
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    if (!group) {
        return false;
    }
    const int nid = EC_GROUP_get_curve_name(group);
    sig_part_len  = (EC_GROUP_get_degree(group) + 7) / 8;
    switch (nid) {
    case NID_X9_62_prime256v1:
        alg = "ES256";
        md  = EVP_sha256();
        return true;
    case NID_secp384r1:
        alg = "ES384";
        md  = EVP_sha384();
        return true;
    case NID_secp521r1:
        alg = "ES512";
        md  = EVP_sha512();
        return true;
    default:
        return false;
    }
#else
    char group_name[64] = {};
    size_t group_len    = 0;
    if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, group_name,
                                       sizeof(group_name), &group_len) != 1 ||
        group_len == 0) {
        group_name[0] = '\0';
    }

    std::string group;
    if (group_len > 0) {
        group.assign(group_name, group_len);
    }

    auto map_by_bits = [&](int bits) -> bool {
        if (bits <= 0) {
            return false;
        }
        sig_part_len = (bits + 7) / 8;
        if (bits == 256) {
            alg = "ES256";
            md  = EVP_sha256();
            return true;
        }
        if (bits == 384) {
            alg = "ES384";
            md  = EVP_sha384();
            return true;
        }
        if (bits == 521) {
            alg = "ES512";
            md  = EVP_sha512();
            return true;
        }
        return false;
    };

    if (group == "prime256v1" || group == "secp256r1") {
        sig_part_len = 32;
        alg          = "ES256";
        md           = EVP_sha256();
        return true;
    }
    if (group == "secp384r1") {
        sig_part_len = 48;
        alg          = "ES384";
        md           = EVP_sha384();
        return true;
    }
    if (group == "secp521r1") {
        sig_part_len = 66;
        alg          = "ES512";
        md           = EVP_sha512();
        return true;
    }

    return map_by_bits(EVP_PKEY_get_bits(key));
#endif
}

bool compute_csign_kid(const std::string &csign_pub_hex, std::string &kid)
{
    kid.clear();

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(nullptr, EVP_PKEY_free);
    if (!parse_ec_pubkey_hex(csign_pub_hex, pkey)) {
        return false;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(pkey.get());
    if (!ec_key) {
        return false;
    }
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    const EC_POINT *point = EC_KEY_get0_public_key(ec_key);
    if (!group || !point) {
        return false;
    }
    size_t pub_len =
        EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, nullptr, 0, nullptr);
    if (pub_len == 0) {
        return false;
    }
    std::vector<uint8_t> pub(pub_len);
    if (EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, pub.data(), pub.size(),
                           nullptr) != pub_len) {
        return false;
    }
#else
    BIGNUM *x_raw = nullptr;
    BIGNUM *y_raw = nullptr;
    if (EVP_PKEY_get_bn_param(pkey.get(), OSSL_PKEY_PARAM_EC_PUB_X, &x_raw) != 1 ||
        EVP_PKEY_get_bn_param(pkey.get(), OSSL_PKEY_PARAM_EC_PUB_Y, &y_raw) != 1) {
        return false;
    }

    std::unique_ptr<BIGNUM, decltype(&BN_free)> x_bn(x_raw, BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> y_bn(y_raw, BN_free);

    const size_t coord_len = (EVP_PKEY_get_bits(pkey.get()) + 7) / 8;
    if (coord_len == 0) {
        return false;
    }

    std::vector<uint8_t> pub(1 + 2 * coord_len);
    pub[0] = 0x04;
    if (BN_bn2binpad(x_bn.get(), pub.data() + 1, coord_len) <= 0 ||
        BN_bn2binpad(y_bn.get(), pub.data() + 1 + coord_len, coord_len) <= 0) {
        return false;
    }
#endif

    uint8_t hash[SHA256_DIGEST_LENGTH] = {};
    SHA256(pub.data(), pub.size(), hash);
    kid = base64url_encode(hash, sizeof(hash));
    return !kid.empty();
}

bool parse_ec_private_key_hex(const std::string &hex, DppKeyPtr &out)
{
    if (!is_hex_string(hex)) {
        return false;
    }

    auto key_bytes           = beerocks::string_utils::hex_to_bytes<std::vector<uint8_t>>(hex);
    const unsigned char *ptr = key_bytes.data();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = d2i_ECPrivateKey(nullptr, &ptr, key_bytes.size());
    if (!ec_key) {
        return false;
    }
    EVP_PKEY *pkey = EVP_PKEY_new();
    if (!pkey || EVP_PKEY_set1_EC_KEY(pkey, ec_key) != 1) {
        EC_KEY_free(ec_key);
        if (pkey) {
            EVP_PKEY_free(pkey);
        }
        return false;
    }
    EC_KEY_free(ec_key);
#else
    EVP_PKEY *pkey = d2i_PrivateKey(EVP_PKEY_EC, nullptr, &ptr, key_bytes.size());
    if (!pkey) {
        return false;
    }
#endif
    out.reset(pkey);
    return true;
}

} // namespace controller_dpp
} // namespace son
