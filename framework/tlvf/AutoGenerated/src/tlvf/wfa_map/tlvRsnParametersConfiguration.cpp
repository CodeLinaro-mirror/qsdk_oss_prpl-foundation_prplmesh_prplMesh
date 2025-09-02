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

#include <tlvf/wfa_map/tlvRsnParametersConfiguration.h>
#include <tlvf/tlvflogging.h>

using namespace wfa_map;

tlvRsnParametersConfiguration::tlvRsnParametersConfiguration(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvRsnParametersConfiguration::tlvRsnParametersConfiguration(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvRsnParametersConfiguration::~tlvRsnParametersConfiguration() {
}
const eTlvTypeMap& tlvRsnParametersConfiguration::type() {
    return (const eTlvTypeMap&)(*m_type);
}

const uint16_t& tlvRsnParametersConfiguration::length() {
    return (const uint16_t&)(*m_length);
}

uint8_t& tlvRsnParametersConfiguration::num_radio() {
    return (uint8_t&)(*m_num_radio);
}

std::tuple<bool, cRsnRadio&> tlvRsnParametersConfiguration::radios(size_t idx) {
    bool ret_success = ( (m_radios_idx__ > 0) && (m_radios_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_radios_vector[ret_idx]));
}

std::shared_ptr<cRsnRadio> tlvRsnParametersConfiguration::create_radios() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list radios, abort!";
        return nullptr;
    }
    size_t len = cRsnRadio::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_radios;
    if (m_radios_idx__ > 0) {
        src = (uint8_t *)m_radios_vector[m_radios_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cRsnRadio>(src, getBuffRemainingBytes(src), m_parse__);
}

bool tlvRsnParametersConfiguration::add_radios(std::shared_ptr<cRsnRadio> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_radios was called before add_radios";
        return false;
    }
    uint8_t *src = (uint8_t *)m_radios;
    if (m_radios_idx__ > 0) {
        src = (uint8_t *)m_radios_vector[m_radios_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_radios_idx__++;
    if (!m_parse__) { (*m_num_radio)++; }
    size_t len = ptr->getLen();
    m_radios_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(!m_parse__ && m_length){ (*m_length) += len; }
    m_lock_allocation__ = false;
    return true;
}

void tlvRsnParametersConfiguration::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    for (size_t i = 0; i < m_radios_idx__; i++){
        std::get<1>(radios(i)).class_swap();
    }
}

bool tlvRsnParametersConfiguration::finalize()
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

size_t tlvRsnParametersConfiguration::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(uint8_t); // num_radio
    return class_size;
}

bool tlvRsnParametersConfiguration::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_type = reinterpret_cast<eTlvTypeMap*>(m_buff_ptr__);
    if (!m_parse__) *m_type = eTlvTypeMap::TLV_RSN_PARAMETERS_CONFIGURATION;
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
    m_num_radio = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_num_radio = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_radios = reinterpret_cast<cRsnRadio*>(m_buff_ptr__);
    uint8_t num_radio = *m_num_radio;
    m_radios_idx__ = 0;
    for (size_t i = 0; i < num_radio; i++) {
        auto radios = create_radios();
        if (!radios || !radios->isInitialized()) {
            TLVF_LOG(ERROR) << "create_radios() failed";
            return false;
        }
        if (!add_radios(radios)) {
            TLVF_LOG(ERROR) << "add_radios() failed";
            return false;
        }
        // swap back since radios will be swapped as part of the whole class swap
        radios->class_swap();
    }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eTlvTypeMap::TLV_RSN_PARAMETERS_CONFIGURATION) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eTlvTypeMap::TLV_RSN_PARAMETERS_CONFIGURATION) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}

