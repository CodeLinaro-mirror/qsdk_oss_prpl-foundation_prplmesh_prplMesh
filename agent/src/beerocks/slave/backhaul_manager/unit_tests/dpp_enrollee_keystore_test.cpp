/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "../dpp_enrollee_keystore.h"

#include <gtest/gtest.h>

#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

using namespace beerocks::dpp_enrollee;

namespace {
enum class IoFault { NONE, FCHMOD, FILE_SYNC, DIRECTORY_SYNC };
thread_local IoFault io_fault = IoFault::NONE;

class ScopedIoFault {
public:
    explicit ScopedIoFault(IoFault fault) : previous(io_fault) { io_fault = fault; }
    ~ScopedIoFault() { io_fault = previous; }
    ScopedIoFault(const ScopedIoFault &) = delete;
    ScopedIoFault &operator=(const ScopedIoFault &) = delete;

private:
    IoFault previous;
};
} // namespace

extern "C" int __real_fchmod(int fd, mode_t mode);
extern "C" int __real_fsync(int fd);
extern "C" int __wrap_fchmod(int fd, mode_t mode)
{
    if (io_fault == IoFault::FCHMOD) {
        errno = EPERM;
        return -1;
    }
    return __real_fchmod(fd, mode);
}
extern "C" int __wrap_fsync(int fd)
{
    struct stat st {
    };
    if (io_fault != IoFault::NONE && fstat(fd, &st) == 0) {
        if ((io_fault == IoFault::FILE_SYNC && S_ISREG(st.st_mode)) ||
            (io_fault == IoFault::DIRECTORY_SYNC && S_ISDIR(st.st_mode))) {
            errno = S_ISDIR(st.st_mode) ? EINVAL : EIO;
            return -1;
        }
    }
    return __real_fsync(fd);
}

namespace {

class DppEnrolleeKeyStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        char tmpl[] = "/tmp/dpp_enrollee_ks_XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        m_dir  = tmpl;
        m_path = m_dir + "/share/prplmesh_dpp_enrollee_keystore";
    }
    void TearDown() override
    {
        std::remove(m_path.c_str());
        std::remove((m_path + ".tmp").c_str());
        rmdir((m_path + ".tmp").c_str()); // write-failure fixture
        rmdir(m_path.c_str());            // when a test made the path a directory
        rmdir((m_dir + "/share").c_str());
        std::remove((m_dir + "/blocker").c_str());
        rmdir(m_dir.c_str());
    }
    void write_file(const std::string &content, mode_t mode = 0600)
    {
        mkdir((m_dir + "/share").c_str(), 0755);
        std::ofstream(m_path, std::ios::binary | std::ios::trunc) << content;
        chmod(m_path.c_str(), mode);
    }
    std::string read_file() const
    {
        std::ifstream in(m_path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    mode_t file_mode() const
    {
        struct stat st {
        };
        return stat(m_path.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0;
    }
    bool exists(const std::string &path) const { return access(path.c_str(), F_OK) == 0; }

    std::string m_dir;
    std::string m_path;
};

std::vector<uint8_t> uncompressed_spki(EVP_PKEY *key)
{
    const int len = i2d_PUBKEY(key, nullptr);
    std::vector<uint8_t> der(len > 0 ? static_cast<size_t>(len) : 0);
    unsigned char *ptr = der.data();
    if (len <= 0 || i2d_PUBKEY(key, &ptr) != len) {
        der.clear();
    }
    return der;
}

// RFC 5915 hex of a valid EC key on a curve other than P-256.
std::string p384_key_hex()
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_secp384r1);
    EvpPkeyPtr key(EVP_PKEY_new());
    if (!ec_key || EC_KEY_generate_key(ec_key) != 1 || !key ||
        EVP_PKEY_set1_EC_KEY(key.get(), ec_key) != 1) {
        EC_KEY_free(ec_key);
        return std::string();
    }
    EC_KEY_free(ec_key);
#else
    EvpPkeyPtr key(EVP_EC_gen("P-384"));
#endif
    std::string hex;
    return (key && encode_private_key_hex(key.get(), hex)) ? hex : std::string();
}

} // namespace

