/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_ENROLLEE_KEYSTORE_H_
#define _DPP_ENROLLEE_KEYSTORE_H_

#include <openssl/evp.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace beerocks {
namespace dpp_enrollee {

struct EvpPkeyDeleter {
    void operator()(EVP_PKEY *key) const { EVP_PKEY_free(key); }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

/** Result of DppEnrolleeKeyStore::load_or_create(). */
enum class eKeyStoreResult {
    LOADED,           ///< Valid stored key loaded (or already loaded in this process).
    CREATED,          ///< No store file: a new key was generated and stored.
    REPLACED_CORRUPT, ///< Store content was invalid and was replaced: reprovision the URI on the Controller.
    READ_ERROR,       ///< Store exists but could not be read: nothing replaced; retry on a later WAIT_WPS pass.
    WRITE_ERROR,      ///< New key could not be stored: disable DPP for this agent process (WPS stays usable).
    INVALID_PATH,     ///< No usable store path (install path unknown). There is no /tmp fallback.
};

const char *to_string(eKeyStoreResult result);

/** True when the result leaves a usable key loaded. */
bool is_success(eKeyStoreResult result);

/**
 * Persistent DPP Enrollee bootstrap key of this agent: one P-256 key shared by all backhaul
 * EndPoints. Nothing touches the disk until load_or_create() is called, so callers that are not
 * eligible (gateway, wired backhaul) never create the store. This class does not log; callers
 * log the result and message. Messages never contain private key material.
 */
class DppEnrolleeKeyStore {
public:
    /** Install path + "share/prplmesh_dpp_enrollee_keystore", or empty if the install path is unknown. */
    static std::string default_path();

    explicit DppEnrolleeKeyStore(std::string path);
    ~DppEnrolleeKeyStore();
    DppEnrolleeKeyStore(const DppEnrolleeKeyStore &) = delete;
    DppEnrolleeKeyStore &operator=(const DppEnrolleeKeyStore &) = delete;

    /**
     * Loads the stored key, or creates one if the file is missing or invalid.
     * Does nothing more once a key is loaded. message explains non-LOADED results and warnings.
     */
    eKeyStoreResult load_or_create(std::string &message);

    bool is_loaded() const { return static_cast<bool>(m_key); }
    const std::string &path() const { return m_path; }

    /** Private key as RFC 5915 DER hex, for the bootstrap registration call only. Clear the copy after use. */
    bool get_private_key_hex(std::string &out) const;

    /** Compressed SubjectPublicKeyInfo DER (59 bytes for P-256), the form used in the URI K:. */
    const std::vector<uint8_t> &public_key_der() const { return m_public_der; }

    /** SHA-256 of public_key_der() as lowercase hex; equals the supplicant DPP_BOOTSTRAP_INFO pkhash. */
    const std::string &pkhash_hex() const { return m_pkhash_hex; }

    /** True if spki_der (compressed or uncompressed, e.g. a decoded URI K:) holds this public key. */
    bool matches_public_key_der(const std::vector<uint8_t> &spki_der) const;

private:
    bool set_key(EvpPkeyPtr key);
    eKeyStoreResult create_and_store(bool replacing, std::string &message);
    void clear();

    std::string m_path;
    EvpPkeyPtr m_key;
    std::vector<uint8_t> m_public_der;
    std::string m_pkhash_hex;
};

// Helpers, exposed for unit tests.

/** Generates a new P-256 key. */
bool generate_p256_key(EvpPkeyPtr &out);

/** Parses RFC 5915 DER hex; accepts only a valid P-256 private key. */
bool parse_private_key_hex(const std::string &hex, EvpPkeyPtr &out);

/** Encodes an EC private key as RFC 5915 DER hex (lowercase). */
bool encode_private_key_hex(EVP_PKEY *key, std::string &out_hex);

/** Builds the compressed SubjectPublicKeyInfo DER of a P-256 key (59 bytes). */
bool compressed_public_key_der(EVP_PKEY *key, std::vector<uint8_t> &out);

/** Reads bootstrap_key from the [dpp_enrollee_keys] section; false if absent or not even-length hex. */
bool parse_store_content(const std::string &content, std::string &key_hex);

/** Store file content for key_hex. */
std::string build_store_content(const std::string &key_hex);

/**
 * Decodes the K: public key of a DPP URI ("DPP:...;K:<base64>;...;;") into
 * SubjectPublicKeyInfo DER.
 * Accepts standard base64 with or without '=' padding, and the URL-safe alphabet.
 * False if the URI framing is wrong, K: is missing or repeated, or K: is not valid base64.
 */
bool decode_uri_public_key(const std::string &uri, std::vector<uint8_t> &der);

/** True if the K: key of uri is the key held by store. */
bool uri_matches_key(const std::string &uri, const DppEnrolleeKeyStore &store);

} // namespace dpp_enrollee
} // namespace beerocks

#endif // _DPP_ENROLLEE_KEYSTORE_H_
