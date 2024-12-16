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

#include <tlvf/airties/tlvVersionReporting.h>
#include <tlvf/tlvflogging.h>

using namespace airties;

tlvVersionReporting::tlvVersionReporting(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvVersionReporting::tlvVersionReporting(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvVersionReporting::~tlvVersionReporting() {
}
const eAirtiesTlvTypeMap& tlvVersionReporting::type() {
    return (const eAirtiesTlvTypeMap&)(*m_type);
}

const uint16_t& tlvVersionReporting::length() {
    return (const uint16_t&)(*m_length);
}

sVendorOUI& tlvVersionReporting::vendor_oui() {
    return (sVendorOUI&)(*m_vendor_oui);
}

uint16_t& tlvVersionReporting::tlv_id() {
    return (uint16_t&)(*m_tlv_id);
}

uint32_t& tlvVersionReporting::em_agent_version() {
    return (uint32_t&)(*m_em_agent_version);
}

uint16_t& tlvVersionReporting::em_agent_feature_list_length() {
    return (uint16_t&)(*m_em_agent_feature_list_length);
}

std::tuple<bool, cLocalInterfaceInfo&> tlvVersionReporting::em_agent_feature_list(size_t idx) {
    bool ret_success = ( (m_em_agent_feature_list_idx__ > 0) && (m_em_agent_feature_list_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_em_agent_feature_list_vector[ret_idx]));
}

std::shared_ptr<cLocalInterfaceInfo> tlvVersionReporting::create_em_agent_feature_list() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list em_agent_feature_list, abort!";
        return nullptr;
    }
    size_t len = cLocalInterfaceInfo::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_em_agent_feature_list;
    if (m_em_agent_feature_list_idx__ > 0) {
        src = (uint8_t *)m_em_agent_feature_list_vector[m_em_agent_feature_list_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cLocalInterfaceInfo>(src, getBuffRemainingBytes(src), m_parse__);
}

bool tlvVersionReporting::add_em_agent_feature_list(std::shared_ptr<cLocalInterfaceInfo> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_em_agent_feature_list was called before add_em_agent_feature_list";
        return false;
    }
    uint8_t *src = (uint8_t *)m_em_agent_feature_list;
    if (m_em_agent_feature_list_idx__ > 0) {
        src = (uint8_t *)m_em_agent_feature_list_vector[m_em_agent_feature_list_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_em_agent_feature_list_idx__++;
    if (!m_parse__) { (*m_em_agent_feature_list_length)++; }
    size_t len = ptr->getLen();
    m_em_agent_feature_list_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(!m_parse__ && m_length){ (*m_length) += len; }
    m_lock_allocation__ = false;
    return true;
}

void tlvVersionReporting::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    m_vendor_oui->struct_swap();
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_tlv_id));
    tlvf_swap(32, reinterpret_cast<uint8_t*>(m_em_agent_version));
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_em_agent_feature_list_length));
    for (size_t i = 0; i < m_em_agent_feature_list_idx__; i++){
        std::get<1>(em_agent_feature_list(i)).class_swap();
    }
}

bool tlvVersionReporting::finalize()
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

size_t tlvVersionReporting::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eAirtiesTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(sVendorOUI); // vendor_oui
    class_size += sizeof(uint16_t); // tlv_id
    class_size += sizeof(uint32_t); // em_agent_version
    class_size += sizeof(uint16_t); // em_agent_feature_list_length
    return class_size;
}

bool tlvVersionReporting::init()
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
    m_em_agent_version = reinterpret_cast<uint32_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint32_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint32_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint32_t); }
    m_em_agent_feature_list_length = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!m_parse__) *m_em_agent_feature_list_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint16_t); }
    m_em_agent_feature_list = reinterpret_cast<cLocalInterfaceInfo*>(m_buff_ptr__);
    uint16_t em_agent_feature_list_length = *m_em_agent_feature_list_length;
    if (m_parse__) {  tlvf_swap(16, reinterpret_cast<uint8_t*>(&em_agent_feature_list_length)); }
    m_em_agent_feature_list_idx__ = 0;
    for (size_t i = 0; i < em_agent_feature_list_length; i++) {
        auto em_agent_feature_list = create_em_agent_feature_list();
        if (!em_agent_feature_list || !em_agent_feature_list->isInitialized()) {
            TLVF_LOG(ERROR) << "create_em_agent_feature_list() failed";
            return false;
        }
        if (!add_em_agent_feature_list(em_agent_feature_list)) {
            TLVF_LOG(ERROR) << "add_em_agent_feature_list() failed";
            return false;
        }
        // swap back since em_agent_feature_list will be swapped as part of the whole class swap
        em_agent_feature_list->class_swap();
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

cLocalInterfaceInfo::cLocalInterfaceInfo(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cLocalInterfaceInfo::cLocalInterfaceInfo(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cLocalInterfaceInfo::~cLocalInterfaceInfo() {
}
uint32_t& cLocalInterfaceInfo::feature_info() {
    return (uint32_t&)(*m_feature_info);
}

void cLocalInterfaceInfo::class_swap()
{
    tlvf_swap(32, reinterpret_cast<uint8_t*>(m_feature_info));
}

bool cLocalInterfaceInfo::finalize()
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

size_t cLocalInterfaceInfo::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(uint32_t); // feature_info
    return class_size;
}

bool cLocalInterfaceInfo::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_feature_info = reinterpret_cast<uint32_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint32_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint32_t) << ") Failed!";
        return false;
    }
    if (m_parse__) { class_swap(); }
    return true;
}