TEST_F(DppEnrolleeKeyStoreTest, ConstructorDoesNotTouchDisk)
{
    DppEnrolleeKeyStore store(m_path);
    EXPECT_FALSE(store.is_loaded());
    EXPECT_FALSE(exists(m_dir + "/share"));
    std::string hex;
    EXPECT_FALSE(store.get_private_key_hex(hex));
}

TEST_F(DppEnrolleeKeyStoreTest, CreatesOwnerOnlyStoreAndReloadsSameIdentity)
{
    std::string msg;
    std::string first_hex;
    std::string first_pkhash;
    {
        DppEnrolleeKeyStore store(m_path);
        EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::CREATED) << msg;
        ASSERT_TRUE(store.is_loaded());
        EXPECT_EQ(file_mode(), 0600u);
        EXPECT_FALSE(exists(m_path + ".tmp"));
        ASSERT_TRUE(store.get_private_key_hex(first_hex));
        EXPECT_EQ(read_file(), build_store_content(first_hex));
        first_pkhash = store.pkhash_hex();
        // A second call in the same process does nothing more.
        EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::LOADED);
        EXPECT_EQ(store.pkhash_hex(), first_pkhash);
    }
    DppEnrolleeKeyStore reloaded(m_path);
    EXPECT_EQ(reloaded.load_or_create(msg), eKeyStoreResult::LOADED) << msg;
    std::string hex;
    ASSERT_TRUE(reloaded.get_private_key_hex(hex));
    EXPECT_EQ(hex, first_hex);
    EXPECT_EQ(reloaded.pkhash_hex(), first_pkhash);
}

TEST_F(DppEnrolleeKeyStoreTest, InvalidContentIsReplacedWithOwnerOnlyStore)
{
    const std::string other_curve = p384_key_hex();
    ASSERT_FALSE(other_curve.empty());
    const std::vector<std::string> invalid = {
        "garbage",
        "[dpp_enrollee_keys]\n",
        "[other]\nbootstrap_key=00\n",
        "[dpp_enrollee_keys]\nbootstrap_key=zz\n",
        "[dpp_enrollee_keys]\nbootstrap_key=3077\n",
        build_store_content(other_curve),
        std::string(70 * 1024, 'a'),
    };
    for (const auto &content : invalid) {
        write_file(content, 0644);
        DppEnrolleeKeyStore store(m_path);
        std::string msg;
        EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::REPLACED_CORRUPT)
            << content.substr(0, 40);
        EXPECT_NE(msg.find("reprovision"), std::string::npos);
        EXPECT_TRUE(store.is_loaded());
        EXPECT_EQ(file_mode(), 0600u);
        std::string hex;
        ASSERT_TRUE(store.get_private_key_hex(hex));
        EXPECT_EQ(read_file(), build_store_content(hex));
    }
}

TEST_F(DppEnrolleeKeyStoreTest, ReadErrorKeepsExistingEntry)
{
    ASSERT_EQ(mkdir((m_dir + "/share").c_str(), 0755), 0);
    ASSERT_EQ(mkdir(m_path.c_str(), 0700), 0); // a directory cannot be read as the store
    DppEnrolleeKeyStore store(m_path);
    std::string msg;
    EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::READ_ERROR);
    EXPECT_FALSE(msg.empty());
    EXPECT_FALSE(store.is_loaded());
    struct stat st {
    };
    ASSERT_EQ(stat(m_path.c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode)); // not replaced
}

TEST_F(DppEnrolleeKeyStoreTest, WriteErrorLeavesNoKey)
{
    ASSERT_EQ(mkdir((m_dir + "/share").c_str(), 0755), 0);
    ASSERT_EQ(mkdir((m_path + ".tmp").c_str(), 0700), 0);
    DppEnrolleeKeyStore store(m_path);
    std::string msg;
    EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::WRITE_ERROR);
    EXPECT_FALSE(msg.empty());
    EXPECT_FALSE(store.is_loaded());
    EXPECT_TRUE(store.pkhash_hex().empty());
    EXPECT_FALSE(is_success(eKeyStoreResult::WRITE_ERROR));
}

TEST_F(DppEnrolleeKeyStoreTest, NonDirectoryParentIsReadError)
{
    std::ofstream(m_dir + "/blocker") << "x";
    DppEnrolleeKeyStore store(m_dir + "/blocker/prplmesh_dpp_enrollee_keystore");
    std::string msg;
    EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::READ_ERROR);
    EXPECT_FALSE(store.is_loaded());
    EXPECT_TRUE(store.pkhash_hex().empty());
    EXPECT_TRUE(exists(m_dir + "/blocker"));
}

