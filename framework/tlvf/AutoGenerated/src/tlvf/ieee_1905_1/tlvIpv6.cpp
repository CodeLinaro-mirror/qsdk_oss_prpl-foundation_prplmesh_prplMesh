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

#include <tlvf/ieee_1905_1/tlvIpv6.h>
#include <tlvf/tlvflogging.h>

using namespace ieee1905_1;

tlvIpv6::tlvIpv6(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvIpv6::tlvIpv6(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvIpv6::~tlvIpv6() {
}
const eTlvType& tlvIpv6::type() {
    return (const eTlvType&)(*m_type);
}

const uint16_t& tlvIpv6::length() {
    return (const uint16_t&)(*m_length);
}

uint8_t& tlvIpv6::number_of_entries() {
    return (uint8_t&)(*m_number_of_entries);
}

std::tuple<bool, cIpv6InterfaceBlock&> tlvIpv6::ipv6_interfaces_list(size_t idx) {
    bool ret_success = ( (m_ipv6_interfaces_list_idx__ > 0) && (m_ipv6_interfaces_list_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_ipv6_interfaces_list_vector[ret_idx]));
}

std::shared_ptr<cIpv6InterfaceBlock> tlvIpv6::create_ipv6_interfaces_list() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list ipv6_interfaces_list, abort!";
        return nullptr;
    }
    size_t len = cIpv6InterfaceBlock::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_ipv6_interfaces_list;
    if (m_ipv6_interfaces_list_idx__ > 0) {
        src = (uint8_t *)m_ipv6_interfaces_list_vector[m_ipv6_interfaces_list_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cIpv6InterfaceBlock>(src, getBuffRemainingBytes(src), m_parse__);
}

bool tlvIpv6::add_ipv6_interfaces_list(std::shared_ptr<cIpv6InterfaceBlock> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_ipv6_interfaces_list was called before add_ipv6_interfaces_list";
        return false;
    }
    uint8_t *src = (uint8_t *)m_ipv6_interfaces_list;
    if (m_ipv6_interfaces_list_idx__ > 0) {
        src = (uint8_t *)m_ipv6_interfaces_list_vector[m_ipv6_interfaces_list_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_ipv6_interfaces_list_idx__++;
    if (!m_parse__) { (*m_number_of_entries)++; }
    size_t len = ptr->getLen();
    m_ipv6_interfaces_list_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(!m_parse__ && m_length){ (*m_length) += len; }
    m_lock_allocation__ = false;
    return true;
}

void tlvIpv6::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    for (size_t i = 0; i < m_ipv6_interfaces_list_idx__; i++){
        std::get<1>(ipv6_interfaces_list(i)).class_swap();
    }
}

bool tlvIpv6::finalize()
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

size_t tlvIpv6::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eTlvType); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(uint8_t); // number_of_entries
    return class_size;
}

bool tlvIpv6::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_type = reinterpret_cast<eTlvType*>(m_buff_ptr__);
    if (!m_parse__) *m_type = eTlvType::TLV_IPV6;
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
    m_number_of_entries = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_number_of_entries = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_ipv6_interfaces_list = reinterpret_cast<cIpv6InterfaceBlock*>(m_buff_ptr__);
    uint8_t number_of_entries = *m_number_of_entries;
    m_ipv6_interfaces_list_idx__ = 0;
    for (size_t i = 0; i < number_of_entries; i++) {
        auto ipv6_interfaces_list = create_ipv6_interfaces_list();
        if (!ipv6_interfaces_list || !ipv6_interfaces_list->isInitialized()) {
            TLVF_LOG(ERROR) << "create_ipv6_interfaces_list() failed";
            return false;
        }
        if (!add_ipv6_interfaces_list(ipv6_interfaces_list)) {
            TLVF_LOG(ERROR) << "add_ipv6_interfaces_list() failed";
            return false;
        }
        // swap back since ipv6_interfaces_list will be swapped as part of the whole class swap
        ipv6_interfaces_list->class_swap();
    }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eTlvType::TLV_IPV6) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eTlvType::TLV_IPV6) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}

