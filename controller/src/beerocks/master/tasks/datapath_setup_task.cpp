/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "datapath_setup_task.h"
#include "../son_actions.h"

#include <arpa/inet.h>
#include <bcl/network/network_utils.h>
#include <easylogging++.h>
#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/wfa_map/tlvDataPathSetupRequest.h>
#include <tlvf/wfa_map/tlvDataPathSetupResponse.h>

using namespace beerocks;
using namespace net;
using namespace son;

datapath_setup_task::datapath_setup_task(db &database, ieee1905_1::CmduMessageTx &cmdu_tx,
                                         task_pool &tasks, const std::string &dest_ip,
                                         uint16_t dest_port, bool add_path,
                                         const sMacAddr &agent_mac,
                                         const std::string &task_name)
    : task(task_name), m_database(database), m_cmdu_tx(cmdu_tx), m_tasks(tasks),
      m_dest_ip(dest_ip), m_dest_port(dest_port), m_add_path(add_path), m_agent_mac(agent_mac)
{
    TASK_LOG(DEBUG) << "datapath_setup_task constructed";
}

void datapath_setup_task::work()
{
    TASK_LOG(DEBUG) << "datapath_setup_task work: send DATA_PATH_SETUP_REQUEST";

    uint8_t ipv6_bin[16];
    if (inet_pton(AF_INET6, m_dest_ip.c_str(), ipv6_bin) != 1) {
        TASK_LOG(ERROR) << "Invalid IPv6 address: " << m_dest_ip;
        finish();
        return;
    }

    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::DATA_PATH_SETUP_REQUEST_MESSAGE)) {
        TASK_LOG(ERROR) << "Failed building DATA_PATH_SETUP_REQUEST_MESSAGE";
        finish();
        return;
    }

    auto datapath_request_tlv = m_cmdu_tx.addClass<wfa_map::tlvDataPathSetupRequest>();
    if (!datapath_request_tlv) {
        TASK_LOG(ERROR) << "addClass tlvDataPathSetupRequest failed";
        finish();
        return;
    }

    datapath_request_tlv->DataPathSetup().AddRemoveDataPath = m_add_path;
    datapath_request_tlv->DataPathSetup().reserved          = 0;

    datapath_request_tlv->TransportProtocol().UDP_over_IPv6 = 1;
    datapath_request_tlv->TransportProtocol().TCP_over_IPv6 = 0;
    datapath_request_tlv->TransportProtocol().reserved      = 0;

    if (!datapath_request_tlv->set_DestinationAddress(ipv6_bin, 16)) {
        TASK_LOG(ERROR) << "set_DestinationAddress failed";
        finish();
        return;
    }
    datapath_request_tlv->DestinationPort() = m_dest_port;

    if (!datapath_request_tlv->finalize()) {
        TASK_LOG(ERROR) << "TLV finalize failed";
        finish();
        return;
    }

    if (!son_actions::send_cmdu_to_agent(m_agent_mac, m_cmdu_tx, m_database)) {
        TASK_LOG(ERROR) << "send_cmdu_to_agent failed";
        finish();
        return;
    }

    TASK_LOG(INFO) << "DataPath setup request sent, agent_mac=" << m_agent_mac
                   << " dest=" << m_dest_ip << " port=" << m_dest_port
                   << " add_path=" << m_add_path;
    finish();
}

bool datapath_setup_task::handle_data_path_setup_response(
    db &database, ieee1905_1::CmduMessageTx &cmdu_tx, const sMacAddr &src_mac,
    ieee1905_1::CmduMessageRx &cmdu_rx)
{
    const auto mid = cmdu_rx.getMessageId();
    LOG(INFO) << "ash :DATA_PATH_SETUP_RESPONSE from " << src_mac << ", mid=" << std::hex << mid;

    auto resp_tlv = cmdu_rx.getClass<wfa_map::tlvDataPathSetupResponse>();
    if (!resp_tlv) {
        LOG(ERROR) << "getClass<tlvDataPathSetupResponse> failed";
        return false;
    }

    const uint8_t err = resp_tlv->ErrorCode();
    if (err == 0) {
        const uint8_t *src_addr = resp_tlv->SourceAddr(0);
        const uint16_t src_port = resp_tlv->SourcePort();
        (void)src_addr;
        LOG(INFO) << "ash :DataPath setup response success, source_port=" << src_port;
    } else {
        LOG(WARNING) << "ash :DataPath setup response failed, error_code=" << static_cast<int>(err);
    }

    if (!cmdu_tx.create(mid, ieee1905_1::eMessageType::ACK_MESSAGE)) {
        LOG(ERROR) << "Failed to create ACK for DATA_PATH_SETUP_RESPONSE";
        return false;
    }
    LOG(INFO) << "ash :create ACK for DATA_PATH_SETUP_RESPONSE";
    return son_actions::send_cmdu_to_agent(src_mac, cmdu_tx, database);
}