cRsnRadio::cRsnRadio(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cRsnRadio::cRsnRadio(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cRsnRadio::~cRsnRadio() {
}
sMacAddr& cRsnRadio::ruid() {
    return (sMacAddr&)(*m_ruid);
}

uint8_t& cRsnRadio::num_bss() {
    return (uint8_t&)(*m_num_bss);
}

std::tuple<bool, cRsnBss&> cRsnRadio::bsss(size_t idx) {
    bool ret_success = ( (m_bsss_idx__ > 0) && (m_bsss_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_bsss_vector[ret_idx]));
}

std::shared_ptr<cRsnBss> cRsnRadio::create_bsss() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list bsss, abort!";
        return nullptr;
    }
    size_t len = cRsnBss::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_bsss;
    if (m_bsss_idx__ > 0) {
        src = (uint8_t *)m_bsss_vector[m_bsss_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_reserved = (uint8_t *)((uint8_t *)(m_reserved) + len);
    return std::make_shared<cRsnBss>(src, getBuffRemainingBytes(src), m_parse__);
}

bool cRsnRadio::add_bsss(std::shared_ptr<cRsnBss> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_bsss was called before add_bsss";
        return false;
    }
    uint8_t *src = (uint8_t *)m_bsss;
    if (m_bsss_idx__ > 0) {
        src = (uint8_t *)m_bsss_vector[m_bsss_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_bsss_idx__++;
    if (!m_parse__) { (*m_num_bss)++; }
    size_t len = ptr->getLen();
    m_reserved = (uint8_t *)((uint8_t *)(m_reserved) + len - ptr->get_initial_size());
    m_bsss_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    m_lock_allocation__ = false;
    return true;
}

uint8_t* cRsnRadio::reserved(size_t idx) {
    if ( (m_reserved_idx__ == 0) || (m_reserved_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_reserved[idx]);
}

bool cRsnRadio::set_reserved(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_reserved received a null pointer.";
        return false;
    }
    if (size > 16) {
        TLVF_LOG(ERROR) << "Received buffer size is smaller than buffer length";
        return false;
    }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_reserved);
    return true;
}
void cRsnRadio::class_swap()
{
    m_ruid->struct_swap();
    for (size_t i = 0; i < m_bsss_idx__; i++){
        std::get<1>(bsss(i)).class_swap();
    }
}

bool cRsnRadio::finalize()
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

size_t cRsnRadio::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(sMacAddr); // ruid
    class_size += sizeof(uint8_t); // num_bss
    class_size += 16 * sizeof(uint8_t); // reserved
    return class_size;
}

bool cRsnRadio::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_ruid = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_ruid->struct_init(); }
    m_num_bss = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_num_bss = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_bsss = reinterpret_cast<cRsnBss*>(m_buff_ptr__);
    uint8_t num_bss = *m_num_bss;
    m_bsss_idx__ = 0;
    for (size_t i = 0; i < num_bss; i++) {
        auto bsss = create_bsss();
        if (!bsss || !bsss->isInitialized()) {
            TLVF_LOG(ERROR) << "create_bsss() failed";
            return false;
        }
        if (!add_bsss(bsss)) {
            TLVF_LOG(ERROR) << "add_bsss() failed";
            return false;
        }
        // swap back since bsss will be swapped as part of the whole class swap
        bsss->class_swap();
    }
    m_reserved = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (16))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (16) << ") Failed!";
        return false;
    }
    m_reserved_idx__  = 16;
    if (m_parse__) { class_swap(); }
    return true;
}

cRsnBss::cRsnBss(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cRsnBss::cRsnBss(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cRsnBss::~cRsnBss() {
}
sMacAddr& cRsnBss::bssid() {
    return (sMacAddr&)(*m_bssid);
}

uint8_t& cRsnBss::bss_index() {
    return (uint8_t&)(*m_bss_index);
}

uint16_t& cRsnBss::security_ies_length() {
    return (uint16_t&)(*m_security_ies_length);
}

uint8_t* cRsnBss::security_ies(size_t idx) {
    if ( (m_security_ies_idx__ == 0) || (m_security_ies_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_security_ies[idx]);
}

bool cRsnBss::set_security_ies(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_security_ies received a null pointer.";
        return false;
    }
    if (m_security_ies_idx__ != 0) {
        TLVF_LOG(ERROR) << "set_security_ies was already allocated!";
        return false;
    }
    if (!alloc_security_ies(size)) { return false; }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_security_ies);
    return true;
}
bool cRsnBss::alloc_security_ies(size_t count) {
    if (m_lock_order_counter__ > 0) {;
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list security_ies, abort!";
        return false;
    }
    size_t len = sizeof(uint8_t) * count;
    if(getBuffRemainingBytes() < len )  {
        TLVF_LOG(ERROR) << "Not enough available space on buffer - can't allocate";
        return false;
    }
    m_lock_order_counter__ = 0;
    uint8_t *src = (uint8_t *)&m_security_ies[*m_security_ies_length];
    uint8_t *dst = src + len;
    if (!m_parse__) {
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_reserved = (uint8_t *)((uint8_t *)(m_reserved) + len);
    m_security_ies_idx__ += count;
    *m_security_ies_length += count;
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    return true;
}

uint8_t* cRsnBss::reserved(size_t idx) {
    if ( (m_reserved_idx__ == 0) || (m_reserved_idx__ <= idx) ) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
        return nullptr;
    }
    return &(m_reserved[idx]);
}

bool cRsnBss::set_reserved(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        TLVF_LOG(WARNING) << "set_reserved received a null pointer.";
        return false;
    }
    if (size > 16) {
        TLVF_LOG(ERROR) << "Received buffer size is smaller than buffer length";
        return false;
    }
    std::copy_n(reinterpret_cast<const uint8_t *>(buffer), size, m_reserved);
    return true;
}
void cRsnBss::class_swap()
{
    m_bssid->struct_swap();
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_security_ies_length));
}

bool cRsnBss::finalize()
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

size_t cRsnBss::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(sMacAddr); // bssid
    class_size += sizeof(uint8_t); // bss_index
    class_size += sizeof(uint16_t); // security_ies_length
    class_size += 16 * sizeof(uint8_t); // reserved
    return class_size;
}

bool cRsnBss::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_bssid = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_bssid->struct_init(); }
    m_bss_index = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_security_ies_length = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!m_parse__) *m_security_ies_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    m_security_ies = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    uint16_t security_ies_length = *m_security_ies_length;
    if (m_parse__) {  tlvf_swap(16, reinterpret_cast<uint8_t*>(&security_ies_length)); }
    m_security_ies_idx__ = security_ies_length;
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (security_ies_length))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (security_ies_length) << ") Failed!";
        return false;
    }
    m_reserved = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t) * (16))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) * (16) << ") Failed!";
        return false;
    }
    m_reserved_idx__  = 16;
    if (m_parse__) { class_swap(); }
    return true;
}


