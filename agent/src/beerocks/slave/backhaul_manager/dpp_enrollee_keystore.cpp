/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_enrollee_keystore.h"

#include <mapf/common/utils.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/objects.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#include <openssl/params.h>
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <sstream>
#include <utility>

namespace beerocks {
namespace dpp_enrollee {

namespace {

constexpr char k_store_file[]   = "share/prplmesh_dpp_enrollee_keystore";
constexpr char k_section[]      = "dpp_enrollee_keys";
constexpr char k_key_name[]     = "bootstrap_key";
constexpr size_t k_coord_len    = 32;        // P-256
constexpr size_t k_max_store_sz = 64 * 1024; // larger content is treated as invalid

// DER prefix of a SubjectPublicKeyInfo for id-ecPublicKey / prime256v1 with a compressed point.
const uint8_t k_p256_spki_prefix[] = {0x30, 0x39, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
                                      0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                      0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x22, 0x00};

void cleanse(std::string &value)
{
    if (!value.empty()) {
        OPENSSL_cleanse(&value[0], value.size());
    }
    value.clear();
}

void cleanse(std::vector<uint8_t> &value)
{
    if (!value.empty()) {
        OPENSSL_cleanse(value.data(), value.size());
    }
    value.clear();
}

std::string to_hex(const uint8_t *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0f]);
    }
    return out;
}

int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool is_even_hex(const std::string &value)
{
    if (value.empty() || (value.size() % 2) != 0) {
        return false;
    }
    for (const auto ch : value) {
        if (hex_value(ch) < 0) {
            return false;
        }
    }
    return true;
}

bool from_hex(const std::string &hex, std::vector<uint8_t> &out)
{
    out.clear();
    if (!is_even_hex(hex)) {
        return false;
    }
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>((hex_value(hex[i]) << 4) | hex_value(hex[i + 1])));
    }
    return true;
}

std::string trim(const std::string &value)
{
    size_t begin = 0;
    size_t end   = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool is_p256(EVP_PKEY *key)
{
    if (!key || EVP_PKEY_base_id(key) != EVP_PKEY_EC) {
        return false;
    }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    const EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(key);
    const EC_GROUP *group = ec_key ? EC_KEY_get0_group(ec_key) : nullptr;
    return group && EC_GROUP_get_curve_name(group) == NID_X9_62_prime256v1;
#else
    char name[64] = {};
    size_t name_len = 0;
    if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, name, sizeof(name),
                                       &name_len) != 1) {
        return false;
    }
    int nid = OBJ_sn2nid(name);
    if (nid == NID_undef) {
        nid = EC_curve_nist2nid(name);
    }
    return nid == NID_X9_62_prime256v1;
#endif
}

bool public_xy(EVP_PKEY *key, std::vector<uint8_t> &xy)
{
    xy.clear();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    const EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(key);
    const EC_GROUP *group = ec_key ? EC_KEY_get0_group(ec_key) : nullptr;
    const EC_POINT *point = ec_key ? EC_KEY_get0_public_key(ec_key) : nullptr;
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
    const bool have_x = EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_EC_PUB_X, &x_raw) == 1;
    const bool have_y = EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_EC_PUB_Y, &y_raw) == 1;
    std::unique_ptr<BIGNUM, decltype(&BN_free)> x(x_raw, BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> y(y_raw, BN_free);
    if (!have_x || !have_y) {
        return false;
    }
#endif
    xy.assign(2 * k_coord_len, 0);
    if (BN_bn2binpad(x.get(), xy.data(), k_coord_len) <= 0 ||
        BN_bn2binpad(y.get(), xy.data() + k_coord_len, k_coord_len) <= 0) {
        xy.clear();
        return false;
    }
    return true;
}

bool private_key_is_valid(EVP_PKEY *key)
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    const EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(key);
    return ec_key && EC_KEY_check_key(ec_key) == 1;
#else
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new_from_pkey(nullptr, key, nullptr), EVP_PKEY_CTX_free);
    return ctx && EVP_PKEY_check(ctx.get()) == 1;
#endif
}

std::string errno_text(const std::string &what, const std::string &path, int err)
{
    return what + " " + path + ": " + std::strerror(err);
}

enum class eReadResult { OK, MISSING, FAILED };