TEST_F(DppEnrolleeKeyStoreTest, PermissionRepairFailureDoesNotLoadOrReplace)
{
    EvpPkeyPtr key;
    std::string hex;
    ASSERT_TRUE(generate_p256_key(key));
    ASSERT_TRUE(encode_private_key_hex(key.get(), hex));
    const auto valid = build_store_content(hex);
    for (const auto &content : {valid, std::string("invalid")}) {
        write_file(content, 0644);
        DppEnrolleeKeyStore store(m_path);
        std::string msg;
        {
            ScopedIoFault fault(IoFault::FCHMOD);
            EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::READ_ERROR);
        }
        EXPECT_NE(msg.find("cannot set 0600"), std::string::npos);
        EXPECT_FALSE(store.is_loaded());
        EXPECT_TRUE(store.pkhash_hex().empty());
        std::string loaded;
        EXPECT_FALSE(store.get_private_key_hex(loaded));
        EXPECT_TRUE(loaded.empty());
        EXPECT_TRUE(read_file() == content); // do not print private content on failure
        EXPECT_EQ(file_mode(), 0644u);
        EXPECT_FALSE(exists(m_path + ".tmp"));
    }
    write_file(valid, 0644);
    DppEnrolleeKeyStore recovered(m_path);
    std::string msg, loaded;
    EXPECT_EQ(recovered.load_or_create(msg), eKeyStoreResult::LOADED);
    ASSERT_TRUE(recovered.get_private_key_hex(loaded));
    EXPECT_TRUE(loaded == hex);
    EXPECT_EQ(file_mode(), 0600u);
}

TEST_F(DppEnrolleeKeyStoreTest, FileSyncFailureRemovesTemporaryAndLeavesNoKey)
{
    DppEnrolleeKeyStore store(m_path);
    std::string msg, hex;
    {
        ScopedIoFault fault(IoFault::FILE_SYNC);
        EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::WRITE_ERROR);
    }
    EXPECT_NE(msg.find("cannot write"), std::string::npos);
    EXPECT_FALSE(store.is_loaded());
    EXPECT_TRUE(store.pkhash_hex().empty());
    EXPECT_FALSE(store.get_private_key_hex(hex));
    EXPECT_FALSE(exists(m_path));
    EXPECT_FALSE(exists(m_path + ".tmp"));
}

TEST_F(DppEnrolleeKeyStoreTest, UnsupportedDirectorySyncFailsButPreservesRenamedIdentity)
{
    DppEnrolleeKeyStore store(m_path);
    std::string msg, hex;
    {
        ScopedIoFault fault(IoFault::DIRECTORY_SYNC);
        EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::WRITE_ERROR);
    }
    EXPECT_NE(msg.find("cannot sync directory"), std::string::npos);
    EXPECT_FALSE(store.is_loaded());
    EXPECT_TRUE(store.pkhash_hex().empty());
    EXPECT_FALSE(store.get_private_key_hex(hex));
    ASSERT_TRUE(exists(m_path)); // rename succeeded; do not delete or rotate this key
    EXPECT_FALSE(exists(m_path + ".tmp"));
    EXPECT_EQ(file_mode(), 0600u);
    const auto content = read_file();
    std::string persisted_hex;
    ASSERT_TRUE(parse_store_content(content, persisted_hex));
    DppEnrolleeKeyStore reloaded(m_path);
    ASSERT_EQ(reloaded.load_or_create(msg), eKeyStoreResult::LOADED);
    ASSERT_TRUE(reloaded.get_private_key_hex(hex));
    EXPECT_TRUE(hex == persisted_hex);
    EXPECT_TRUE(read_file() == content);
}

TEST_F(DppEnrolleeKeyStoreTest, EmptyPathIsInvalid)
{
    DppEnrolleeKeyStore store("");
    std::string msg;
    EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::INVALID_PATH);
    EXPECT_FALSE(store.is_loaded());
}

