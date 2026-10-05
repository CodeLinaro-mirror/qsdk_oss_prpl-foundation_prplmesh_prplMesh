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
#include <iomanip>
#include <sstream>
#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/ieee_1905_1/tlvUnknown.h>
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
    LOG(ERROR) << "ash:DataPathReq mid=0x" << std::hex << mid << std::dec
               << " src=" << src_mac << " dst=" << dst_mac
               << " msg_len=" << cmdu_rx.getMessageLength()
               << " buff_len=" << cmdu_rx.getMessageBuffLength()
               << " expected_tlv=0x"
               << std::hex << int(wfa_map::eTlvTypeMap::TLV_DATAPATH_SETUP_REQUEST) << std::dec;

    /* Dump parsed TLVs — if request became tlvUnknown, type/len explain getClass failure. */
    {
        auto unknowns = cmdu_rx.getClassList<ieee1905_1::tlvUnknown>();
        LOG(ERROR) << "ash:DataPathReq tlvUnknown count=" << unknowns.size();
        for (const auto &u : unknowns) {
            if (!u) {
                continue;
            }
            LOG(ERROR) << "ash:DataPathReq unknown TLV type=0x" << std::hex << int(u->type())
                       << " length=" << std::dec << u->length();
        }
    }
    {
        const uint8_t *buf = cmdu_rx.getMessageBuff();
        const size_t len   = cmdu_rx.getMessageLength();
        std::ostringstream oss;
        oss << "ash:DataPathReq hex(";
        oss << len << "):";
        const size_t dump_n = len < 64 ? len : 64;
        for (size_t i = 0; i < dump_n; i++) {
            oss << " " << std::hex << std::setw(2) << std::setfill('0') << int(buf[i]);
        }
        if (len > dump_n) {
            oss << " ...";
        }
        LOG(ERROR) << oss.str();
    }

    auto tlv = cmdu_rx.getClass<wfa_map::tlvDataPathSetupRequest>();
    if (!tlv) {
        LOG(ERROR) << "DataPath Setup Request: getClass<tlvDataPathSetupRequest> failed"
                   << " (CMDU type matched, but TLV class missing — see tlvUnknown/hex above;"
                   << " also grep agent log for 'TLV type mismatch' / 'Not enough available space')";
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
    bool datapath_ok = false;
    if (auto *sensing = m_btl_ctx.get_agent_sensing()) {
        if (add_path) {
            datapath_ok = sensing->setup_datapath();
            if (!datapath_ok) {
                LOG(ERROR) << "DataPath Setup: agent_sensing::setup_datapath failed";
            } else {
                LOG(DEBUG) << "DataPath Setup: agent_sensing::setup_datapath success";
            }
        } else {
            datapath_ok = sensing->RemoveLayer3Path();
            if (!datapath_ok) {
                LOG(ERROR) << "DataPath Setup: agent_sensing::RemoveLayer3Path failed";
            } else {
                LOG(DEBUG) << "DataPath Setup: agent_sensing::RemoveLayer3Path success";
            }
        }
    } else {
        LOG(ERROR) << "DataPath Setup: agent_sensing is null";
    }

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
    // setup_datapath() stores srcIp/srcPort on the last AgentDB::sDataPathEntry (agent_db.h)

    bool success = datapath_ok;
    uint8_t source_addr[16] = {0};
    uint16_t source_port    = 0;

    if (success && add_path) {
        if (db->data_path_entries.empty()) {
            LOG(ERROR) << "DataPath Setup Response: no data_path_entries after setup_datapath";
            success = false;
        } else {
            const auto &path_entry = db->data_path_entries.back();
            source_port            = path_entry.source_port;
            if (inet_pton(AF_INET6, path_entry.source_addr.c_str(), source_addr) != 1) {
                LOG(ERROR) << "DataPath Setup Response: invalid source_addr from AgentDB: "
                           << path_entry.source_addr;
                success = false;
            }
        }
    }

    LOG(DEBUG) << "DataPath Setup Response: create DATA_PATH_SETUP_RESPONSE_MESSAGE";

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
