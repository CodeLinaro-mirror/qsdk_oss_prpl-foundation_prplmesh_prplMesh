/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BaseClass_H_
#define _BaseClass_H_

#include <cstddef>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>

/**
 * @brief Marks a generated codec method as library-local.
 *
 * The generated classes export roughly 12800 symbols between libtlvf and
 * libbtlvf, and the resulting .dynstr/.dynsym/.hash are a large fraction of
 * both libraries. init() is private and only ever called from the generated
 * constructor in the same translation unit, so it needs no visibility outside
 * the library defining it.
 *
 * finalize() and class_swap() must NOT be marked: callers outside the codec
 * libraries invoke them directly on a concrete type (e.g.
 * beerocks::CmduUtils::verify_cmdu calls tlvVendorSpecific::class_swap), so
 * hiding either breaks the link.
 */
#if defined(TLVF_FLASH_OPTIMIZATION) && defined(__GNUC__) && __GNUC__ >= 4
#define TLVF_LOCAL __attribute__((visibility("hidden")))
#else
#define TLVF_LOCAL
#endif

class ClassList;

class BaseClass {
protected:
    BaseClass(uint8_t *buff_, const size_t buff_len_, const bool parse_ = false);
    virtual ~BaseClass() = default;

public:
    uint8_t *getBuffPtr();
    uint8_t *getStartBuffPtr();
    size_t getBuffRemainingBytes(void *start = nullptr);
    bool buffPtrIncrementSafe(size_t length);

    /**
     * @brief buffPtrIncrementSafe, logging the failure.
     */
    bool buffPtrIncrementSafeLogged(size_t length);

    /**
     * @brief Whether the buffer still holds initial_size bytes, logging if not.
     */
    bool hasInitialSpace(size_t initial_size);

    /**
     * @brief Whether length bytes remain from start (default: the current
     * pointer), logging if not.
     */
    bool hasSpaceFor(size_t length, void *start = nullptr);

    /**
     * @brief hasSpaceFor for the alloc paths, which log a distinct message.
     */
    bool hasSpaceForAlloc(size_t length);

    size_t getLen();
    bool isInitialized();
    virtual bool isPostInitSucceeded() { return true; };
    virtual void class_swap() = 0;
    virtual bool finalize()   = 0;
    bool is_finalized() { return m_finalized__; };
    void addInnerClassList(std::shared_ptr<ClassList> list) { m_inner__ = list; };
    std::shared_ptr<ClassList> getInnerClassList() { return m_inner__; };
    /**
     * @brief cast class
     *
     * Cast self to std::shared_ptr<T> which will first swap the whole class back
     * to the original byte order, then create a shared_ptr<T> on the buffer.
     * Used only in parsing.
     *
     * @tparam T template type to cast to
     * @return std::shared_ptr<T> pointer to the casted class
     */
    template <class T> std::shared_ptr<T> class_cast()
    {
        if (m_parse__) {
            class_swap();
            return std::make_shared<T>(m_buff__, m_buff_len__, true);
        }
        return nullptr;
    }

protected:
    uint8_t *m_buff__;
    uint8_t *m_buff_ptr__;
    const size_t m_buff_len__;
    const bool m_parse__;
    bool m_finalized__    = false;
    bool m_init_succeeded = false;
    std::shared_ptr<ClassList> m_inner__;
};

#endif //_BaseClass_H_
