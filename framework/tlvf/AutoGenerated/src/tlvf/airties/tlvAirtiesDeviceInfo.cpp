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

#include <tlvf/airties/tlvAirtiesDeviceInfo.h>
#include <tlvf/tlvflogging.h>

using namespace airties;

tlvAirtiesDeviceInfo::tlvAirtiesDeviceInfo(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvAirtiesDeviceInfo::tlvAirtiesDeviceInfo(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvAirtiesDeviceInfo::~tlvAirtiesDeviceInfo() {
}
const eAirtiesTlvTypeMap& tlvAirtiesDeviceInfo::type() {
    return (const eAirtiesTlvTypeMap&)(*m_type);
}

const uint16_t& tlvAirtiesDeviceInfo::length() {
    return (const uint16_t&)(*m_length);
}

sVendorOUI& tlvAirtiesDeviceInfo::vendor_oui() {
    return (sVendorOUI&)(*m_vendor_oui);
}

uint16_t& tlvAirtiesDeviceInfo::tlv_id() {
    return (uint16_t&)(*m_tlv_id);
}

uint32_t& tlvAirtiesDeviceInfo::boot_id() {
    return (uint32_t&)(*m_boot_id);
}

uint8_t& tlvAirtiesDeviceInfo::client_id_length() {
    return (uint8_t&)(*m_client_id_length);
}

std::string tlvAirtiesDeviceInfo::client_id_str() {
    char *client_id_ = client_id();
    if (!client_id_) { return std::string(); }
    auto str = std::string(client_id_, m_client_id_idx__);
    auto pos = str.find_first_of('\0');
    if (pos != std::string::npos) {
        str.erase(pos);
    }
    return str;
}

char* tlvAirtiesDeviceInfo::client_id(size_t length) {
    if( (m_client_id_idx__ == 0) || (m_client_id_idx__ < length) ) {
        TLVF_LOG(ERROR) << "client_id length is smaller than requested length";
        return nullptr;
    }
    return ((char*)m_client_id);
}

bool tlvAirtiesDeviceInfo::set_client_id(const std::string& str) { return set_client_id(str.c_str(), str.size()); }
bool tlvAirtiesDeviceInfo::set_client_id(const char str[], size_t size) {
    if (str == nullptr) {
        TLVF_LOG(WARNING) << "set_client_id received a null pointer.";
        return false;
    }
    if (m_client_id_idx__ != 0) {
        TLVF_LOG(ERROR) << "set_client_id was already allocated!";
        return false;
    }
    if (!alloc_client_id(size)) { return false; }
    std::copy(str, str + size, m_client_id);
    return true;
}
bool tlvAirtiesDeviceInfo::alloc_client_id(size_t count) {
    if (m_lock_order_counter__ > 0) {;
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list client_id, abort!";
        return false;
    }
    size_t len = sizeof(char) * count;
    if(getBuffRemainingBytes() < len )  {
        TLVF_LOG(ERROR) << "Not enough available space on buffer - can't allocate";
        return false;
    }
    m_lock_order_counter__ = 0;
    uint8_t *src = (uint8_t *)&m_client_id[*m_client_id_length];
    uint8_t *dst = src + len;
    if (!m_parse__) {
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_client_secret_length = (uint8_t *)((uint8_t *)(m_client_secret_length) + len);
    m_client_secret = (char *)((uint8_t *)(m_client_secret) + len);
    m_flags1 = (sFlags1 *)((uint8_t *)(m_flags1) + len);
    m_flags2 = (sFlags2 *)((uint8_t *)(m_flags2) + len);
    m_client_id_idx__ += count;
    *m_client_id_length += count;
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(m_length){ (*m_length) += len; }
    return true;
}

uint8_t& tlvAirtiesDeviceInfo::client_secret_length() {
    return (uint8_t&)(*m_client_secret_length);
}

std::string tlvAirtiesDeviceInfo::client_secret_str() {
    char *client_secret_ = client_secret();
    if (!client_secret_) { return std::string(); }
    auto str = std::string(client_secret_, m_client_secret_idx__);
    auto pos = str.find_first_of('\0');
    if (pos != std::string::npos) {
        str.erase(pos);
    }
    return str;
}

char* tlvAirtiesDeviceInfo::client_secret(size_t length) {
    if( (m_client_secret_idx__ == 0) || (m_client_secret_idx__ < length) ) {
        TLVF_LOG(ERROR) << "client_secret length is smaller than requested length";
        return nullptr;
    }
    return ((char*)m_client_secret);
}

bool tlvAirtiesDeviceInfo::set_client_secret(const std::string& str) { return set_client_secret(str.c_str(), str.size()); }
bool tlvAirtiesDeviceInfo::set_client_secret(const char str[], size_t size) {
    if (str == nullptr) {
        TLVF_LOG(WARNING) << "set_client_secret received a null pointer.";
        return false;
    }
    if (m_client_secret_idx__ != 0) {
        TLVF_LOG(ERROR) << "set_client_secret was already allocated!";
        return false;
    }
    if (!alloc_client_secret(size)) { return false; }
    std::copy(str, str + size, m_client_secret);
    return true;
}
bool tlvAirtiesDeviceInfo::alloc_client_secret(size_t count) {
    if (m_lock_order_counter__ > 1) {;
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list client_secret, abort!";
        return false;
    }
    size_t len = sizeof(char) * count;
    if(getBuffRemainingBytes() < len )  {
        TLVF_LOG(ERROR) << "Not enough available space on buffer - can't allocate";
        return false;
    }
    m_lock_order_counter__ = 1;
    uint8_t *src = (uint8_t *)&m_client_secret[*m_client_secret_length];
    uint8_t *dst = src + len;
    if (!m_parse__) {
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_flags1 = (sFlags1 *)((uint8_t *)(m_flags1) + len);
    m_flags2 = (sFlags2 *)((uint8_t *)(m_flags2) + len);
    m_client_secret_idx__ += count;
    *m_client_secret_length += count;
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(m_length){ (*m_length) += len; }
    return true;
}

tlvAirtiesDeviceInfo::sFlags1& tlvAirtiesDeviceInfo::flags1() {
    return (sFlags1&)(*m_flags1);
}

tlvAirtiesDeviceInfo::sFlags2& tlvAirtiesDeviceInfo::flags2() {
    return (sFlags2&)(*m_flags2);
}

void tlvAirtiesDeviceInfo::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    m_vendor_oui->struct_swap();
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_tlv_id));
    tlvf_swap(32, reinterpret_cast<uint8_t*>(m_boot_id));
    m_flags1->struct_swap();
    m_flags2->struct_swap();
}

bool tlvAirtiesDeviceInfo::finalize()
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

size_t tlvAirtiesDeviceInfo::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eAirtiesTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(sVendorOUI); // vendor_oui
    class_size += sizeof(uint16_t); // tlv_id
    class_size += sizeof(uint32_t); // boot_id
    class_size += sizeof(uint8_t); // client_id_length
    class_size += sizeof(uint8_t); // client_secret_length
    class_size += sizeof(sFlags1); // flags1
    class_size += sizeof(sFlags2); // flags2
    return class_size;
}

