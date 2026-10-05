/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "sensing_exchange_task.h"
#include "../son_actions.h"

#include <easylogging++.h>
#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/wfa_map/tlvSensingExchangeRequest.h>
#include <tlvf/wfa_map/tlvSensingExchangeResponse.h>

using namespace beerocks;
using namespace son;

namespace {
constexpr uint16_t STATUS_SUCCESS                  = 0x0000;
constexpr uint16_t STATUS_DATA_PATH_DOES_NOT_EXIST = 0x1001;
}

sensing_exchange_task::sensing_exchange_task(db &database,
                                             ieee1905_1::CmduMessageTx &cmdu_tx,
                                             task_pool &tasks,
                                             uint32_t exchange_id,
                                             bool add_exchange,
                                             uint8_t exchange_type,
                                             uint16_t rate_tu10,
                                             uint16_t bandwidth_mhz,
                                             uint16_t ntx,
                                             uint16_t nrx,
                                             uint32_t data_type_mask,
                                             uint8_t csi_threshold,
                                             bool tx_mac_valid,
                                             bool rx_mac_valid,
                                             const sMacAddr &tx_mac,
                                             const sMacAddr &rx_mac,
                                             const sMacAddr &agent_mac,
                                             const std::string &task_name)
    : task(task_name)
    , m_database(database)
    , m_cmdu_tx(cmdu_tx)
    , m_tasks(tasks)
    , m_exchange_id(exchange_id)
    , m_add_exchange(add_exchange)
    , m_exchange_type(exchange_type)
    , m_rate_tu10(rate_tu10)
    , m_bandwidth_mhz(bandwidth_mhz)
    , m_ntx(ntx)
    , m_nrx(nrx)
    , m_data_type_mask(data_type_mask)
    , m_csi_threshold(csi_threshold)
    , m_tx_mac_valid(tx_mac_valid)
    , m_rx_mac_valid(rx_mac_valid)
    , m_tx_mac(tx_mac)
    , m_rx_mac(rx_mac)
    , m_agent_mac(agent_mac)
{
}

void sensing_exchange_task::work()
{
    if (!send_sensing_exchange_request()) {
        finish();
        return;
    }

    finish();
}

bool sensing_exchange_task::send_sensing_exchange_request()
{
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::SENSING_EXCHANGE_REQUEST_MESSAGE)) {
        TASK_LOG(ERROR) << "Failed building SENSING_EXCHANGE_REQUEST_MESSAGE";
        return false;
    }

    auto tlvSensingExchangeRequest = m_cmdu_tx.addClass<wfa_map::tlvSensingExchangeRequest>();
    if (!tlvSensingExchangeRequest) {
        TASK_LOG(ERROR) << "addClass tlvSensingExchangeRequest failed";
        return false;
    }

    tlvSensingExchangeRequest->ExchangeID() = m_exchange_id;

    tlvSensingExchangeRequest->AddRemoveFlags().AddRemove = m_add_exchange ? 1 : 0;
    tlvSensingExchangeRequest->AddRemoveFlags().reserved  = 0;

    tlvSensingExchangeRequest->ExchangeType() = m_exchange_type;
    tlvSensingExchangeRequest->Rate()         = m_rate_tu10;
    tlvSensingExchangeRequest->Bandwidth()    = m_bandwidth_mhz;
    tlvSensingExchangeRequest->NTx()          = m_ntx;
    tlvSensingExchangeRequest->NRx()          = m_nrx;
    tlvSensingExchangeRequest->DataType()     = m_data_type_mask;
    tlvSensingExchangeRequest->CSI_Threshold() = m_csi_threshold;

    tlvSensingExchangeRequest->MacAddrValid().Transmitter_MAC_Address_Valid =
        m_tx_mac_valid ? 1 : 0;
    tlvSensingExchangeRequest->MacAddrValid().Receiver_MAC_Address_Valid =
        m_rx_mac_valid ? 1 : 0;
    tlvSensingExchangeRequest->MacAddrValid().reserved = 0;

    if (m_tx_mac_valid) {
        tlvSensingExchangeRequest->Transmitter_MAC_Address() = m_tx_mac;
    }
    if (m_rx_mac_valid) {
        tlvSensingExchangeRequest->Receiver_MAC_Address() = m_rx_mac;
    }

    if (!tlvSensingExchangeRequest->finalize()) {
        TASK_LOG(ERROR) << "tlvSensingExchangeRequest finalize failed";
        return false;
    }

    sMacAddr dst = m_agent_mac;
    if (!son_actions::send_cmdu_to_agent(dst, m_cmdu_tx, m_database)) {
        TASK_LOG(ERROR) << "send_cmdu_to_agent failed";
        return false;
    }

    TASK_LOG(INFO) << "Sensing Exchange Request sent successfully: mid=" << std::hex
                   << m_cmdu_tx.getMessageId()
                   << " exchange_id=" << std::dec << m_exchange_id
                   << " add=" << int(m_add_exchange)
                   << " type=" << int(m_exchange_type)
                   << " rate(10TU)=" << m_rate_tu10
                   << " bw(MHz)=" << m_bandwidth_mhz
                   << " ntx=" << m_ntx
                   << " nrx=" << m_nrx;

    return true;
}

bool sensing_exchange_task::handle_sensing_exchange_response(
    db &database,
    ieee1905_1::CmduMessageTx &cmdu_tx,
    const sMacAddr &src_mac,
    ieee1905_1::CmduMessageRx &cmdu_rx)
{
    const auto mid = cmdu_rx.getMessageId();

    auto tlv = cmdu_rx.getClass<wfa_map::tlvSensingExchangeResponse>();
    if (!tlv) {
        LOG(ERROR) << "getClass<tlvSensingExchangeResponse> failed";
        return false;
    }

    const uint32_t exchange_id = tlv->exchange_id();
    const uint16_t error_code  = tlv->error_code();

    if (error_code == STATUS_SUCCESS) {
        LOG(INFO) << "Sensing Exchange success, ExchangeID=" << exchange_id;
    } else if (error_code == STATUS_DATA_PATH_DOES_NOT_EXIST) {
        LOG(WARNING) << "Sensing Exchange failed: data path does not exist, ExchangeID="
                     << exchange_id;
    } else {
        LOG(WARNING) << "Sensing Exchange failed, ExchangeID=" << exchange_id
                     << " error_code=0x" << std::hex << error_code << std::dec;
    }

    if (!cmdu_tx.create(mid, ieee1905_1::eMessageType::ACK_MESSAGE)) {
        LOG(ERROR) << "Exchange: Failed to create ACK for SENSING_EXCHANGE_RESPONSE_MESSAGE";
        return false;
    }

    return son_actions::send_cmdu_to_agent(src_mac, cmdu_tx, database);
}
