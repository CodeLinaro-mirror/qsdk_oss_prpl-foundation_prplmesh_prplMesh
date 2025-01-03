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

#include <tlvf/airties/tlvAirtiesDeviceMetrics.h>
#include <tlvf/tlvflogging.h>

using namespace airties;

tlvAirtiesDeviceMetrics::tlvAirtiesDeviceMetrics(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvAirtiesDeviceMetrics::tlvAirtiesDeviceMetrics(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvAirtiesDeviceMetrics::~tlvAirtiesDeviceMetrics() {
}
const eAirtiesTlvTypeMap& tlvAirtiesDeviceMetrics::type() {
    return (const eAirtiesTlvTypeMap&)(*m_type);
}

const uint16_t& tlvAirtiesDeviceMetrics::length() {
    return (const uint16_t&)(*m_length);
}

sVendorOUI& tlvAirtiesDeviceMetrics::vendor_oui() {
    return (sVendorOUI&)(*m_vendor_oui);
}

uint16_t& tlvAirtiesDeviceMetrics::tlv_id() {
    return (uint16_t&)(*m_tlv_id);
}

uint32_t& tlvAirtiesDeviceMetrics::uptime_to_boot() {
    return (uint32_t&)(*m_uptime_to_boot);
}

uint8_t& tlvAirtiesDeviceMetrics::cpu_loadtime_platform() {
    return (uint8_t&)(*m_cpu_loadtime_platform);
}

uint8_t& tlvAirtiesDeviceMetrics::cpu_temperature() {
    return (uint8_t&)(*m_cpu_temperature);
}

uint32_t& tlvAirtiesDeviceMetrics::platform_totalmemory() {
    return (uint32_t&)(*m_platform_totalmemory);
}

uint32_t& tlvAirtiesDeviceMetrics::platform_freememory() {
    return (uint32_t&)(*m_platform_freememory);
}

uint32_t& tlvAirtiesDeviceMetrics::platform_cachedmemory() {
    return (uint32_t&)(*m_platform_cachedmemory);
}

uint8_t& tlvAirtiesDeviceMetrics::num_of_radios() {
    return (uint8_t&)(*m_num_of_radios);
}

std::tuple<bool, cRadioInfo&> tlvAirtiesDeviceMetrics::radio_list(size_t idx) {
    bool ret_success = ( (m_radio_list_idx__ > 0) && (m_radio_list_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_radio_list_vector[ret_idx]));
}

std::shared_ptr<cRadioInfo> tlvAirtiesDeviceMetrics::create_radio_list() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list radio_list, abort!";
        return nullptr;
    }
    size_t len = cRadioInfo::get_initial_size();
    if (m_lock_allocation__) {
        TLVF_LOG(ERROR) << "Can't create new element before adding the previous one";
        return nullptr;
    }
    if (getBuffRemainingBytes() < len) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return nullptr;
    }
    m_lock_order_counter__ = 0;
    m_lock_allocation__ = true;
    uint8_t *src = (uint8_t *)m_radio_list;
    if (m_radio_list_idx__ > 0) {
        src = (uint8_t *)m_radio_list_vector[m_radio_list_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cRadioInfo>(src, getBuffRemainingBytes(src), m_parse__);
}

bool tlvAirtiesDeviceMetrics::add_radio_list(std::shared_ptr<cRadioInfo> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_radio_list was called before add_radio_list";
        return false;
    }
    uint8_t *src = (uint8_t *)m_radio_list;
    if (m_radio_list_idx__ > 0) {
        src = (uint8_t *)m_radio_list_vector[m_radio_list_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_radio_list_idx__++;
    if (!m_parse__) { (*m_num_of_radios)++; }
    size_t len = ptr->getLen();
    m_radio_list_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(!m_parse__ && m_length){ (*m_length) += len; }
    m_lock_allocation__ = false;
    return true;
}

void tlvAirtiesDeviceMetrics::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    m_vendor_oui->struct_swap();
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_tlv_id));
    tlvf_swap(32, reinterpret_cast<uint8_t*>(m_uptime_to_boot));
    tlvf_swap(32, reinterpret_cast<uint8_t*>(m_platform_totalmemory));
    tlvf_swap(32, reinterpret_cast<uint8_t*>(m_platform_freememory));
    tlvf_swap(32, reinterpret_cast<uint8_t*>(m_platform_cachedmemory));
    for (size_t i = 0; i < m_radio_list_idx__; i++){
        std::get<1>(radio_list(i)).class_swap();
    }
}

bool tlvAirtiesDeviceMetrics::finalize()
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

size_t tlvAirtiesDeviceMetrics::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eAirtiesTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(sVendorOUI); // vendor_oui
    class_size += sizeof(uint16_t); // tlv_id
    class_size += sizeof(uint32_t); // uptime_to_boot
    class_size += sizeof(uint8_t); // cpu_loadtime_platform
    class_size += sizeof(uint8_t); // cpu_temperature
    class_size += sizeof(uint32_t); // platform_totalmemory
    class_size += sizeof(uint32_t); // platform_freememory
    class_size += sizeof(uint32_t); // platform_cachedmemory
    class_size += sizeof(uint8_t); // num_of_radios
    return class_size;
}

bool tlvAirtiesDeviceMetrics::init()
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
    m_uptime_to_boot = reinterpret_cast<uint32_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint32_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint32_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint32_t); }
    m_cpu_loadtime_platform = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_cpu_temperature = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_platform_totalmemory = reinterpret_cast<uint32_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint32_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint32_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint32_t); }
    m_platform_freememory = reinterpret_cast<uint32_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint32_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint32_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint32_t); }
    m_platform_cachedmemory = reinterpret_cast<uint32_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint32_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint32_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint32_t); }
    m_num_of_radios = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_num_of_radios = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_radio_list = reinterpret_cast<cRadioInfo*>(m_buff_ptr__);
    uint8_t num_of_radios = *m_num_of_radios;
    m_radio_list_idx__ = 0;
    for (size_t i = 0; i < num_of_radios; i++) {
        auto radio_list = create_radio_list();
        if (!radio_list || !radio_list->isInitialized()) {
            TLVF_LOG(ERROR) << "create_radio_list() failed";
            return false;
        }
        if (!add_radio_list(radio_list)) {
            TLVF_LOG(ERROR) << "add_radio_list() failed";
            return false;
        }
        // swap back since radio_list will be swapped as part of the whole class swap
        radio_list->class_swap();
    }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eAirtiesTlvTypeMap::TLV_VENDOR_SPECIFIC) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eAirtiesTlvTypeMap::TLV_VENDOR_SPECIFIC) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}

cRadioInfo::cRadioInfo(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cRadioInfo::cRadioInfo(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cRadioInfo::~cRadioInfo() {
}
sMacAddr& cRadioInfo::radio_id() {
    return (sMacAddr&)(*m_radio_id);
}

uint8_t& cRadioInfo::radio_temperature() {
    return (uint8_t&)(*m_radio_temperature);
}

void cRadioInfo::class_swap()
{
    m_radio_id->struct_swap();
}

bool cRadioInfo::finalize()
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
    }
    class_swap();
    m_finalized__ = true;
    return true;
}

size_t cRadioInfo::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(sMacAddr); // radio_id
    class_size += sizeof(uint8_t); // radio_temperature
    return class_size;
}

bool cRadioInfo::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_radio_id = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_radio_id->struct_init(); }
    m_radio_temperature = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if (m_parse__) { class_swap(); }
    return true;
}


