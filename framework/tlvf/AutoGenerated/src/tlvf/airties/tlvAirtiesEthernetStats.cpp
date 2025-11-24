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

#include <tlvf/airties/tlvAirtiesEthernetStats.h>
#include <tlvf/tlvflogging.h>

using namespace airties;

tlvAirtiesEthernetStats::tlvAirtiesEthernetStats(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvAirtiesEthernetStats::tlvAirtiesEthernetStats(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvAirtiesEthernetStats::~tlvAirtiesEthernetStats() {
}
const eAirtiesTlvTypeMap& tlvAirtiesEthernetStats::type() {
    return (const eAirtiesTlvTypeMap&)(*m_type);
}

const uint16_t& tlvAirtiesEthernetStats::length() {
    return (const uint16_t&)(*m_length);
}

sVendorOUI& tlvAirtiesEthernetStats::vendor_oui() {
    return (sVendorOUI&)(*m_vendor_oui);
}

uint16_t& tlvAirtiesEthernetStats::tlv_id() {
    return (uint16_t&)(*m_tlv_id);
}

uint16_t& tlvAirtiesEthernetStats::supported_extra_stats() {
    return (uint16_t&)(*m_supported_extra_stats);
}

uint8_t& tlvAirtiesEthernetStats::num_of_ports() {
    return (uint8_t&)(*m_num_of_ports);
}

std::tuple<bool, cPortList&> tlvAirtiesEthernetStats::port_list(size_t idx) {
    bool ret_success = ( (m_port_list_idx__ > 0) && (m_port_list_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_port_list_vector[ret_idx]));
}

std::shared_ptr<cPortList> tlvAirtiesEthernetStats::create_port_list() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list port_list, abort!";
        return nullptr;
    }
    size_t len = cPortList::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_port_list;
    if (m_port_list_idx__ > 0) {
        src = (uint8_t *)m_port_list_vector[m_port_list_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cPortList>(src, getBuffRemainingBytes(src), m_parse__);
}

bool tlvAirtiesEthernetStats::add_port_list(std::shared_ptr<cPortList> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_port_list was called before add_port_list";
        return false;
    }
    uint8_t *src = (uint8_t *)m_port_list;
    if (m_port_list_idx__ > 0) {
        src = (uint8_t *)m_port_list_vector[m_port_list_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_port_list_idx__++;
    if (!m_parse__) { (*m_num_of_ports)++; }
    size_t len = ptr->getLen();
    m_port_list_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(!m_parse__ && m_length){ (*m_length) += len; }
    m_lock_allocation__ = false;
    return true;
}

void tlvAirtiesEthernetStats::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    m_vendor_oui->struct_swap();
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_tlv_id));
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_supported_extra_stats));
    for (size_t i = 0; i < m_port_list_idx__; i++){
        std::get<1>(port_list(i)).class_swap();
    }
}

bool tlvAirtiesEthernetStats::finalize()
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

size_t tlvAirtiesEthernetStats::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eAirtiesTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(sVendorOUI); // vendor_oui
    class_size += sizeof(uint16_t); // tlv_id
    class_size += sizeof(uint16_t); // supported_extra_stats
    class_size += sizeof(uint8_t); // num_of_ports
    return class_size;
}

bool tlvAirtiesEthernetStats::init()
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
    m_supported_extra_stats = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint16_t); }
    m_num_of_ports = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_num_of_ports = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_port_list = reinterpret_cast<cPortList*>(m_buff_ptr__);
    uint8_t num_of_ports = *m_num_of_ports;
    m_port_list_idx__ = 0;
    for (size_t i = 0; i < num_of_ports; i++) {
        auto port_list = create_port_list();
        if (!port_list || !port_list->isInitialized()) {
            TLVF_LOG(ERROR) << "create_port_list() failed";
            return false;
        }
        if (!add_port_list(port_list)) {
            TLVF_LOG(ERROR) << "add_port_list() failed";
            return false;
        }
        // swap back since port_list will be swapped as part of the whole class swap
        port_list->class_swap();
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

cPortList::cPortList(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cPortList::cPortList(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cPortList::~cPortList() {
}
uint8_t& cPortList::port_id() {
    return (uint8_t&)(*m_port_id);
}

size_t cPortList::statsItem_length()
{
    size_t len = 0;
    for (size_t i = 0; i < m_statsItem_idx__; i++) {
        len += m_statsItem_vector[i]->getLen();
    }
    return len;
}

std::tuple<bool, cStatsItem&> cPortList::statsItem(size_t idx) {
    bool ret_success = ( (m_statsItem_idx__ > 0) && (m_statsItem_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_statsItem_vector[ret_idx]));
}

std::shared_ptr<cStatsItem> cPortList::create_statsItem() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list statsItem, abort!";
        return nullptr;
    }
    size_t len = cStatsItem::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_statsItem;
    if (m_statsItem_idx__ > 0) {
        src = (uint8_t *)m_statsItem_vector[m_statsItem_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cStatsItem>(getBuffPtr(), getBuffRemainingBytes(), m_parse__);
}

bool cPortList::add_statsItem(std::shared_ptr<cStatsItem> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_statsItem was called before add_statsItem";
        return false;
    }
    uint8_t *src = (uint8_t *)m_statsItem;
    if (m_statsItem_idx__ > 0) {
        src = (uint8_t *)m_statsItem_vector[m_statsItem_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_statsItem_idx__++;
    size_t len = ptr->getLen();
    m_statsItem_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    m_lock_allocation__ = false;
    return true;
}

void cPortList::class_swap()
{
    for (size_t i = 0; i < m_statsItem_idx__; i++){
        std::get<1>(statsItem(i)).class_swap();
    }
}

bool cPortList::finalize()
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

size_t cPortList::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(uint8_t); // port_id
    return class_size;
}

bool cPortList::init()
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
    m_statsItem = reinterpret_cast<cStatsItem*>(m_buff_ptr__);
    if (m_parse__) {
        size_t len = getBuffRemainingBytes();
        while (len > 0) {
            if (len < cStatsItem::get_initial_size()) {
                TLVF_LOG(ERROR) << "Invalid length (statsItem)";
                return false;
            }
            auto statsItem = create_statsItem();
            if (!statsItem || !statsItem->isInitialized()) {
                TLVF_LOG(ERROR) << "create_statsItem() failed";
                return false;
            }
            if (!add_statsItem(statsItem)) {
                TLVF_LOG(ERROR) << "add_statsItem() failed";
                return false;
            }
            // swap back since statsItem will be swapped as part of the whole class swap
            statsItem->class_swap();
            len -= statsItem->getLen();
        }
    }
    if (m_parse__) { class_swap(); }
    return true;
}

cStatsItem::cStatsItem(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cStatsItem::cStatsItem(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cStatsItem::~cStatsItem() {
}
uint8_t* cStatsItem::item(size_t idx) {
    if ( (m_item_idx__ == 0) || (m_item_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_item[idx]);
}

bool cStatsItem::set_item(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_item received a null pointer.";
        return false;
    }
    if (size > 6) {
        TLVF_LOG(ERROR) << "Received buffer size is smaller than buffer length";
        return false;
    }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_item);
    return true;
}
void cStatsItem::class_swap()
{
}

bool cStatsItem::finalize()
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

size_t cStatsItem::get_initial_size()
{
    size_t class_size = 0;
    class_size += 6 * sizeof(uint8_t); // item
    return class_size;
}

bool cStatsItem::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_item = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (6))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (6) << ") Failed!";
        return false;
    }
    m_item_idx__  = 6;
    if (m_parse__) { class_swap(); }
    return true;
}


