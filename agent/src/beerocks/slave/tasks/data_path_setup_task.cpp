/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "data_path_setup_task.h"
#include "../agent_db.h"
#include "../son_slave_thread.h"

#include <arpa/inet.h>
#include <easylogging++.h>
#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/wfa_map/tlvDataPathSetupRequest.h>
#include <tlvf/wfa_map/tlvDataPathSetupResponse.h>

namespace beerocks {

DataPathSetupTask::DataPathSetupTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::DATA_PATH_SETUP), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx)
{
}

bool DataPathSetupTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                   const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                                   std::shared_ptr<beerocks_header> beerocks_header)
{
    if (cmdu_rx.getMessageType() != ieee1905_1::eMessageType::DATA_PATH_SETUP_REQUEST_MESSAGE) {
        return false;
    }

    // ========== REQUEST TLV - Parse tlvDataPathSetupRequest ==========
    const auto mid = cmdu_rx.getMessageId();
    auto tlv = cmdu_rx.getClass<wfa_map::tlvDataPathSetupRequest>();
    if (!tlv) {
        LOG(ERROR) << "DataPath Setup Request: getClass<tlvDataPathSetupRequest> failed";
        return true;
    }

    uint8_t add_path = tlv->DataPathSetup().AddRemoveDataPath;
    uint16_t port    = tlv->DestinationPort();
    const uint8_t *addr = tlv->DestinationAddress(0);
    char dest_ip[INET6_ADDRSTRLEN];
    if (!addr || !inet_ntop(AF_INET6, addr, dest_ip, sizeof(dest_ip))) {
        LOG(ERROR) << "DataPath Setup Request: invalid DestinationAddress";
        return true;
    }

    auto db = AgentDB::get();
    db->data_path_entries.push_back(
        AgentDB::sDataPathEntry{std::string(dest_ip), port, add_path});
    LOG(INFO) << "DataPath Setup Request: saved dest=" << dest_ip << " port=" << port
              << " add_path=" << (int)add_path;

    // Send ACK for the Request
    if (!m_cmdu_tx.create(mid, ieee1905_1::eMessageType::ACK_MESSAGE)) {
        LOG(ERROR) << "DataPath Setup Request: failed to create ACK_MESSAGE";
        return false;
    }
    LOG(DEBUG) << "DataPath Setup Request: sending ACK, mid=" << std::hex << mid;
    if (!m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx)) {
        LOG(ERROR) << "DataPath Setup Request: failed to send ACK";
        return false;
    }

    // ========== RESPONSE TLV - Build and send tlvDataPathSetupResponse ==========
    // Call AgentWiFiSensing with (destAddr, destPort)
    // AgentWiFiSensing returns (sourceAddr, sourcePort)
    // Send Datapath Response message to Controller
     bool success = false;
    uint8_t source_addr[16] = {0};
    uint16_t source_port    = 0;

    // TODO: Integrate with AgentWiFiSensing - call setup_datapath(dest_ip, port, add_path)
    // and receive (source_addr, source_port) on success. For now placeholder:
    success = true; // replace with actual WSN/AgentWiFiSensing result
    // if (success) { get source_addr, source_port from AgentWiFiSensing response }
    LOG(DEBUG) << "ash :DataPath Setup Response: enetered to  create DATA_PATH_SETUP_RESPONSE_MESSAGE";

    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::DATA_PATH_SETUP_RESPONSE_MESSAGE)) {
        LOG(ERROR) << "DataPath Setup: failed to create DATA_PATH_SETUP_RESPONSE_MESSAGE";
        return true;
    }
    auto resp_tlv = m_cmdu_tx.addClass<wfa_map::tlvDataPathSetupResponse>();
    if (!resp_tlv) {
        LOG(ERROR) << "DataPath Setup: addClass tlvDataPathSetupResponse failed";
        return true;
    }
    if (success) {
        resp_tlv->ErrorCode() = 0;
        resp_tlv->set_SourceAddr(source_addr, 16);
        resp_tlv->SourcePort() = source_port;
    } else {
        resp_tlv->ErrorCode() = 1;
    }
    if (!resp_tlv->finalize()) {
        LOG(ERROR) << "DataPath Setup: tlvDataPathSetupResponse finalize failed";
        return true;
    }
    LOG(DEBUG) << "ash :DataPath Setup Response: success DATA_PATH_SETUP_RESPONSE_MESSAGE";
    return m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

} // namespace beerocks
