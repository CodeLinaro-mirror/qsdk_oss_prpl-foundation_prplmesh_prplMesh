///////////////////////////////////////
// AUTO GENERATED FILE - DO NOT EDIT //
///////////////////////////////////////

/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <tlvf/wfa_map/tlvSupportedCipherSuites.h>
#include <tlvf/tlvflogging.h>

using namespace wfa_map;

tlvSupportedCipherSuites::tlvSupportedCipherSuites(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvSupportedCipherSuites::tlvSupportedCipherSuites(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvSupportedCipherSuites::~tlvSupportedCipherSuites() {
}
const eTlvTypeMap& tlvSupportedCipherSuites::type() {
    return (const eTlvTypeMap&)(*m_type);
}

const uint16_t& tlvSupportedCipherSuites::length() {
    return (const uint16_t&)(*m_length);
}

uint8_t& tlvSupportedCipherSuites::number_of_cipher_suite_selectors() {
    return (uint8_t&)(*m_number_of_cipher_suite_selectors);
}

std::tuple<bool, tlvSupportedCipherSuites::sCipherSuiteSelector&> tlvSupportedCipherSuites::cipher_suite_selectors(size_t idx) {
    bool ret_success = ( (m_cipher_suite_selectors_idx__ > 0) && (m_cipher_suite_selectors_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, m_cipher_suite_selectors[ret_idx]);
}

bool tlvSupportedCipherSuites::alloc_cipher_suite_selectors(size_t count) {
    if (m_lock_order_counter__ > 0) {;
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list cipher_suite_selectors, abort!";
        return false;
    }
    size_t len = sizeof(sCipherSuiteSelector) * count;
    if(getBuffRemainingBytes() < len )  {
        TLVF_LOG(ERROR) << "Not enough available space on buffer - can't allocate";
        return false;
    }
    m_lock_order_counter__ = 0;
    uint8_t *src = (uint8_t *)&m_cipher_suite_selectors[*m_number_of_cipher_suite_selectors];
    uint8_t *dst = src + len;
    if (!m_parse__) {
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_cipher_suite_selectors_idx__ += count;
    *m_number_of_cipher_suite_selectors += count;
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(m_length){ (*m_length) += len; }
    if (!m_parse__) { 
        for (size_t i = m_cipher_suite_selectors_idx__ - count; i < m_cipher_suite_selectors_idx__; i++) { m_cipher_suite_selectors[i].struct_init(); }
    }
    return true;
}

void tlvSupportedCipherSuites::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    for (size_t i = 0; i < m_cipher_suite_selectors_idx__; i++){
        m_cipher_suite_selectors[i].struct_swap();
    }
}

bool tlvSupportedCipherSuites::finalize()
{
    if (m_parse__) {
        TLVF_LOG(DEBUG) << "finalize() called but m_parse__ is set";
        return true;
    }
    if (m_finalized__) {
        TLVF_LOG(DEBUG) << "finalize() called for already finalized class";
        return true;
    }
    if (!isPostInitSucceeded()) {
        TLVF_LOG(ERROR) << "post init check failed";
        return false;
    }
    if (m_inner__) {
        if (!m_inner__->finalize()) {
            TLVF_LOG(ERROR) << "m_inner__->finalize() failed";
            return false;
        }
        auto tailroom = m_inner__->getMessageBuffLength() - m_inner__->getMessageLength();
        m_buff_ptr__ -= tailroom;
        *m_length -= tailroom;
    }
    class_swap();
    m_finalized__ = true;
    return true;
}

size_t tlvSupportedCipherSuites::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(uint8_t); // number_of_cipher_suite_selectors
    return class_size;
}

bool tlvSupportedCipherSuites::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_type = reinterpret_cast<eTlvTypeMap*>(m_buff_ptr__);
    if (!m_parse__) *m_type = eTlvTypeMap::TLV_SUPPORTED_CIPHER_SUITES;
    if (!buffPtrIncrementSafe(sizeof(eTlvTypeMap))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(eTlvTypeMap) << ") Failed!";
        return false;
    }
    m_length = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!m_parse__) *m_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    m_number_of_cipher_suite_selectors = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_number_of_cipher_suite_selectors = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_cipher_suite_selectors = reinterpret_cast<sCipherSuiteSelector*>(m_buff_ptr__);
    uint8_t number_of_cipher_suite_selectors = *m_number_of_cipher_suite_selectors;
    m_cipher_suite_selectors_idx__ = number_of_cipher_suite_selectors;
    if (!buffPtrIncrementSafe(sizeof(sCipherSuiteSelector) * (number_of_cipher_suite_selectors))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sCipherSuiteSelector) * (number_of_cipher_suite_selectors) << ") Failed!";
        return false;
    }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eTlvTypeMap::TLV_SUPPORTED_CIPHER_SUITES) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eTlvTypeMap::TLV_SUPPORTED_CIPHER_SUITES) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}


