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

bool split_jws(const std::string &jws, std::string &header_b64, std::string &payload_b64,
               std::string &sig_b64, std::string &error)
{
    header_b64.clear();
    payload_b64.clear();
    sig_b64.clear();

    auto first_dot = jws.find('.');
    if (first_dot == std::string::npos) {
        error = "Missing JWS header separator";
        return false;
    }
    auto second_dot = jws.find('.', first_dot + 1);
    if (second_dot == std::string::npos || second_dot <= first_dot + 1) {
        error = "Missing JWS payload separator";
        return false;
    }

    header_b64  = jws.substr(0, first_dot);
    payload_b64 = jws.substr(first_dot + 1, second_dot - first_dot - 1);
    sig_b64     = jws.substr(second_dot + 1);
    if (header_b64.empty() || payload_b64.empty() || sig_b64.empty()) {
        error = "Empty JWS component";
        return false;
    }
    return true;
}

bool parse_jws_header(const std::string &json, std::string &alg, std::string &typ,
                      std::string &kid, std::string &error)
{
    alg.clear();
    typ.clear();
    kid.clear();

    json_object *root = json_tokener_parse(json.c_str());
    if (!root) {
        error = "Invalid JWS header JSON";
        return false;
    }

    json_object *alg_obj = nullptr;
    if (!json_object_object_get_ex(root, "alg", &alg_obj) ||
        json_object_get_type(alg_obj) != json_type_string) {
        json_object_put(root);
        error = "Missing JWS alg";
        return false;
    }
    alg = json_object_get_string(alg_obj);

    json_object *typ_obj = nullptr;
    if (json_object_object_get_ex(root, "typ", &typ_obj) &&
        json_object_get_type(typ_obj) == json_type_string) {
        typ = json_object_get_string(typ_obj);
    }

    json_object *kid_obj = nullptr;
    if (json_object_object_get_ex(root, "kid", &kid_obj) &&
        json_object_get_type(kid_obj) == json_type_string) {
        kid = json_object_get_string(kid_obj);
    }

    json_object_put(root);
    return true;
}

bool jwk_pubkey_to_der_hex(json_object *jwk, std::string &out_hex)
{
    out_hex.clear();
    if (!jwk || json_object_get_type(jwk) != json_type_object) {
        return false;
    }

    json_object *kty_obj = nullptr;
    json_object *crv_obj = nullptr;
    json_object *x_obj   = nullptr;
    json_object *y_obj   = nullptr;
    if (!json_object_object_get_ex(jwk, "kty", &kty_obj) ||
        !json_object_object_get_ex(jwk, "crv", &crv_obj) ||
        !json_object_object_get_ex(jwk, "x", &x_obj) ||
        !json_object_object_get_ex(jwk, "y", &y_obj) ||
        json_object_get_type(kty_obj) != json_type_string ||
        json_object_get_type(crv_obj) != json_type_string ||
        json_object_get_type(x_obj) != json_type_string ||
        json_object_get_type(y_obj) != json_type_string) {
        return false;
    }

    const std::string kty = json_object_get_string(kty_obj);
    if (kty != "EC") {
        return false;
    }

    const std::string crv = json_object_get_string(crv_obj);
    int nid               = NID_undef;
    if (crv == "P-256") {
        nid = NID_X9_62_prime256v1;
    } else if (crv == "P-384") {
        nid = NID_secp384r1;
    } else if (crv == "P-521") {
        nid = NID_secp521r1;
    }
    if (nid == NID_undef) {
        return false;
    }

    std::string x_decoded;
    std::string y_decoded;
    if (!beerocks::string_utils::base64_url_decode(json_object_get_string(x_obj), x_decoded) ||
        !beerocks::string_utils::base64_url_decode(json_object_get_string(y_obj), y_decoded)) {
        return false;
    }

    if (x_decoded.empty() || y_decoded.empty()) {
        return false;
    }

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(nullptr, EVP_PKEY_free);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec_key(EC_KEY_new_by_curve_name(nid),
                                                           EC_KEY_free);
    if (!ec_key) {
        return false;
    }

    const EC_GROUP *group = EC_KEY_get0_group(ec_key.get());
    std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> point(EC_POINT_new(group), EC_POINT_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> x_bn(
        BN_bin2bn(reinterpret_cast<const uint8_t *>(x_decoded.data()), x_decoded.size(), nullptr),
        BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> y_bn(
        BN_bin2bn(reinterpret_cast<const uint8_t *>(y_decoded.data()), y_decoded.size(), nullptr),
        BN_free);

    if (!point || !x_bn || !y_bn ||
        !EC_POINT_set_affine_coordinates_GFp(group, point.get(), x_bn.get(), y_bn.get(), nullptr) ||
        !EC_KEY_set_public_key(ec_key.get(), point.get())) {
        return false;
    }

    pkey.reset(EVP_PKEY_new());
    if (!pkey || EVP_PKEY_set1_EC_KEY(pkey.get(), ec_key.get()) != 1) {
        return false;
    }
#else
    const size_t coord_len = std::max(x_decoded.size(), y_decoded.size());
    std::vector<uint8_t> pub(1 + 2 * coord_len);
    pub[0] = 0x04;
    std::memcpy(pub.data() + 1 + (coord_len - x_decoded.size()), x_decoded.data(),
                x_decoded.size());
    std::memcpy(pub.data() + 1 + coord_len + (coord_len - y_decoded.size()), y_decoded.data(),
                y_decoded.size());

    std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)> bld(OSSL_PARAM_BLD_new(),
                                                                        OSSL_PARAM_BLD_free);
    if (!bld) {
        return false;
    }

    const char *group_name = OBJ_nid2sn(nid);
    if (!group_name) {
        return false;
    }

    if (OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, group_name, 0) !=
            1 ||
        OSSL_PARAM_BLD_push_octet_string(bld.get(), OSSL_PKEY_PARAM_PUB_KEY, pub.data(),
                                         pub.size()) != 1) {
        return false;
    }

    std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)> params(
        OSSL_PARAM_BLD_to_param(bld.get()), OSSL_PARAM_free);
    if (!params) {
        return false;
    }

    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_fromdata_init(ctx.get()) != 1) {
        return false;
    }
    EVP_PKEY *raw = nullptr;
    if (EVP_PKEY_fromdata(ctx.get(), &raw, EVP_PKEY_PUBLIC_KEY, params.get()) != 1) {
        return false;
    }
    pkey.reset(raw);