TEST_F(DppEnrolleeKeyStoreTest, ValidStoreWithLoosePermissionsIsTightened)
{
    EvpPkeyPtr key;
    std::string hex;
    ASSERT_TRUE(generate_p256_key(key));
    ASSERT_TRUE(encode_private_key_hex(key.get(), hex));
    write_file("# comment\n[dpp_enrollee_keys]\n  bootstrap_key = " + hex + "  \n", 0644);
    DppEnrolleeKeyStore store(m_path);
    std::string msg;
    EXPECT_EQ(store.load_or_create(msg), eKeyStoreResult::LOADED);
    EXPECT_NE(msg.find("0600"), std::string::npos);
    EXPECT_EQ(file_mode(), 0600u);
    std::string loaded;
    ASSERT_TRUE(store.get_private_key_hex(loaded));
    EXPECT_EQ(loaded, hex);
}

TEST(DppEnrolleeKeyStore, DefaultPathUsesInstallShareDirectory)
{
    const std::string suffix = "share/prplmesh_dpp_enrollee_keystore";
    const auto path          = DppEnrolleeKeyStore::default_path();
    ASSERT_GT(path.size(), suffix.size());
    EXPECT_EQ(path.compare(path.size() - suffix.size(), suffix.size(), suffix), 0);
    EXPECT_NE(path.find('/'), std::string::npos);
}

TEST_F(DppEnrolleeKeyStoreTest, PkhashIsSha256OfCompressedSpki)
{
    DppEnrolleeKeyStore store(m_path);
    std::string msg;
    ASSERT_TRUE(is_success(store.load_or_create(msg))) << msg;
    const auto &der = store.public_key_der();
    ASSERT_EQ(der.size(), 59u);
    EXPECT_EQ(der[0], 0x30);
    EXPECT_EQ(der[1], 0x39);
    EXPECT_TRUE(der[26] == 0x02 || der[26] == 0x03);

    uint8_t hash[SHA256_DIGEST_LENGTH] = {};
    SHA256(der.data(), der.size(), hash);
    static const char hex_chars[] = "0123456789abcdef";
    std::string expected;
    for (auto byte : hash) {
        expected.push_back(hex_chars[byte >> 4]);
        expected.push_back(hex_chars[byte & 0x0f]);
    }
    EXPECT_EQ(store.pkhash_hex(), expected);

    // OpenSSL must decode the hand-built compressed DER to the same key.
    const unsigned char *ptr = der.data();
    EvpPkeyPtr decoded(d2i_PUBKEY(nullptr, &ptr, static_cast<long>(der.size())));
    ASSERT_TRUE(decoded);
    std::vector<uint8_t> again;
    ASSERT_TRUE(compressed_public_key_der(decoded.get(), again));
    EXPECT_EQ(again, der);
}

TEST_F(DppEnrolleeKeyStoreTest, PublicKeyComparedByValueNotEncoding)
{
    DppEnrolleeKeyStore store(m_path);
    std::string msg;
    ASSERT_TRUE(is_success(store.load_or_create(msg))) << msg;
    std::string hex;
    ASSERT_TRUE(store.get_private_key_hex(hex));
    EvpPkeyPtr same;
    ASSERT_TRUE(parse_private_key_hex(hex, same));

    const auto uncompressed = uncompressed_spki(same.get());
    ASSERT_EQ(uncompressed.size(), 91u);
    EXPECT_TRUE(store.matches_public_key_der(uncompressed));
    EXPECT_TRUE(store.matches_public_key_der(store.public_key_der()));

    EvpPkeyPtr other;
    ASSERT_TRUE(generate_p256_key(other));
    EXPECT_FALSE(store.matches_public_key_der(uncompressed_spki(other.get())));
    EXPECT_FALSE(store.matches_public_key_der({0x30, 0x00}));
    auto trailing = uncompressed;
    trailing.push_back(0x00);
    EXPECT_FALSE(store.matches_public_key_der(trailing));
}

TEST(DppEnrolleeKeyHelpers, ParseRejectsTrailingBytesAndAcceptsUppercase)
{
    EvpPkeyPtr key;
    std::string hex;
    ASSERT_TRUE(generate_p256_key(key));
    ASSERT_TRUE(encode_private_key_hex(key.get(), hex));
    EvpPkeyPtr parsed;
    EXPECT_FALSE(parse_private_key_hex(hex + "00", parsed));
    EXPECT_FALSE(parse_private_key_hex("", parsed));
    std::string upper = hex;
    for (auto &ch : upper) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    EXPECT_TRUE(parse_private_key_hex(upper, parsed));
}

