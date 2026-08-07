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

#include <tlvf/ieee_1905_1/tlvDeviceIdentification.h>
#include <tlvf/tlvflogging.h>

using namespace ieee1905_1;

tlvDeviceIdentification::tlvDeviceIdentification(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvDeviceIdentification::tlvDeviceIdentification(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvDeviceIdentification::~tlvDeviceIdentification() {
}
const eTlvType& tlvDeviceIdentification::type() {
    return (const eTlvType&)(*m_type);
}

const uint16_t& tlvDeviceIdentification::length() {
    return (const uint16_t&)(*m_length);
}

uint8_t* tlvDeviceIdentification::friendly_name(size_t idx) {
    if ( (m_friendly_name_idx__ == 0) || (m_friendly_name_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_friendly_name[idx]);
}

bool tlvDeviceIdentification::set_friendly_name(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_friendly_name received a null pointer.";
        return false;
    }
    if (size > 64) {
        TLVF_LOG(ERROR) << "Received buffer size is smaller than buffer length";
        return false;
    }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_friendly_name);
    return true;
}
uint8_t* tlvDeviceIdentification::manufacturer_name(size_t idx) {
    if ( (m_manufacturer_name_idx__ == 0) || (m_manufacturer_name_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_manufacturer_name[idx]);
}

bool tlvDeviceIdentification::set_manufacturer_name(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_manufacturer_name received a null pointer.";
        return false;
    }
    if (size > 64) {
        TLVF_LOG(ERROR) << "Received buffer size is smaller than buffer length";
        return false;
    }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_manufacturer_name);
    return true;
}
uint8_t* tlvDeviceIdentification::manufacturer_model(size_t idx) {
    if ( (m_manufacturer_model_idx__ == 0) || (m_manufacturer_model_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_manufacturer_model[idx]);
}

bool tlvDeviceIdentification::set_manufacturer_model(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_manufacturer_model received a null pointer.";
        return false;
    }
    if (size > 64) {
        TLVF_LOG(ERROR) << "Received buffer size is smaller than buffer length";
        return false;
    }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_manufacturer_model);
    return true;
}
void tlvDeviceIdentification::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
}

bool tlvDeviceIdentification::finalize()
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

size_t tlvDeviceIdentification::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eTlvType); // type
    class_size += sizeof(uint16_t); // length
    class_size += 64 * sizeof(uint8_t); // friendly_name
    class_size += 64 * sizeof(uint8_t); // manufacturer_name
    class_size += 64 * sizeof(uint8_t); // manufacturer_model
    return class_size;
}

bool tlvDeviceIdentification::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_type = reinterpret_cast<eTlvType*>(m_buff_ptr__);
    if (!m_parse__) *m_type = eTlvType::TLV_DEVICE_IDENTIFICATION;
    if (!buffPtrIncrementSafe(sizeof(eTlvType))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(eTlvType) << ") Failed!";
        return false;
    }
    m_length = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!m_parse__) *m_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    m_friendly_name = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (64))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (64) << ") Failed!";
        return false;
    }
    m_friendly_name_idx__  = 64;
    if (!m_parse__) {
        if (m_length) { (*m_length) += (sizeof(uint8_t) * 64); }
    }
    m_manufacturer_name = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (64))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (64) << ") Failed!";
        return false;
    }
    m_manufacturer_name_idx__  = 64;
    if (!m_parse__) {
        if (m_length) { (*m_length) += (sizeof(uint8_t) * 64); }
    }
    m_manufacturer_model = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (64))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (64) << ") Failed!";
        return false;
    }
    m_manufacturer_model_idx__  = 64;
    if (!m_parse__) {
        if (m_length) { (*m_length) += (sizeof(uint8_t) * 64); }
    }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eTlvType::TLV_DEVICE_IDENTIFICATION) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eTlvType::TLV_DEVICE_IDENTIFICATION) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}


