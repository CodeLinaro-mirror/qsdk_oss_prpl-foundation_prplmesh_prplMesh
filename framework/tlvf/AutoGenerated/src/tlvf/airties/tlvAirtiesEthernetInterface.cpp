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

#include <tlvf/airties/tlvAirtiesEthernetInterface.h>
#include <tlvf/tlvflogging.h>

using namespace airties;

tlvAirtiesEthernetInterface::tlvAirtiesEthernetInterface(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvAirtiesEthernetInterface::tlvAirtiesEthernetInterface(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvAirtiesEthernetInterface::~tlvAirtiesEthernetInterface() {
}
const eAirtiesTlvTypeMap& tlvAirtiesEthernetInterface::type() {
    return (const eAirtiesTlvTypeMap&)(*m_type);
}

const uint16_t& tlvAirtiesEthernetInterface::length() {
    return (const uint16_t&)(*m_length);
}

sVendorOUI& tlvAirtiesEthernetInterface::vendor_oui() {
    return (sVendorOUI&)(*m_vendor_oui);
}

uint16_t& tlvAirtiesEthernetInterface::tlv_id() {
    return (uint16_t&)(*m_tlv_id);
}

uint8_t& tlvAirtiesEthernetInterface::number_of_eth_phy() {
    return (uint8_t&)(*m_number_of_eth_phy);
}

std::tuple<bool, cInterfaceList&> tlvAirtiesEthernetInterface::interface_list(size_t idx) {
    bool ret_success = ( (m_interface_list_idx__ > 0) && (m_interface_list_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_interface_list_vector[ret_idx]));
}

std::shared_ptr<cInterfaceList> tlvAirtiesEthernetInterface::create_interface_list() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list interface_list, abort!";
        return nullptr;
    }
    size_t len = cInterfaceList::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_interface_list;
    if (m_interface_list_idx__ > 0) {
        src = (uint8_t *)m_interface_list_vector[m_interface_list_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cInterfaceList>(src, getBuffRemainingBytes(src), m_parse__);
}

bool tlvAirtiesEthernetInterface::add_interface_list(std::shared_ptr<cInterfaceList> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_interface_list was called before add_interface_list";
        return false;
    }
    uint8_t *src = (uint8_t *)m_interface_list;
    if (m_interface_list_idx__ > 0) {
        src = (uint8_t *)m_interface_list_vector[m_interface_list_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_interface_list_idx__++;
    if (!m_parse__) { (*m_number_of_eth_phy)++; }
    size_t len = ptr->getLen();
    m_interface_list_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(!m_parse__ && m_length){ (*m_length) += len; }
    m_lock_allocation__ = false;
    return true;
}

void tlvAirtiesEthernetInterface::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    m_vendor_oui->struct_swap();
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_tlv_id));
    for (size_t i = 0; i < m_interface_list_idx__; i++){
        std::get<1>(interface_list(i)).class_swap();
    }
}

bool tlvAirtiesEthernetInterface::finalize()
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

size_t tlvAirtiesEthernetInterface::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eAirtiesTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(sVendorOUI); // vendor_oui
    class_size += sizeof(uint16_t); // tlv_id
    class_size += sizeof(uint8_t); // number_of_eth_phy
    return class_size;
}

