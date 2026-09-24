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

bool curve_info_from_key(EVP_PKEY *key, int &nid, const EVP_MD *&md, size_t &hash_len,
                         size_t &nonce_len, size_t &coord_len, std::string &error)
{
    nid       = NID_undef;
    md        = nullptr;
    hash_len  = 0;
    nonce_len = 0;
    coord_len = 0;
    if (!key) {
        error = "Missing EC key";
        return false;
    }

    auto set_by_nid = [&](int key_nid) -> bool {
        switch (key_nid) {
        case NID_X9_62_prime256v1:
            nid       = NID_X9_62_prime256v1;
            md        = EVP_sha256();
            hash_len  = 32;
            nonce_len = 16;
            coord_len = 32;
            return true;
        case NID_secp384r1:
            nid       = NID_secp384r1;
            md        = EVP_sha384();
            hash_len  = 48;
            nonce_len = 24;
            coord_len = 48;
            return true;
        case NID_secp521r1:
            nid       = NID_secp521r1;
            md        = EVP_sha512();
            hash_len  = 64;
            nonce_len = 32;
            coord_len = 66;
            return true;
        default:
            return false;
        }
    };

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(key);
    if (!ec_key) {
        error = "DPP bootstrap public key is not EC";
        return false;
    }
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    if (!group || !set_by_nid(EC_GROUP_get_curve_name(group))) {
        error = "Unsupported DPP bootstrap curve";
        return false;
    }
#else
    char group_name[64] = {};
    size_t group_len    = 0;
    if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, group_name,
                                       sizeof(group_name), &group_len) == 1 &&
        group_len > 0) {
        std::string group(group_name, group_len);
        if (group == "prime256v1" || group == "secp256r1") {
            return set_by_nid(NID_X9_62_prime256v1);
        }
        if (group == "secp384r1") {
            return set_by_nid(NID_secp384r1);
        }
        if (group == "secp521r1") {
            return set_by_nid(NID_secp521r1);
        }
    }

    const int bits = EVP_PKEY_get_bits(key);
    if (bits == 256) {
        return set_by_nid(NID_X9_62_prime256v1);
    }
    if (bits == 384) {
        return set_by_nid(NID_secp384r1);
    }
    if (bits == 521) {
        return set_by_nid(NID_secp521r1);
    }
    error = "Unsupported DPP bootstrap curve";
    return false;
#endif

    return true;
}

bool parse_ec_pubkey_der(const uint8_t *der, size_t len, DppKeyPtr &out)
{
    out.reset(nullptr);
    if (!der || len == 0) {
        return false;
    }

    const unsigned char *ptr = der;
    EVP_PKEY *key            = d2i_PUBKEY(nullptr, &ptr, len);
    if (!key || ptr != der + len) {
        if (key) {
            EVP_PKEY_free(key);
        }
        return false;
    }
    out.reset(key);
    return true;
}

bool generate_ec_key(int nid, DppKeyPtr &out)
{
    out.reset(nullptr);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec_key(EC_KEY_new_by_curve_name(nid),
                                                           EC_KEY_free);
    if (!ec_key || EC_KEY_generate_key(ec_key.get()) != 1) {
        return false;
    }
    EVP_PKEY *key = EVP_PKEY_new();
    if (!key || EVP_PKEY_set1_EC_KEY(key, ec_key.get()) != 1) {
        if (key) {
            EVP_PKEY_free(key);
        }
        return false;
    }
    out.reset(key);
    return true;
#else
    const char *group_name = OBJ_nid2sn(nid);
    if (!group_name) {
        return false;
    }

    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1) {
        return false;
    }

    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                                 const_cast<char *>(group_name), 0);
    params[1] = OSSL_PARAM_construct_end();
    if (EVP_PKEY_CTX_set_params(ctx.get(), params) != 1) {
        return false;
    }

    EVP_PKEY *key = nullptr;
    if (EVP_PKEY_generate(ctx.get(), &key) != 1) {
        return false;
    }
    out.reset(key);
    return true;
#endif
}