namespace {
// GeneratedQR URI and DPP_BOOTSTRAP_INFO pkhash read from a device (2.4 GHz EndPoint).
// Its K: uses '+', '/' and '=' padding, which an earlier check rejected.
const std::string kDeviceKey =
    "MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgADpY8N5wwYmxHUDzZOMR/dwF0nZ9iou+aeRTL5ly68L8g=";
const std::string kDeviceUri = "DPP:C:81/6;V:3;K:" + kDeviceKey + ";;";
const char kDevicePkhash[]   = "be530b537d1b5aee141e1e131376e27453d2ca50ea5355585bdfdf00e6950167";

std::string sha256_hex(const std::vector<uint8_t> &data)
{
    uint8_t hash[SHA256_DIGEST_LENGTH] = {};
    SHA256(data.data(), data.size(), hash);
    static const char hex_chars[] = "0123456789abcdef";
    std::string out;
    for (auto byte : hash) {
        out.push_back(hex_chars[byte >> 4]);
        out.push_back(hex_chars[byte & 0x0f]);
    }
    return out;
}

// Standard base64 with '=' padding, as hostapd writes K:.
std::string base64(const std::vector<uint8_t> &data)
{
    std::string out(4 * ((data.size() + 2) / 3) + 1, '\0');
    const int len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&out[0]), data.data(),
                                    static_cast<int>(data.size()));
    out.resize(len > 0 ? static_cast<size_t>(len) : 0);
    return out;
}

std::string uri_with_key(const std::string &key) { return "DPP:V:3;K:" + key + ";;"; }
} // namespace

TEST(DppEnrolleeUriKey, DecodesDeviceUriWithStandardBase64)
{
    std::vector<uint8_t> der;
    ASSERT_TRUE(decode_uri_public_key(kDeviceUri, der));
    EXPECT_EQ(der.size(), 59u);
    EXPECT_EQ(sha256_hex(der), kDevicePkhash);
}

TEST_F(DppEnrolleeKeyStoreTest, UriMatchesOwnKeyPaddedUnpaddedAndUrlSafe)
{
    DppEnrolleeKeyStore store(m_path);
    std::string msg;
    ASSERT_TRUE(is_success(store.load_or_create(msg))) << msg;

    const std::string padded = base64(store.public_key_der());
    ASSERT_EQ(padded.size(), 80u);
    ASSERT_EQ(padded.back(), '=');
    EXPECT_TRUE(uri_matches_key(uri_with_key(padded), store));
    EXPECT_TRUE(uri_matches_key("DPP:C:81/6;V:3;K:" + padded + ";;", store));

    std::string unpadded = padded;
    while (!unpadded.empty() && unpadded.back() == '=') {
        unpadded.pop_back();
    }
    EXPECT_TRUE(uri_matches_key(uri_with_key(unpadded), store));

    std::string url_safe = unpadded;
    for (auto &c : url_safe) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    EXPECT_TRUE(uri_matches_key(uri_with_key(url_safe), store));

    // A valid URI with another key must not match.
    EXPECT_FALSE(uri_matches_key(kDeviceUri, store));
}

TEST(DppEnrolleeUriKey, RejectsMalformedUris)
{
    const std::string &k = kDeviceKey;
    std::vector<uint8_t> der;
    EXPECT_TRUE(decode_uri_public_key(uri_with_key(k), der));

    EXPECT_FALSE(decode_uri_public_key("DPP:V:3;;", der));                     // no K:
    EXPECT_FALSE(decode_uri_public_key(uri_with_key(""), der));                // empty K:
    EXPECT_FALSE(decode_uri_public_key("DPP:K:" + k + ";K:" + k + ";;", der)); // repeated K:
    EXPECT_FALSE(decode_uri_public_key("K:" + k + ";;", der));                 // no DPP: prefix
    EXPECT_FALSE(decode_uri_public_key("DPP:K:" + k + ";", der));              // no ;; end
    EXPECT_FALSE(decode_uri_public_key(uri_with_key(k + "=="), der));          // three '='
    EXPECT_FALSE(decode_uri_public_key(uri_with_key(k.substr(0, 8) + "=" + k.substr(9)), der));
    EXPECT_FALSE(decode_uri_public_key(uri_with_key(k.substr(0, 8) + "*" + k.substr(9)), der));
    EXPECT_TRUE(der.empty());
}