cIpv6InterfaceBlock::cIpv6InterfaceBlock(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cIpv6InterfaceBlock::cIpv6InterfaceBlock(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cIpv6InterfaceBlock::~cIpv6InterfaceBlock() {
}
sMacAddr& cIpv6InterfaceBlock::mac_address() {
    return (sMacAddr&)(*m_mac_address);
}

uint8_t* cIpv6InterfaceBlock::ipv6_link_local_address(size_t idx) {
    if ( (m_ipv6_link_local_address_idx__ == 0) || (m_ipv6_link_local_address_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_ipv6_link_local_address[idx]);
}

bool cIpv6InterfaceBlock::set_ipv6_link_local_address(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_ipv6_link_local_address received a null pointer.";
        return false;
    }
    if (size > 16) {
        TLVF_LOG(ERROR) << "Received buffer size is smaller than buffer length";
        return false;
    }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_ipv6_link_local_address);
    return true;
}
uint8_t& cIpv6InterfaceBlock::number_of_ipv6_addresses() {
    return (uint8_t&)(*m_number_of_ipv6_addresses);
}

std::tuple<bool, cIpv6InterfaceBlock::sIpv6AddressEntry&> cIpv6InterfaceBlock::ipv6_address_entries(size_t idx) {
    bool ret_success = ( (m_ipv6_address_entries_idx__ > 0) && (m_ipv6_address_entries_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, m_ipv6_address_entries[ret_idx]);
}

bool cIpv6InterfaceBlock::alloc_ipv6_address_entries(size_t count) {
    if (m_lock_order_counter__ > 0) {;
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list ipv6_address_entries, abort!";
        return false;
    }
    size_t len = sizeof(sIpv6AddressEntry) * count;
    if(getBuffRemainingBytes() < len )  {
        TLVF_LOG(ERROR) << "Not enough available space on buffer - can't allocate";
        return false;
    }
    m_lock_order_counter__ = 0;
    uint8_t *src = (uint8_t *)&m_ipv6_address_entries[*m_number_of_ipv6_addresses];
    uint8_t *dst = src + len;
    if (!m_parse__) {
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_ipv6_address_entries_idx__ += count;
    *m_number_of_ipv6_addresses += count;
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if (!m_parse__) { 
        for (size_t i = m_ipv6_address_entries_idx__ - count; i < m_ipv6_address_entries_idx__; i++) { m_ipv6_address_entries[i].struct_init(); }
    }
    return true;
}

void cIpv6InterfaceBlock::class_swap()
{
    m_mac_address->struct_swap();
    for (size_t i = 0; i < m_ipv6_address_entries_idx__; i++){
        m_ipv6_address_entries[i].struct_swap();
    }
}

bool cIpv6InterfaceBlock::finalize()
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

size_t cIpv6InterfaceBlock::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(sMacAddr); // mac_address
    class_size += 16 * sizeof(uint8_t); // ipv6_link_local_address
    class_size += sizeof(uint8_t); // number_of_ipv6_addresses
    return class_size;
}

bool cIpv6InterfaceBlock::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_mac_address = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_mac_address->struct_init(); }
    m_ipv6_link_local_address = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (16))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (16) << ") Failed!";
        return false;
    }
    m_ipv6_link_local_address_idx__  = 16;
    m_number_of_ipv6_addresses = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_number_of_ipv6_addresses = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_ipv6_address_entries = reinterpret_cast<sIpv6AddressEntry*>(m_buff_ptr__);
    uint8_t number_of_ipv6_addresses = *m_number_of_ipv6_addresses;
    m_ipv6_address_entries_idx__ = number_of_ipv6_addresses;
    if (!buffPtrIncrementSafe(sizeof(sIpv6AddressEntry) * (number_of_ipv6_addresses))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sIpv6AddressEntry) * (number_of_ipv6_addresses) << ") Failed!";
        return false;
    }
    if (m_parse__) { class_swap(); }
    return true;
}