bool ec_public_xy(EVP_PKEY *key, size_t coord_len, std::vector<uint8_t> &xy)
{
    xy.clear();
    if (!key || coord_len == 0) {
        return false;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(key);
    if (!ec_key) {
        return false;
    }
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    const EC_POINT *point = EC_KEY_get0_public_key(ec_key);
    if (!group || !point) {
        return false;
    }
    std::unique_ptr<BIGNUM, decltype(&BN_free)> x(BN_new(), BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> y(BN_new(), BN_free);
    if (!x || !y ||
        EC_POINT_get_affine_coordinates_GFp(group, point, x.get(), y.get(), nullptr) != 1) {
        return false;
    }
#else
    BIGNUM *x_raw = nullptr;
    BIGNUM *y_raw = nullptr;
    if (EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_EC_PUB_X, &x_raw) != 1 ||
        EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_EC_PUB_Y, &y_raw) != 1) {
        return false;
    }
    std::unique_ptr<BIGNUM, decltype(&BN_free)> x(x_raw, BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> y(y_raw, BN_free);
#endif

    xy.assign(2 * coord_len, 0);
    if (BN_bn2binpad(x.get(), xy.data(), coord_len) <= 0 ||
        BN_bn2binpad(y.get(), xy.data() + coord_len, coord_len) <= 0) {
        xy.clear();
        return false;
    }
    return true;
}

bool ec_public_x(EVP_PKEY *key, size_t coord_len, std::vector<uint8_t> &x_only)
{
    std::vector<uint8_t> xy;
    if (!ec_public_xy(key, coord_len, xy) || xy.size() < coord_len) {
        return false;
    }
    x_only.assign(xy.begin(), xy.begin() + coord_len);
    return true;
}

bool ec_public_from_xy(int nid, const uint8_t *xy, size_t xy_len, DppKeyPtr &out)
{
    out.reset(nullptr);
    if (!xy || xy_len == 0 || (xy_len % 2) != 0) {
        return false;
    }

    const size_t coord_len = xy_len / 2;
    std::vector<uint8_t> encoded(1 + xy_len);
    encoded[0] = 0x04;
    std::copy_n(xy, xy_len, encoded.data() + 1);

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec_key(EC_KEY_new_by_curve_name(nid),
                                                           EC_KEY_free);
    if (!ec_key) {
        return false;
    }
    const EC_GROUP *group = EC_KEY_get0_group(ec_key.get());
    std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> point(EC_POINT_new(group), EC_POINT_free);
    if (!group || !point ||
        EC_POINT_oct2point(group, point.get(), encoded.data(), encoded.size(), nullptr) != 1 ||
        EC_KEY_set_public_key(ec_key.get(), point.get()) != 1) {
        return false;
    }
    EVP_PKEY *key = EVP_PKEY_new();
    if (!key || EVP_PKEY_set1_EC_KEY(key, ec_key.get()) != 1) {
        if (key) {
            EVP_PKEY_free(key);
        }
        return false;
    }
    out.reset(key);
    return true;
#else
    const char *group_name = OBJ_nid2sn(nid);
    if (!group_name || coord_len == 0) {
        return false;
    }

    std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)> bld(OSSL_PARAM_BLD_new(),
                                                                        OSSL_PARAM_BLD_free);
    if (!bld ||
        OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, group_name, 0) !=
            1 ||
        OSSL_PARAM_BLD_push_octet_string(bld.get(), OSSL_PKEY_PARAM_PUB_KEY, encoded.data(),
                                         encoded.size()) != 1) {
        return false;
    }

    std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)> params(
        OSSL_PARAM_BLD_to_param(bld.get()), OSSL_PARAM_free);
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free);
    if (!params || !ctx || EVP_PKEY_fromdata_init(ctx.get()) != 1) {
        return false;
    }

    EVP_PKEY *key = nullptr;
    if (EVP_PKEY_fromdata(ctx.get(), &key, EVP_PKEY_PUBLIC_KEY, params.get()) != 1) {
        return false;
    }
    out.reset(key);
    return true;
#endif
}

bool ecdh_secret(EVP_PKEY *own, EVP_PKEY *peer, size_t coord_len, std::vector<uint8_t> &secret)
{
    secret.clear();
    if (!own || !peer || coord_len == 0) {
        return false;
    }

    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new(own, nullptr),
                                                                    EVP_PKEY_CTX_free);
    size_t len = 0;
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 ||
        EVP_PKEY_derive_set_peer(ctx.get(), peer) != 1 ||
        EVP_PKEY_derive(ctx.get(), nullptr, &len) != 1 || len == 0) {
        return false;
    }

    std::vector<uint8_t> raw(len);
    if (EVP_PKEY_derive(ctx.get(), raw.data(), &len) != 1 || len == 0) {
        return false;
    }
    raw.resize(len);

    if (raw.size() < coord_len) {
        secret.assign(coord_len - raw.size(), 0);
        secret.insert(secret.end(), raw.begin(), raw.end());
    } else {
        secret.assign(raw.end() - coord_len, raw.end());
    }
    return true;
}

bool hmac_digest(const EVP_MD *md, const uint8_t *key, size_t key_len, const uint8_t *data,
                 size_t data_len, std::vector<uint8_t> &out)
{
    out.clear();
    if (!md || !key || (!data && data_len > 0)) {
        return false;
    }

    unsigned int len = EVP_MD_size(md);
    if (len == 0) {
        return false;
    }
    out.resize(len);
    if (!HMAC(md, key, static_cast<int>(key_len), data, data_len, out.data(), &len)) {
        out.clear();
        return false;
    }
    out.resize(len);
    return true;
}