// Reads the store file. Oversized content is returned empty (treated as invalid, not as an
// I/O error). A world/group-accessible file is tightened to 0600; warning reports that.
eReadResult read_store_file(const std::string &path, std::string &content, std::string &message,
                            std::string &warning)
{
    content.clear();
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        const int err = errno;
        if (err == ENOENT) {
            return eReadResult::MISSING;
        }
        message = errno_text("cannot open", path, err);
        return eReadResult::FAILED;
    }
    struct stat st {
    };
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        message = path + " is not a readable regular file";
        close(fd);
        return eReadResult::FAILED;
    }
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
            const int err = errno;
            message = errno_text("cannot set 0600 on", path, err);
            close(fd);
            return eReadResult::FAILED;
        }
        warning = "tightened permissions of " + path + " to 0600";
    }
    char buf[512];
    bool oversized = false;
    while (true) {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            message = errno_text("cannot read", path, errno);
            OPENSSL_cleanse(buf, sizeof(buf));
            cleanse(content);
            close(fd);
            return eReadResult::FAILED;
        }
        if (n == 0) {
            break;
        }
        if (content.size() + static_cast<size_t>(n) > k_max_store_sz) {
            oversized = true;
            break;
        }
        content.append(buf, static_cast<size_t>(n));
    }
    OPENSSL_cleanse(buf, sizeof(buf));
    close(fd);
    if (oversized) {
        cleanse(content);
    }
    return eReadResult::OK;
}

// 0600 temp file, write, fsync, rename over path, fsync the directory. Removes the temp on failure.
bool write_store_file(const std::string &path, const std::string &content, std::string &message)
{
    const auto slash      = path.rfind('/');
    const std::string dir = (slash == std::string::npos) ? "." : path.substr(0, slash + 1);
    struct stat st {
    };
    if (stat(dir.c_str(), &st) != 0) {
        if (errno != ENOENT || mkdir(dir.c_str(), 0755) != 0) {
            message = errno_text("cannot use directory", dir, errno);
            return false;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        message = dir + " is not a directory";
        return false;
    }

    const std::string tmp_path = path + ".tmp";
    const int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                        S_IRUSR | S_IWUSR);
    if (fd < 0) {
        message = errno_text("cannot create", tmp_path, errno);
        return false;
    }
    bool ok = fchmod(fd, S_IRUSR | S_IWUSR) == 0;
    size_t done = 0;
    while (ok && done < content.size()) {
        const ssize_t n = write(fd, content.data() + done, content.size() - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        ok = n > 0;
        if (ok) {
            done += static_cast<size_t>(n);
        }
    }
    ok = ok && fsync(fd) == 0;
    const int write_errno = errno;
    ok = (close(fd) == 0) && ok;
    if (!ok) {
        message = errno_text("cannot write", tmp_path, write_errno);
        unlink(tmp_path.c_str());
        return false;
    }
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        message = errno_text("cannot rename to", path, errno);
        unlink(tmp_path.c_str());
        return false;
    }
    const int dir_fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0) {
        message = errno_text("cannot open directory", dir, errno);
        return false;
    }
    const bool dir_synced = fsync(dir_fd) == 0;
    const int sync_errno  = errno;
    close(dir_fd);
    if (!dir_synced) {
        message = errno_text("cannot sync directory", dir, sync_errno);
        return false;
    }
    return true;
}

} // namespace

const char *to_string(eKeyStoreResult result)
{
    switch (result) {
    case eKeyStoreResult::LOADED:
        return "LOADED";
    case eKeyStoreResult::CREATED:
        return "CREATED";
    case eKeyStoreResult::REPLACED_CORRUPT:
        return "REPLACED_CORRUPT";
    case eKeyStoreResult::READ_ERROR:
        return "READ_ERROR";
    case eKeyStoreResult::WRITE_ERROR:
        return "WRITE_ERROR";
    case eKeyStoreResult::INVALID_PATH:
        return "INVALID_PATH";
    }
    return "UNKNOWN";
}

bool is_success(eKeyStoreResult result)
{
    return result == eKeyStoreResult::LOADED || result == eKeyStoreResult::CREATED ||
           result == eKeyStoreResult::REPLACED_CORRUPT;
}

bool generate_p256_key(EvpPkeyPtr &out)
{
    out.reset();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec_key(
        EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free);
    if (!ec_key || EC_KEY_generate_key(ec_key.get()) != 1) {
        return false;
    }
    EvpPkeyPtr key(EVP_PKEY_new());
    if (!key || EVP_PKEY_set1_EC_KEY(key.get(), ec_key.get()) != 1) {
        return false;
    }
    out = std::move(key);
#else
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1) {
        return false;
    }
    char group[] = SN_X9_62_prime256v1;
    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0);
    params[1] = OSSL_PARAM_construct_end();
    EVP_PKEY *raw = nullptr;
    if (EVP_PKEY_CTX_set_params(ctx.get(), params) != 1 ||
        EVP_PKEY_generate(ctx.get(), &raw) != 1) {
        return false;
    }
    out.reset(raw);
#endif
    return is_p256(out.get());
}