bool tlvAirtiesEthernetInterface::init()
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
    m_number_of_eth_phy = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_number_of_eth_phy = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_interface_list = reinterpret_cast<cInterfaceList*>(m_buff_ptr__);
    uint8_t number_of_eth_phy = *m_number_of_eth_phy;
    m_interface_list_idx__ = 0;
    for (size_t i = 0; i < number_of_eth_phy; i++) {
        auto interface_list = create_interface_list();
        if (!interface_list || !interface_list->isInitialized()) {
            TLVF_LOG(ERROR) << "create_interface_list() failed";
            return false;
        }
        if (!add_interface_list(interface_list)) {
            TLVF_LOG(ERROR) << "add_interface_list() failed";
            return false;
        }
        // swap back since interface_list will be swapped as part of the whole class swap
        interface_list->class_swap();
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

cInterfaceList::cInterfaceList(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cInterfaceList::cInterfaceList(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cInterfaceList::~cInterfaceList() {
}
uint8_t& cInterfaceList::port_id() {
    return (uint8_t&)(*m_port_id);
}

sMacAddr& cInterfaceList::eth_mac() {
    return (sMacAddr&)(*m_eth_mac);
}

uint8_t& cInterfaceList::eth_intf_name_len() {
    return (uint8_t&)(*m_eth_intf_name_len);
}

std::string cInterfaceList::eth_intf_name_str() {
    char *eth_intf_name_ = eth_intf_name();
    if (!eth_intf_name_) { return std::string(); }
    auto str = std::string(eth_intf_name_, m_eth_intf_name_idx__);
    auto pos = str.find_first_of('\0');
    if (pos != std::string::npos) {
        str.erase(pos);
    }
    return str;
}

char* cInterfaceList::eth_intf_name(size_t length) {
    if( (m_eth_intf_name_idx__ == 0) || (m_eth_intf_name_idx__ < length) ) {
        TLVF_LOG(ERROR) << "eth_intf_name length is smaller than requested length";
        return nullptr;
    }
    return ((char*)m_eth_intf_name);
}

bool cInterfaceList::set_eth_intf_name(const std::string& str) { return set_eth_intf_name(str.c_str(), str.size()); }
bool cInterfaceList::set_eth_intf_name(const char str[], size_t size) {
    if (str == nullptr) {
        TLVF_LOG(WARNING) << "set_eth_intf_name received a null pointer.";
        return false;
    }
    if (m_eth_intf_name_idx__ != 0) {
        TLVF_LOG(ERROR) << "set_eth_intf_name was already allocated!";
        return false;
    }
    if (!alloc_eth_intf_name(size)) { return false; }
    std::copy(str, str + size, m_eth_intf_name);
    return true;
}
bool cInterfaceList::alloc_eth_intf_name(size_t count) {
    if (m_lock_order_counter__ > 0) {;
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list eth_intf_name, abort!";
        return false;
    }
    size_t len = sizeof(char) * count;
    if(getBuffRemainingBytes() < len )  {
        TLVF_LOG(ERROR) << "Not enough available space on buffer - can't allocate";
        return false;
    }
    m_lock_order_counter__ = 0;
    uint8_t *src = (uint8_t *)&m_eth_intf_name[*m_eth_intf_name_len];
    uint8_t *dst = src + len;
    if (!m_parse__) {
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_flags1 = (sFlags1 *)((uint8_t *)(m_flags1) + len);
    m_flags2 = (sFlags2 *)((uint8_t *)(m_flags2) + len);
    m_eth_intf_name_idx__ += count;
    *m_eth_intf_name_len += count;
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    return true;
}

cInterfaceList::sFlags1& cInterfaceList::flags1() {
    return (sFlags1&)(*m_flags1);
}

cInterfaceList::sFlags2& cInterfaceList::flags2() {
    return (sFlags2&)(*m_flags2);
}

void cInterfaceList::class_swap()
{
    m_eth_mac->struct_swap();
    m_flags1->struct_swap();
    m_flags2->struct_swap();
}

bool cInterfaceList::finalize()
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

size_t cInterfaceList::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(uint8_t); // port_id
    class_size += sizeof(sMacAddr); // eth_mac
    class_size += sizeof(uint8_t); // eth_intf_name_len
    class_size += sizeof(sFlags1); // flags1
    class_size += sizeof(sFlags2); // flags2
    return class_size;
}

bool cInterfaceList::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_port_id = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_eth_mac = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_eth_mac->struct_init(); }
    m_eth_intf_name_len = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_eth_intf_name_len = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_eth_intf_name = reinterpret_cast<char*>(m_buff_ptr__);
    uint8_t eth_intf_name_len = *m_eth_intf_name_len;
    m_eth_intf_name_idx__ = eth_intf_name_len;
    if (!buffPtrIncrementSafe(sizeof(char) * (eth_intf_name_len))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(char) * (eth_intf_name_len) << ") Failed!";
        return false;
    }
    m_flags1 = reinterpret_cast<sFlags1*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sFlags1))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sFlags1) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_flags1->struct_init(); }
    m_flags2 = reinterpret_cast<sFlags2*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sFlags2))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sFlags2) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_flags2->struct_init(); }
    if (m_parse__) { class_swap(); }
    return true;
}


