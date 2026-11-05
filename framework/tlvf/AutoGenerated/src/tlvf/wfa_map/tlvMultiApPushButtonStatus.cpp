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

#include <tlvf/wfa_map/tlvMultiApPushButtonStatus.h>
#include <tlvf/tlvflogging.h>

using namespace wfa_map;

tlvMultiApPushButtonStatus::tlvMultiApPushButtonStatus(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvMultiApPushButtonStatus::tlvMultiApPushButtonStatus(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvMultiApPushButtonStatus::~tlvMultiApPushButtonStatus() {
}
const eTlvTypeMap& tlvMultiApPushButtonStatus::type() {
    return (const eTlvTypeMap&)(*m_type);
}

const uint16_t& tlvMultiApPushButtonStatus::length() {
    return (const uint16_t&)(*m_length);
}

sPbcSessionId& tlvMultiApPushButtonStatus::pbc_session_id() {
    return (sPbcSessionId&)(*m_pbc_session_id);
}

sMacAddr& tlvMultiApPushButtonStatus::bssid() {
    return (sMacAddr&)(*m_bssid);
}

tlvMultiApPushButtonStatus::eResult& tlvMultiApPushButtonStatus::result() {
    return (eResult&)(*m_result);
}

bool tlvMultiApPushButtonStatus::alloc_failure_reason() {
    if (m_failure_reason_allocated) {
        LOG(ERROR) << "failure_reason already allocated!";
        return false;
    }
    size_t len = sizeof(uint16_t);
    if(getBuffRemainingBytes() < len )  {
        TLVF_LOG(ERROR) << "Not enough available space on buffer - can't allocate";
        return false;
    }
    uint8_t *src = (uint8_t *)m_failure_reason;
    uint8_t *dst = src + len;
    if (!m_parse__) {
        size_t move_length = getBuffRemainingBytes(src) - len;
        std::copy_n(src, move_length, dst);
    }
    m_enrollee_mac = (sMacAddr *)((uint8_t *)(m_enrollee_mac) + len);
    if (!buffPtrIncrementSafe(len)) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << len << ") Failed!";
        return false;
    }
    if(m_length){ (*m_length) += len; }
    m_failure_reason_allocated = true;
    return true;
}

uint16_t* tlvMultiApPushButtonStatus::failure_reason() {
    if (!m_result || !(*m_result == eResult::FAILURE_ABORTED || *m_result == eResult::NOT_STARTED)) {
        TLVF_LOG(ERROR) << "failure_reason requested but condition not met: *m_result == eResult::FAILURE_ABORTED || *m_result == eResult::NOT_STARTED";
        return nullptr;
    }
    return (uint16_t*)(m_failure_reason);
}

bool tlvMultiApPushButtonStatus::set_failure_reason(const uint16_t failure_reason) {
    if (!m_failure_reason_allocated && !alloc_failure_reason()) {
        LOG(ERROR) << "Could not allocate failure_reason!";
        return false;
    }
    *m_failure_reason = failure_reason;
    return true;
}

sMacAddr& tlvMultiApPushButtonStatus::enrollee_mac() {
    return (sMacAddr&)(*m_enrollee_mac);
}

void tlvMultiApPushButtonStatus::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_length));
    m_pbc_session_id->struct_swap();
    m_bssid->struct_swap();
    tlvf_swap(8*sizeof(eResult), reinterpret_cast<uint8_t*>(m_result));
    if (*m_result == eResult::FAILURE_ABORTED || *m_result == eResult::NOT_STARTED) {
        tlvf_swap(16, reinterpret_cast<uint8_t*>(m_failure_reason));
    }
    m_enrollee_mac->struct_swap();
}

bool tlvMultiApPushButtonStatus::finalize()
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

size_t tlvMultiApPushButtonStatus::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(eTlvTypeMap); // type
    class_size += sizeof(uint16_t); // length
    class_size += sizeof(sPbcSessionId); // pbc_session_id
    class_size += sizeof(sMacAddr); // bssid
    class_size += sizeof(eResult); // result
    class_size += sizeof(sMacAddr); // enrollee_mac
    return class_size;
}

bool tlvMultiApPushButtonStatus::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_type = reinterpret_cast<eTlvTypeMap*>(m_buff_ptr__);
    if (!m_parse__) *m_type = eTlvTypeMap::TLV_MULTI_AP_PUSH_BUTTON_STATUS;
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
    m_pbc_session_id = reinterpret_cast<sPbcSessionId*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sPbcSessionId))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sPbcSessionId) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(sPbcSessionId); }
    if (!m_parse__) { m_pbc_session_id->struct_init(); }
    m_bssid = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(sMacAddr); }
    if (!m_parse__) { m_bssid->struct_init(); }
    m_result = reinterpret_cast<eResult*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(eResult))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(eResult) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(eResult); }
    m_failure_reason = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if ((*m_result == eResult::FAILURE_ABORTED || *m_result == eResult::NOT_STARTED) && !buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    m_enrollee_mac = reinterpret_cast<sMacAddr*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sMacAddr))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sMacAddr) << ") Failed!";
        return false;
    }
    if(m_length && !m_parse__){ (*m_length) += sizeof(sMacAddr); }
    if (!m_parse__) { m_enrollee_mac->struct_init(); }
    if (m_parse__) { class_swap(); }
    if (m_parse__) {
        if (*m_type != eTlvTypeMap::TLV_MULTI_AP_PUSH_BUTTON_STATUS) {
            TLVF_LOG(ERROR) << "TLV type mismatch. Expected value: " << int(eTlvTypeMap::TLV_MULTI_AP_PUSH_BUTTON_STATUS) << ", received value: " << int(*m_type);
            return false;
        }
    }
    return true;
}