bool tlvAirtiesDeviceInfo::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_type = reinterpret_cast<eAirtiesTlvTypeMap*>(m_buff_ptr__);
    if (!m_parse__) *m_type = eAirtiesTlvTypeMap::TLV_VENDOR_SPECIFIC;
    if (!buffPtrIncrementSafe(sizeof(eAirtiesTlvTypeMap))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(eAirtiesTlvTypeMap) << ") Failed!";
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
    m_tlv_id = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint16_t); }
    m_boot_id = reinterpret_cast<uint32_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint32_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint32_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint32_t); }
    m_client_id_length = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_client_id_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_client_id = reinterpret_cast<char*>(m_buff_ptr__);
    uint8_t client_id_length = *m_client_id_length;
    m_client_id_idx__ = client_id_length;
    if (!buffPtrIncrementSafe(sizeof(char) * (client_id_length))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(char) * (client_id_length) << ") Failed!";
        return false;
    }
    m_client_secret_length = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_client_secret_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_client_secret = reinterpret_cast<char*>(m_buff_ptr__);
    uint8_t client_secret_length = *m_client_secret_length;
    m_client_secret_idx__ = client_secret_length;
    if (!buffPtrIncrementSafe(sizeof(char) * (client_secret_length))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(char) * (client_secret_length) << ") Failed!";
        return false;
    }
    m_flags1 = reinterpret_cast<sFlags1*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sFlags1))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sFlags1) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(sFlags1); }
    if (!m_parse__) { m_flags1->struct_init(); }
    m_flags2 = reinterpret_cast<sFlags2*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sFlags2))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sFlags2) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(sFlags2); }
    if (!m_parse__) { m_flags2->struct_init(); }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eAirtiesTlvTypeMap::TLV_VENDOR_SPECIFIC) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eAirtiesTlvTypeMap::TLV_VENDOR_SPECIFIC) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}