bool parse_private_key_hex(const std::string &hex, EvpPkeyPtr &out)
{
    out.reset();
    std::vector<uint8_t> der;
    if (!from_hex(hex, der)) {
        return false;
    }
    const unsigned char *ptr = der.data();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_raw = d2i_ECPrivateKey(nullptr, &ptr, static_cast<long>(der.size()));
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec_key(ec_raw, EC_KEY_free);
    EvpPkeyPtr key(ec_key ? EVP_PKEY_new() : nullptr);
    if (!key || EVP_PKEY_set1_EC_KEY(key.get(), ec_key.get()) != 1) {
        key.reset();
    }
#else
    EvpPkeyPtr key(d2i_PrivateKey(EVP_PKEY_EC, nullptr, &ptr, static_cast<long>(der.size())));
#endif
    // Reject trailing bytes, other curves and inconsistent keys.
    const bool consumed = key && ptr == der.data() + der.size();
    cleanse(der);
    if (!consumed || !is_p256(key.get()) || !private_key_is_valid(key.get())) {
        return false;
    }
    out = std::move(key);
    return true;
}

bool encode_private_key_hex(EVP_PKEY *key, std::string &out_hex)
{
    out_hex.clear();
    if (!key || EVP_PKEY_base_id(key) != EVP_PKEY_EC) {
        return false;
    }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = EVP_PKEY_get0_EC_KEY(key);
    const int der_len = ec_key ? i2d_ECPrivateKey(ec_key, nullptr) : 0;
#else
    const int der_len = i2d_PrivateKey(key, nullptr);
#endif
    if (der_len <= 0) {
        return false;
    }
    std::vector<uint8_t> der(static_cast<size_t>(der_len));
    unsigned char *ptr = der.data();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    const int written = i2d_ECPrivateKey(ec_key, &ptr);
#else
    const int written = i2d_PrivateKey(key, &ptr);
#endif
    if (written == der_len) {
        out_hex = to_hex(der.data(), der.size());
    }
    cleanse(der);
    return !out_hex.empty();
}

bool compressed_public_key_der(EVP_PKEY *key, std::vector<uint8_t> &out)
{
    out.clear();
    std::vector<uint8_t> xy;
    if (!is_p256(key) || !public_xy(key, xy)) {
        return false;
    }
    out.assign(std::begin(k_p256_spki_prefix), std::end(k_p256_spki_prefix));
    out.push_back((xy.back() & 0x01) ? 0x03 : 0x02); // parity of Y
    out.insert(out.end(), xy.begin(), xy.begin() + k_coord_len);
    return true;
}

bool parse_store_content(const std::string &content, std::string &key_hex)
{
    cleanse(key_hex);
    std::istringstream in(content);
    std::string line;
    bool in_section = false;
    bool found      = false;
    while (!found && std::getline(in, line)) {
        const std::string text = trim(line);
        if (text.empty() || text[0] == '#' || text[0] == ';') {
            continue;
        }
        if (text.front() == '[' && text.back() == ']') {
            in_section = trim(text.substr(1, text.size() - 2)) == k_section;
            continue;
        }
        const auto eq = text.find('=');
        if (in_section && eq != std::string::npos && trim(text.substr(0, eq)) == k_key_name) {
            key_hex = trim(text.substr(eq + 1));
            found   = true;
        }
    }
    cleanse(line);
    if (!found || !is_even_hex(key_hex)) {
        cleanse(key_hex);
        return false;
    }
    return true;
}

std::string build_store_content(const std::string &key_hex)
{
    return std::string("[") + k_section + "]\n" + k_key_name + "=" + key_hex + "\n";
}

std::string DppEnrolleeKeyStore::default_path()
{
    const auto install_path = mapf::utils::get_install_path();
    if (install_path.empty()) {
        return std::string();
    }
    return install_path + k_store_file;
}

DppEnrolleeKeyStore::DppEnrolleeKeyStore(std::string path) : m_path(std::move(path)) {}

DppEnrolleeKeyStore::~DppEnrolleeKeyStore() { clear(); }

void DppEnrolleeKeyStore::clear()
{
    m_key.reset(); // EVP_PKEY_free clears the private scalar
    m_public_der.clear();
    m_pkhash_hex.clear();
}

bool DppEnrolleeKeyStore::set_key(EvpPkeyPtr key)
{
    std::vector<uint8_t> der;
    if (!compressed_public_key_der(key.get(), der)) {
        return false;
    }
    uint8_t hash[SHA256_DIGEST_LENGTH] = {};
    SHA256(der.data(), der.size(), hash);
    m_pkhash_hex = to_hex(hash, sizeof(hash));
    m_public_der = std::move(der);
    m_key        = std::move(key);
    return true;
}

