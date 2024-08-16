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

#include <tlvf/sample_vendor/tlvSampleVendor.h>
#include <tlvf/tlvflogging.h>

using namespace sample_vendor;

tlvSampleVendor::tlvSampleVendor(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvSampleVendor::tlvSampleVendor(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvSampleVendor::~tlvSampleVendor() {
}
const eSampleVendorTlvTypeMap& tlvSampleVendor::type() {
    return (const eSampleVendorTlvTypeMap&)(*m_type);
}

const uint16_t& tlvSampleVendor::length() {
    return (const uint16_t&)(*m_length);
}

sVendorOUI& tlvSampleVendor::vendor_oui() {
    return (sVendorOUI&)(*m_vendor_oui);
}

void tlvSampleVendor::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    m_vendor_oui->struct_swap();
}

bool tlvSampleVendor::finalize()
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

size_t tlvSampleVendor::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eSampleVendorTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(sVendorOUI); // vendor_oui
    return class_size;
}

bool tlvSampleVendor::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_type = reinterpret_cast<eSampleVendorTlvTypeMap*>(m_buff_ptr__);
    if (!m_parse__) *m_type = eSampleVendorTlvTypeMap::TLV_VENDOR_SPECIFIC;
    if (!buffPtrIncrementSafe(sizeof(eSampleVendorTlvTypeMap))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(eSampleVendorTlvTypeMap) << ") Failed!";
        return false;
    }
    m_length = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!m_parse__) *m_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    m_vendor_oui = reinterpret_cast<sVendorOUI*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sVendorOUI))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sVendorOUI) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(sVendorOUI); }
    if (!m_parse__) { m_vendor_oui->struct_init(); }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eSampleVendorTlvTypeMap::TLV_VENDOR_SPECIFIC) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eSampleVendorTlvTypeMap::TLV_VENDOR_SPECIFIC) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}


