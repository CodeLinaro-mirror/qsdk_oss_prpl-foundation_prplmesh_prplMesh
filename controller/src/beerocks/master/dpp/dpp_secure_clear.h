/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _CONTROLLER_DPP_SECURE_CLEAR_H_
#define _CONTROLLER_DPP_SECURE_CLEAR_H_

#include <openssl/crypto.h>

#include <cstdint>
#include <vector>

namespace son {
namespace controller_dpp {

/**
 * @brief Irretrievably delete a secret held in a byte vector.
 *
 * std::vector::clear() only resets size(); the key bytes stay in the heap
 * block until it is reused. This overwrites the whole allocation with
 * OPENSSL_cleanse() (which the compiler may not elide) and then releases it.
 * The full capacity is wiped because a shrink (resize/assign) leaves the old
 * bytes beyond size().
 *
 * Used for the DPP Authentication secrets that Wi-Fi Easy Connect v3.0
 * section 6.3.4 requires to be irretrievably deleted.
 */
template <class Allocator> void secure_clear(std::vector<uint8_t, Allocator> &secret)
{
    if (secret.capacity() > 0) {
        // Growing up to capacity() never reallocates, so data() spans the whole block.
        secret.resize(secret.capacity());
        OPENSSL_cleanse(secret.data(), secret.size());
    }
    secret.clear();
    // Swap with an empty vector to release the (now zeroed) block.
    std::vector<uint8_t, Allocator>(secret.get_allocator()).swap(secret);
}

/**
 * @brief Calls secure_clear() on a local secret when the enclosing scope
 * exits, so every return path wipes it.
 */
class SecureClearGuard {
public:
    explicit SecureClearGuard(std::vector<uint8_t> &secret) : m_secret(secret) {}
    ~SecureClearGuard() { secure_clear(m_secret); }
    SecureClearGuard(const SecureClearGuard &) = delete;
    SecureClearGuard &operator=(const SecureClearGuard &) = delete;

private:
    std::vector<uint8_t> &m_secret;
};

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_SECURE_CLEAR_H_
