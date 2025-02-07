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

#include <tlvf/airties/tlvAirtiesServiceStatus.h>
#include <tlvf/tlvflogging.h>

using namespace airties;

tlvAirtiesServiceStatus::tlvAirtiesServiceStatus(uint8_t* buff, size_t buff_len, bool parse) :
    BaseClass(buff, buff_len, parse) {
    m_init_succeeded = init();
}
tlvAirtiesServiceStatus::tlvAirtiesServiceStatus(std::shared_ptr<BaseClass> base, bool parse) :
BaseClass(base->getBuffPtr(), base->getBuffRemainingBytes(), parse){
    m_init_succeeded = init();
}
tlvAirtiesServiceStatus::~tlvAirtiesServiceStatus() {
}
uint16_t& tlvAirtiesServiceStatus::tlv_id() {
    return (uint16_t&)(*m_tlv_id);
}

tlvAirtiesServiceStatus::sServiceStatusTlvPayload& tlvAirtiesServiceStatus::payload() {
    return (sServiceStatusTlvPayload&)(*m_payload);
}

void tlvAirtiesServiceStatus::class_swap()
{
    tlvf_swap(16, reinterpret_cast<uint8_t*>(m_tlv_id));
    m_payload->struct_swap();
}

bool tlvAirtiesServiceStatus::finalize()
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

size_t tlvAirtiesServiceStatus::get_initial_size()
{
    size_t class_size = 0;
    class_size += sizeof(uint16_t); // tlv_id
    class_size += sizeof(sServiceStatusTlvPayload); // payload
    return class_size;
}

bool tlvAirtiesServiceStatus::init()
{
    if (getBuffRemainingBytes() < get_initial_size()) {
        TLVF_LOG(ERROR) << "Not enough available space on buffer. Class init failed";
        return false;
    }
    m_tlv_id = reinterpret_cast<uint16_t*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(uint16_t))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(uint16_t) << ") Failed!";
        return false;
    }
    m_payload = reinterpret_cast<sServiceStatusTlvPayload*>(m_buff_ptr__);
    if (!buffPtrIncrementSafe(sizeof(sServiceStatusTlvPayload))) {
        LOG(ERROR) << "buffPtrIncrementSafe(" << std::dec << sizeof(sServiceStatusTlvPayload) << ") Failed!";
        return false;
    }
    if (!m_parse__) { m_payload->struct_init(); }
    if (m_parse__) { class_swap(); }
    return true;
}