#endif

    int len = i2d_PUBKEY(pkey.get(), nullptr);
    if (len <= 0) {
        return false;
    }

    std::vector<uint8_t> der(static_cast<size_t>(len));
    unsigned char *ptr = der.data();
    if (i2d_PUBKEY(pkey.get(), &ptr) != len) {
        return false;
    }

    out_hex = bytes_to_hex_string(der.data(), der.size());
    return !out_hex.empty();
}

bool verify_jws_signature(EVP_PKEY *pkey, const EVP_MD *md, size_t sig_part_len,
                          const std::string &signing_input, const std::string &signature_raw,
                          std::string &error)
{
    if (!pkey || !md) {
        error = "Missing signature parameters";
        return false;
    }

    if (signature_raw.size() != sig_part_len * 2) {
        error = "Invalid JWS signature length";
        return false;
    }

    const auto *raw = reinterpret_cast<const uint8_t *>(signature_raw.data());
    std::unique_ptr<BIGNUM, decltype(&BN_free)> r(BN_bin2bn(raw, sig_part_len, nullptr), BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> s(
        BN_bin2bn(raw + sig_part_len, sig_part_len, nullptr), BN_free);
    std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> sig(ECDSA_SIG_new(), ECDSA_SIG_free);
    if (!sig || !r || !s || ECDSA_SIG_set0(sig.get(), r.release(), s.release()) != 1) {
        error = "Failed building ECDSA signature";
        return false;
    }

    int der_len = i2d_ECDSA_SIG(sig.get(), nullptr);
    if (der_len <= 0) {
        error = "Failed encoding ECDSA signature";
        return false;
    }
    std::vector<uint8_t> der(static_cast<size_t>(der_len));
    unsigned char *ptr = der.data();
    if (i2d_ECDSA_SIG(sig.get(), &ptr) != der_len) {
        error = "Failed encoding ECDSA signature";
        return false;
    }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md_ctx(EVP_MD_CTX_new(),
                                                                   EVP_MD_CTX_free);
    if (!md_ctx) {
        error = "Failed allocating digest context";
        return false;
    }

    if (EVP_DigestVerifyInit(md_ctx.get(), nullptr, md, nullptr, pkey) != 1 ||
        EVP_DigestVerifyUpdate(md_ctx.get(), signing_input.data(), signing_input.size()) != 1) {
        error = "Failed initializing signature verification";
        return false;
    }

    if (EVP_DigestVerifyFinal(md_ctx.get(), der.data(), der.size()) != 1) {
        error = "JWS signature verification failed";
        return false;
    }

    return true;
}

bool jws_sign(EVP_PKEY *pkey, const EVP_MD *md, size_t sig_part_len,
              const std::string &signing_input, std::string &signature_raw, std::string &error)
{
    signature_raw.clear();

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md_ctx(EVP_MD_CTX_new(),
                                                                   EVP_MD_CTX_free);
    if (!md_ctx) {
        error = "Failed allocating digest context";
        return false;
    }

    if (EVP_DigestSignInit(md_ctx.get(), nullptr, md, nullptr, pkey) != 1 ||
        EVP_DigestSignUpdate(md_ctx.get(), signing_input.data(), signing_input.size()) != 1) {
        error = "Failed initializing signature";
        return false;
    }

    size_t der_len = 0;
    if (EVP_DigestSignFinal(md_ctx.get(), nullptr, &der_len) != 1 || der_len == 0) {
        error = "Failed signing";
        return false;
    }
    std::vector<uint8_t> der(der_len);
    if (EVP_DigestSignFinal(md_ctx.get(), der.data(), &der_len) != 1 || der_len == 0) {
        error = "Failed signing";
        return false;
    }
    der.resize(der_len);

    const unsigned char *ptr = der.data();
    std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> sig(d2i_ECDSA_SIG(nullptr, &ptr, der_len),
                                                              ECDSA_SIG_free);
    if (!sig) {
        error = "Failed decoding ECDSA signature";
        return false;
    }

    const BIGNUM *r = nullptr;
    const BIGNUM *s = nullptr;
    ECDSA_SIG_get0(sig.get(), &r, &s);
    if (!r || !s) {
        error = "Failed extracting ECDSA signature";
        return false;
    }

    signature_raw.resize(sig_part_len * 2);
    if (BN_bn2binpad(r, reinterpret_cast<unsigned char *>(&signature_raw[0]), sig_part_len) <= 0 ||
        BN_bn2binpad(s, reinterpret_cast<unsigned char *>(&signature_raw[sig_part_len]),
                     sig_part_len) <= 0) {
        error = "Failed encoding ECDSA signature";
        return false;
    }

    return true;
}

bool ec_key_to_jwk(EVP_PKEY *key, json_object **jwk_out, std::string &error)
{
    if (!key || !jwk_out) {
        error = "Missing key";
        return false;
    }

    std::string crv;
    size_t coord_len = 0;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(key);
    if (!ec_key) {
        error = "Invalid EC key";
        return false;
    }
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    const EC_POINT *point = EC_KEY_get0_public_key(ec_key);
    if (!group || !point) {
        error = "Missing EC public key";
        return false;
    }
    const int nid = EC_GROUP_get_curve_name(group);
    coord_len     = (EC_GROUP_get_degree(group) + 7) / 8;
    if (nid == NID_X9_62_prime256v1) {
        crv = "P-256";
    } else if (nid == NID_secp384r1) {
        crv = "P-384";
    } else if (nid == NID_secp521r1) {
        crv = "P-521";
    } else {
        error = "Unsupported EC curve";
        return false;
    }

    std::unique_ptr<BIGNUM, decltype(&BN_free)> x_bn(BN_new(), BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> y_bn(BN_new(), BN_free);
    if (!x_bn || !y_bn ||
        EC_POINT_get_affine_coordinates_GFp(group, point, x_bn.get(), y_bn.get(), nullptr) != 1) {
        error = "Failed reading EC public key";
        return false;
    }
#else
    BIGNUM *x_raw = nullptr;
    BIGNUM *y_raw = nullptr;
    if (EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_EC_PUB_X, &x_raw) != 1 ||
        EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_EC_PUB_Y, &y_raw) != 1) {
        error = "Missing EC public key";
        return false;
    }

    std::unique_ptr<BIGNUM, decltype(&BN_free)> x_bn(x_raw, BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> y_bn(y_raw, BN_free);

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
    if (group == "prime256v1" || group == "secp256r1") {
        crv       = "P-256";
        coord_len = 32;
    } else if (group == "secp384r1") {
        crv       = "P-384";
        coord_len = 48;
    } else if (group == "secp521r1") {
        crv       = "P-521";
        coord_len = 66;
    } else {
        const int bits = EVP_PKEY_get_bits(key);
        if (bits == 256) {
            crv       = "P-256";
            coord_len = 32;
        } else if (bits == 384) {
            crv       = "P-384";
            coord_len = 48;
        } else if (bits == 521) {
            crv       = "P-521";
            coord_len = 66;
        } else {
            error = "Unsupported EC curve";
            return false;
        }
    }
#endif

    std::vector<uint8_t> x_bytes(coord_len);
    std::vector<uint8_t> y_bytes(coord_len);
    if (BN_bn2binpad(x_bn.get(), x_bytes.data(), x_bytes.size()) <= 0 ||
        BN_bn2binpad(y_bn.get(), y_bytes.data(), y_bytes.size()) <= 0) {
        error = "Failed encoding EC public key";
        return false;
    }

    std::string x_b64 = base64url_encode(x_bytes.data(), x_bytes.size());
    std::string y_b64 = base64url_encode(y_bytes.data(), y_bytes.size());
    if (x_b64.empty() || y_b64.empty()) {
        error = "Failed encoding EC public key";
        return false;
    }

    json_object *jwk = json_object_new_object();
    json_object_object_add(jwk, "kty", json_object_new_string("EC"));
    json_object_object_add(jwk, "crv", json_object_new_string(crv.c_str()));
    json_object_object_add(jwk, "x", json_object_new_string(x_b64.c_str()));
    json_object_object_add(jwk, "y", json_object_new_string(y_b64.c_str()));

    *jwk_out = jwk;
    return true;
}

bool add_public_jwk(json_object *parent, const char *name, EVP_PKEY *key, const std::string &kid,
                    std::string &error)
{
    if (!parent || !name || !key) {
        error = "Missing JWK parameters";
        return false;
    }

    json_object *jwk = nullptr;
    if (!ec_key_to_jwk(key, &jwk, error)) {
        return false;
    }

    if (!kid.empty()) {
        json_object_object_add(jwk, "kid", json_object_new_string(kid.c_str()));
    }

    json_object_object_add(parent, name, jwk);
    return true;
}

} // namespace controller_dpp
} // namespace son
