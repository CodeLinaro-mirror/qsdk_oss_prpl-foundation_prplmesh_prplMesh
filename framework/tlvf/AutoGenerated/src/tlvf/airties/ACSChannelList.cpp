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

#include <tlvf/airties/ACSChannelList.h>
#include <tlvf/tlvflogging.h>

using namespace airties;

cACSChannelList::cACSChannelList(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cACSChannelList::cACSChannelList(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cACSChannelList::~cACSChannelList() {
}
uint8_t& cACSChannelList::acs_list_length() {
    return (uint8_t&)(*m_acs_list_length);
}

std::tuple<bool, cExcludeList&> cACSChannelList::acs_list(size_t idx) {
    bool ret_success = ( (m_acs_list_idx__ > 0) && (m_acs_list_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_acs_list_vector[ret_idx]));
}

std::shared_ptr<cExcludeList> cACSChannelList::create_acs_list() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list acs_list, abort!";
        return nullptr;
    }
    size_t len = cExcludeList::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_acs_list;
    if (m_acs_list_idx__ > 0) {
        src = (uint8_t *)m_acs_list_vector[m_acs_list_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cExcludeList>(src, getBuffRemainingBytes(src), m_parse__);
}

bool cACSChannelList::add_acs_list(std::shared_ptr<cExcludeList> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_acs_list was called before add_acs_list";
        return false;
    }
    uint8_t *src = (uint8_t *)m_acs_list;
    if (m_acs_list_idx__ > 0) {
        src = (uint8_t *)m_acs_list_vector[m_acs_list_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_acs_list_idx__++;
    if (!m_parse__) { (*m_acs_list_length)++; }
    size_t len = ptr->getLen();
    m_acs_list_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    m_lock_allocation__ = false;
    return true;
}

void cACSChannelList::class_swap()
{
    for (size_t i = 0; i < m_acs_list_idx__; i++){
        std::get<1>(acs_list(i)).class_swap();
    }
}

bool cACSChannelList::finalize()
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

size_t cACSChannelList::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(uint8_t); // acs_list_length
    return class_size;
}

bool cACSChannelList::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_acs_list_length = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_acs_list_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_acs_list = reinterpret_cast<cExcludeList*>(m_buff_ptr__);
    uint8_t acs_list_length = *m_acs_list_length;
    m_acs_list_idx__ = 0;
    for (size_t i = 0; i < acs_list_length; i++) {
        auto acs_list = create_acs_list();
        if (!acs_list || !acs_list->isInitialized()) {
            TLVF_LOG(ERROR) << "create_acs_list() failed";
            return false;
        }
        if (!add_acs_list(acs_list)) {
            TLVF_LOG(ERROR) << "add_acs_list() failed";
            return false;
        }
        // swap back since acs_list will be swapped as part of the whole class swap
        acs_list->class_swap();
    }
    if (m_parse__) { class_swap(); }
    return true;
}

cExcludeList::cExcludeList(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cExcludeList::cExcludeList(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cExcludeList::~cExcludeList() {
}
uint8_t& cExcludeList::opclass() {
    return (uint8_t&)(*m_opclass);
}

uint8_t& cExcludeList::exclude_channels_length() {
    return (uint8_t&)(*m_exclude_channels_length);
}

uint8_t* cExcludeList::exclude_channels(size_t idx) {
    if ( (m_exclude_channels_idx__ == 0) || (m_exclude_channels_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_exclude_channels[idx]);
}

bool cExcludeList::set_exclude_channels(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_exclude_channels received a null pointer.";
        return false;
    }
    if (m_exclude_channels_idx__ != 0) {
        TLVF_LOG(ERROR) << "set_exclude_channels was already allocated!";
        return false;
    }
    if (!alloc_exclude_channels(size)) { return false; }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_exclude_channels);
    return true;
}
bool cExcludeList::alloc_exclude_channels(size_t count) {
    if (m_lock_order_counter__ > 0) {;
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list exclude_channels, abort!";
        return false;
    }
    size_t len = sizeof(uint8_t) * count;
    if(getBuffRemainingBytes() < len )  {
        TLVF_LOG(ERROR) << "Not enough available space on buffer - can't allocate";
        return false;
    }
    m_lock_order_counter__ = 0;
    uint8_t *src = (uint8_t *)&m_exclude_channels[*m_exclude_channels_length];
    uint8_t *dst = src + len;
    if (!m_parse__) {
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_exclude_channels_idx__ += count;
    *m_exclude_channels_length += count;
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    return true;
}

void cExcludeList::class_swap()
{
}

bool cExcludeList::finalize()
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

size_t cExcludeList::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(uint8_t); // opclass
    class_size += sizeof(uint8_t); // exclude_channels_length
    return class_size;
}

bool cExcludeList::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_opclass = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_exclude_channels_length = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_exclude_channels_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_exclude_channels = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    uint8_t exclude_channels_length = *m_exclude_channels_length;
    m_exclude_channels_idx__ = exclude_channels_length;
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (exclude_channels_length))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (exclude_channels_length) << ") Failed!";
        return false;
    }
    if (m_parse__) { class_swap(); }
    return true;
}