bool hkdf_expand_one_block(const EVP_MD *md, const std::vector<uint8_t> &prk,
                           const char *info, size_t out_len, std::vector<uint8_t> &out)
{
    out.clear();
    if (!md || prk.empty() || !info || out_len == 0 || out_len > prk.size()) {
        return false;
    }

    std::vector<uint8_t> input(reinterpret_cast<const uint8_t *>(info),
                               reinterpret_cast<const uint8_t *>(info) + std::strlen(info));
    input.push_back(1);
    std::vector<uint8_t> digest;
    if (!hmac_digest(md, prk.data(), prk.size(), input.data(), input.size(), digest) ||
        digest.size() < out_len) {
        return false;
    }
    out.assign(digest.begin(), digest.begin() + out_len);
    return true;
}

bool derive_intermediate_key(const EVP_MD *md, const std::vector<uint8_t> &secret,
                             const char *info, size_t hash_len, std::vector<uint8_t> &out)
{
    out.clear();
    if (!md || secret.empty() || hash_len == 0) {
        return false;
    }

    std::vector<uint8_t> salt(hash_len, 0);
    std::vector<uint8_t> prk;
    if (!hmac_digest(md, salt.data(), salt.size(), secret.data(), secret.size(), prk)) {
        return false;
    }
    return hkdf_expand_one_block(md, prk, info, hash_len, out);
}

bool derive_bk_ke(const EVP_MD *md, const std::vector<uint8_t> &i_nonce,
                  const std::vector<uint8_t> &r_nonce, const std::vector<uint8_t> &mx,
                  const std::vector<uint8_t> &nx, size_t hash_len, std::vector<uint8_t> &bk,
                  std::vector<uint8_t> &ke)
{
    bk.clear();
    ke.clear();
    if (!md || i_nonce.empty() || r_nonce.empty() || mx.empty() || nx.empty() || hash_len == 0) {
        return false;
    }

    std::vector<uint8_t> salt;
    salt.reserve(i_nonce.size() + r_nonce.size());
    salt.insert(salt.end(), i_nonce.begin(), i_nonce.end());
    salt.insert(salt.end(), r_nonce.begin(), r_nonce.end());

    std::vector<uint8_t> ikm;
    ikm.reserve(mx.size() + nx.size());
    ikm.insert(ikm.end(), mx.begin(), mx.end());
    ikm.insert(ikm.end(), nx.begin(), nx.end());

    if (!hmac_digest(md, salt.data(), salt.size(), ikm.data(), ikm.size(), bk)) {
        return false;
    }
    return hkdf_expand_one_block(md, bk, "DPP Key", hash_len, ke);
}

bool hash_vector(const EVP_MD *md, const std::vector<std::vector<uint8_t>> &parts,
                 std::vector<uint8_t> &out)
{
    out.clear();
    if (!md) {
        return false;
    }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),
                                                                EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), md, nullptr) != 1) {
        return false;
    }
    for (const auto &part : parts) {
        if (!part.empty() && EVP_DigestUpdate(ctx.get(), part.data(), part.size()) != 1) {
            return false;
        }
    }

    unsigned int len = EVP_MD_size(md);
    if (len == 0) {
        return false;
    }
    out.resize(len);
    if (EVP_DigestFinal_ex(ctx.get(), out.data(), &len) != 1) {
        out.clear();
        return false;
    }
    out.resize(len);
    return true;
}

bool aes_siv_encrypt_vector(const std::vector<uint8_t> &key, const std::vector<uint8_t> &plain,
                            const std::vector<std::vector<uint8_t>> &ad,
                            std::vector<uint8_t> &out)
{
    std::vector<const uint8_t *> ad_ptrs;
    std::vector<size_t> ad_lens;
    ad_ptrs.reserve(ad.size());
    ad_lens.reserve(ad.size());
    for (const auto &item : ad) {
        ad_ptrs.push_back(item.empty() ? nullptr : item.data());
        ad_lens.push_back(item.size());
    }
    return mapf::encryption::aes_siv_encrypt(
        key.data(), key.size(), plain.empty() ? nullptr : plain.data(), plain.size(),
        ad_ptrs.empty() ? nullptr : ad_ptrs.data(), ad_lens.empty() ? nullptr : ad_lens.data(),
        ad.size(), out);
}

bool aes_siv_decrypt_vector(const std::vector<uint8_t> &key, const uint8_t *wrapped,
                            size_t wrapped_len, const std::vector<std::vector<uint8_t>> &ad,
                            std::vector<uint8_t> &plain)
{
    std::vector<const uint8_t *> ad_ptrs;
    std::vector<size_t> ad_lens;
    ad_ptrs.reserve(ad.size());
    ad_lens.reserve(ad.size());
    for (const auto &item : ad) {
        ad_ptrs.push_back(item.empty() ? nullptr : item.data());
        ad_lens.push_back(item.size());
    }
    return mapf::encryption::aes_siv_decrypt(
        key.data(), key.size(), wrapped, wrapped_len, ad_ptrs.empty() ? nullptr : ad_ptrs.data(),
        ad_lens.empty() ? nullptr : ad_lens.data(), ad.size(), plain);
}

} // namespace controller_dpp
} // namespace son
