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

#include <tlvf/wfa_map/tlvBeaconMetricsResponse.h>
#include <tlvf/tlvflogging.h>

using namespace wfa_map;

tlvBeaconMetricsResponse::tlvBeaconMetricsResponse(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvBeaconMetricsResponse::tlvBeaconMetricsResponse(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvBeaconMetricsResponse::~tlvBeaconMetricsResponse() {
}
const eTlvTypeMap& tlvBeaconMetricsResponse::type() {
    return (const eTlvTypeMap&)(*m_type);
}

const uint16_t& tlvBeaconMetricsResponse::length() {
    return (const uint16_t&)(*m_length);
}

sMacAddr& tlvBeaconMetricsResponse::associated_sta_mac() {
    return (sMacAddr&)(*m_associated_sta_mac);
}

uint8_t& tlvBeaconMetricsResponse::reserved() {
    return (uint8_t&)(*m_reserved);
}

uint8_t& tlvBeaconMetricsResponse::measurement_report_list_length() {
    return (uint8_t&)(*m_measurement_report_list_length);
}

std::tuple<bool, cMeasurementReportElement&> tlvBeaconMetricsResponse::measurement_report_list(size_t idx) {
    bool ret_success = ( (m_measurement_report_list_idx__ > 0) && (m_measurement_report_list_idx__ > idx) );
    size_t ret_idx = ret_success ? idx : 0;
    if (!ret_success) {
        TLVF_LOG(ERROR) << "Requested index is greater than the number of available entries";
    }
    return std::forward_as_tuple(ret_success, *(m_measurement_report_list_vector[ret_idx]));
}

std::shared_ptr<cMeasurementReportElement> tlvBeaconMetricsResponse::create_measurement_report_list() {
    if (m_lock_order_counter__ > 0) {
        TLVF_LOG(ERROR) << "Out of order allocation for variable length list measurement_report_list, abort!";
        return nullptr;
    }
    size_t len = cMeasurementReportElement::get_initial_size();
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
    uint8_t *src = (uint8_t *)m_measurement_report_list;
    if (m_measurement_report_list_idx__ > 0) {
        src = (uint8_t *)m_measurement_report_list_vector[m_measurement_report_list_idx__ - 1]->getBuffPtr();
    }
    if (!m_parse__) {
        uint8_t *dst = src + len;
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    return std::make_shared<cMeasurementReportElement>(src, getBuffRemainingBytes(src), m_parse__);
}

bool tlvBeaconMetricsResponse::add_measurement_report_list(std::shared_ptr<cMeasurementReportElement> ptr) {
    if (ptr == nullptr) {
        TLVF_LOG(ERROR) << "Received entry is nullptr";
        return false;
    }
    if (m_lock_allocation__ == false) {
        TLVF_LOG(ERROR) << "No call to create_measurement_report_list was called before add_measurement_report_list";
        return false;
    }
    uint8_t *src = (uint8_t *)m_measurement_report_list;
    if (m_measurement_report_list_idx__ > 0) {
        src = (uint8_t *)m_measurement_report_list_vector[m_measurement_report_list_idx__ - 1]->getBuffPtr();
    }
    if (ptr->getStartBuffPtr() != src) {
        TLVF_LOG(ERROR) << "Received entry pointer is different than expected (expecting the same pointer returned from add method)";
        return false;
    }
    if (ptr->getLen() > getBuffRemainingBytes(ptr->getStartBuffPtr())) {;
        TLVF_LOG(ERROR) << "Not enough available space on buffer";
        return false;
    }
    m_measurement_report_list_idx__++;
    if (!m_parse__) { (*m_measurement_report_list_length)++; }
    size_t len = ptr->getLen();
    m_measurement_report_list_vector.push_back(ptr);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(!m_parse__ && m_length){ (*m_length) += len; }
    m_lock_allocation__ = false;
    return true;
}

void tlvBeaconMetricsResponse::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    m_associated_sta_mac->struct_swap();
    for (size_t i = 0; i < m_measurement_report_list_idx__; i++){
        std::get<1>(measurement_report_list(i)).class_swap();
    }
}

bool tlvBeaconMetricsResponse::finalize()
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

size_t tlvBeaconMetricsResponse::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(sMacAddr); // associated_sta_mac
    class_size += sizeof(uint8_t); // reserved
    class_size += sizeof(uint8_t); // measurement_report_list_length
    return class_size;
}

bool tlvBeaconMetricsResponse::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_type = reinterpret_cast<eTlvTypeMap*>(m_buff_ptr__);
    if (!m_parse__) *m_type = eTlvTypeMap::TLV_BEACON_METRICS_RESPONSE;
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
    m_associated_sta_mac = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(sMacAddr); }
    if (!m_parse__) { m_associated_sta_mac->struct_init(); }
    m_reserved = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_measurement_report_list_length = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!m_parse__) *m_measurement_report_list_length = 0;
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(uint8_t); }
    m_measurement_report_list = reinterpret_cast<cMeasurementReportElement*>(m_buff_ptr__);
    uint8_t measurement_report_list_length = *m_measurement_report_list_length;
    m_measurement_report_list_idx__ = 0;
    for (size_t i = 0; i < measurement_report_list_length; i++) {
        auto measurement_report_list = create_measurement_report_list();
        if (!measurement_report_list || !measurement_report_list->isInitialized()) {
            TLVF_LOG(ERROR) << "create_measurement_report_list() failed";
            return false;
        }
        if (!add_measurement_report_list(measurement_report_list)) {
            TLVF_LOG(ERROR) << "add_measurement_report_list() failed";
            return false;
        }
        // swap back since measurement_report_list will be swapped as part of the whole class swap
        measurement_report_list->class_swap();
    }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eTlvTypeMap::TLV_BEACON_METRICS_RESPONSE) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eTlvTypeMap::TLV_BEACON_METRICS_RESPONSE) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}