eKeyStoreResult DppEnrolleeKeyStore::load_or_create(std::string &message)
{
    message.clear();
    if (m_key) {
        return eKeyStoreResult::LOADED;
    }
    if (m_path.empty()) {
        message = "no DPP enrollee key store path (install path unknown); no /tmp fallback";
        return eKeyStoreResult::INVALID_PATH;
    }

    std::string content;
    std::string warning;
    const auto read_result = read_store_file(m_path, content, message, warning);
    if (read_result == eReadResult::FAILED) {
        return eKeyStoreResult::READ_ERROR; // keep the file; retry on a later pass
    }
    const bool existed = read_result == eReadResult::OK;
    if (existed) {
        std::string hex;
        EvpPkeyPtr key;
        const bool ok = parse_store_content(content, hex) && parse_private_key_hex(hex, key) &&
                        set_key(std::move(key));
        cleanse(hex);
        cleanse(content);
        if (ok) {
            message = warning;
            return eKeyStoreResult::LOADED;
        }
    }
    return create_and_store(existed, message);
}

eKeyStoreResult DppEnrolleeKeyStore::create_and_store(bool replacing, std::string &message)
{
    EvpPkeyPtr key;
    std::string hex;
    if (!generate_p256_key(key) || !encode_private_key_hex(key.get(), hex)) {
        message = "cannot generate a P-256 key";
        return eKeyStoreResult::WRITE_ERROR;
    }
    std::string new_content = build_store_content(hex);
    bool stored             = write_store_file(m_path, new_content, message);
    cleanse(new_content);
    if (stored) {
        std::string back;
        std::string back_hex;
        std::string back_warning;
        stored = read_store_file(m_path, back, message, back_warning) == eReadResult::OK &&
                 parse_store_content(back, back_hex) && back_hex == hex;
        cleanse(back);
        cleanse(back_hex);
        if (!stored && message.empty()) {
            message = "read-back of " + m_path + " does not match the written key";
        }
    }
    cleanse(hex);
    if (!stored || !set_key(std::move(key))) {
        if (message.empty()) {
            message = "cannot use the new key";
        }
        return eKeyStoreResult::WRITE_ERROR;
    }
    if (replacing) {
        message = "invalid key store " + m_path +
                  " was replaced; reprovision this agent's DPP URI on the Controller";
        return eKeyStoreResult::REPLACED_CORRUPT;
    }
    message.clear();
    return eKeyStoreResult::CREATED;
}

bool DppEnrolleeKeyStore::get_private_key_hex(std::string &out) const
{
    out.clear();
    return m_key && encode_private_key_hex(m_key.get(), out);
}

bool DppEnrolleeKeyStore::matches_public_key_der(const std::vector<uint8_t> &spki_der) const
{
    if (!m_key || spki_der.empty()) {
        return false;
    }
    const unsigned char *ptr = spki_der.data();
    EvpPkeyPtr other(d2i_PUBKEY(nullptr, &ptr, static_cast<long>(spki_der.size())));
    if (!other || ptr != spki_der.data() + spki_der.size()) {
        return false;
    }
    std::vector<uint8_t> other_der;
    return compressed_public_key_der(other.get(), other_der) && other_der == m_public_der;
}

bool decode_uri_public_key(const std::string &uri, std::vector<uint8_t> &der)
{
    der.clear();
    if (uri.size() < 6 || uri.compare(0, 4, "DPP:") != 0 ||
        uri.compare(uri.size() - 2, 2, ";;") != 0) {
        return false;
    }
    std::string key;
    unsigned count = 0;
    for (size_t pos = 4, end; (end = uri.find(';', pos)) != std::string::npos; pos = end + 1) {
        if (uri.compare(pos, 2, "K:") == 0) {
            key = uri.substr(pos + 2, end - pos - 2);
            ++count;
        }
    }
    // URIs carry standard base64 with '=' padding (a 59-byte P-256 key always has one '=').
    for (int pad = 0; pad < 2 && !key.empty() && key.back() == '='; ++pad) {
        key.pop_back();
    }
    if (count != 1 || key.empty() || key.size() > 1024 || key.size() % 4 == 1) {
        return false;
    }
    for (auto &c : key) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '+' || c == '/')) {
            return false;
        }
    }
    const size_t pad = (4 - key.size() % 4) % 4;
    key.append(pad, '=');
    std::vector<uint8_t> out(key.size());
    const int len = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char *>(key.data()),
                                    static_cast<int>(key.size()));
    if (len <= 0 || static_cast<size_t>(len) <= pad) {
        return false;
    }
    out.resize(static_cast<size_t>(len) - pad);
    der.swap(out);
    return true;
}

bool uri_matches_key(const std::string &uri, const DppEnrolleeKeyStore &store)
{
    std::vector<uint8_t> der;
    return decode_uri_public_key(uri, der) && store.matches_public_key_der(der);
}

} // namespace dpp_enrollee
} // namespace beerocks