cMeasurementReportElement::cMeasurementReportElement(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
cMeasurementReportElement::cMeasurementReportElement(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
cMeasurementReportElement::~cMeasurementReportElement() {
}
const cMeasurementReportElement::eElementID& cMeasurementReportElement::element_id() {
    return (const eElementID&)(*m_element_id);
}

uint8_t& cMeasurementReportElement::length() {
    return (uint8_t&)(*m_length);
}

uint8_t& cMeasurementReportElement::measurement_token() {
    return (uint8_t&)(*m_measurement_token);
}

uint8_t& cMeasurementReportElement::measurement_req_mode() {
    return (uint8_t&)(*m_measurement_req_mode);
}

const cMeasurementReportElement::eMeasurementType& cMeasurementReportElement::measurement_type() {
    return (const eMeasurementType&)(*m_measurement_type);
}

uint8_t& cMeasurementReportElement::op_class() {
    return (uint8_t&)(*m_op_class);
}

uint8_t& cMeasurementReportElement::channel() {
    return (uint8_t&)(*m_channel);
}

tlvf_uint64_t cMeasurementReportElement::start_time() {
    return tlvf_uint64_t(*m_start_time);
}

uint16_t& cMeasurementReportElement::duration() {
    return (uint16_t&)(*m_duration);
}

uint8_t& cMeasurementReportElement::phy_type() {
    return (uint8_t&)(*m_phy_type);
}

uint8_t& cMeasurementReportElement::rcpi() {
    return (uint8_t&)(*m_rcpi);
}

uint8_t& cMeasurementReportElement::rsni() {
    return (uint8_t&)(*m_rsni);
}

sMacAddr& cMeasurementReportElement::bssid() {
    return (sMacAddr&)(*m_bssid);
}

uint8_t& cMeasurementReportElement::antenna_id() {
    return (uint8_t&)(*m_antenna_id);
}

uint32_t& cMeasurementReportElement::parent_tsf() {
    return (uint32_t&)(*m_parent_tsf);
}

void cMeasurementReportElement::class_swap()
{
    tlvf_swap(64, reinterpret_cast<uint8_t*>(m_start_time));
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_duration));
    m_bssid->struct_swap();
    tlvf_swap(32, reinterpret_cast<uint8_t*>(m_parent_tsf));
}

bool cMeasurementReportElement::finalize()
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

size_t cMeasurementReportElement::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eElementID); // element_id
    class_size += sizeof(uint8_t); // length
    class_size += sizeof(uint8_t); // measurement_token
    class_size += sizeof(uint8_t); // measurement_req_mode
    class_size += sizeof(eMeasurementType); // measurement_type
    class_size += sizeof(uint8_t); // op_class
    class_size += sizeof(uint8_t); // channel
    class_size += sizeof(uint64_t); // start_time
    class_size += sizeof(uint16_t); // duration
    class_size += sizeof(uint8_t); // phy_type
    class_size += sizeof(uint8_t); // rcpi
    class_size += sizeof(uint8_t); // rsni
    class_size += sizeof(sMacAddr); // bssid
    class_size += sizeof(uint8_t); // antenna_id
    class_size += sizeof(uint32_t); // parent_tsf
    return class_size;
}

bool cMeasurementReportElement::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_element_id = reinterpret_cast<eElementID*>(m_buff_ptr__);
    if (!m_parse__) *m_element_id = eElementID::ID_MEASUREMENT_REPORT;
    if (!buffPtrIncrementSafe(sizeof(eElementID))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(eElementID) << ") Failed!";
        return false;
    }
    m_length = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_measurement_token = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_measurement_req_mode = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_measurement_type = reinterpret_cast<eMeasurementType*>(m_buff_ptr__);
    if (!m_parse__) *m_measurement_type = eMeasurementType::TYPE_BEACON;
    if (!buffPtrIncrementSafe(sizeof(eMeasurementType))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(eMeasurementType) << ") Failed!";
        return false;
    }
    m_op_class = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_channel = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_start_time = reinterpret_cast<uint64_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint64_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint64_t) << ") Failed!";
        return false;
    }
    m_duration = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    m_phy_type = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_rcpi = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_rsni = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_bssid = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_bssid->struct_init(); }
    m_antenna_id = reinterpret_cast<uint8_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint8_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint8_t) << ") Failed!";
        return false;
    }
    m_parent_tsf = reinterpret_cast<uint32_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint32_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint32_t) << ") Failed!";
        return false;
    }
    if (m_parse__) { class_swap(); }
    return true;
}


