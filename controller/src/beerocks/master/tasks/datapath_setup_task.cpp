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
#include <algorithm>
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
                                         const sMacAddr &agent_mac,bool use_udp,
                                         const std::string &task_name)
    : task(task_name), m_database(database), m_cmdu_tx(cmdu_tx), m_tasks(tasks),
      m_dest_ip(dest_ip), m_dest_port(dest_port), m_add_path(add_path),  m_use_udp(use_udp),m_agent_mac(agent_mac)
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
  
    datapath_request_tlv->TransportProtocol().UDP_over_IPv6 = m_use_udp ? 1 : 0;
    datapath_request_tlv->TransportProtocol().TCP_over_IPv6 = m_use_udp ? 0 : 1;
    
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
                   << " add_path=" << m_add_path
		    << " protocol=" << (m_use_udp ? "UDP" : "TCP");
    m_database.data_path_entries.emplace_back();
    auto &entry       = m_database.data_path_entries.back();
    entry.dest_ip     = m_dest_ip;
    entry.dest_port   = m_dest_port;
    entry.add_path    = m_add_path;
    entry.agent_mac   = m_agent_mac;

    TASK_LOG(INFO) << "DataPath setup request sent success: stored db entry for agent_mac="
                   << entry.agent_mac << " dest=" << entry.dest_ip
                   << " port=" << entry.dest_port;
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
	 char src_addr_str[INET6_ADDRSTRLEN] = {0};
        if (!src_addr || !inet_ntop(AF_INET6, src_addr, src_addr_str, sizeof(src_addr_str))) {
            LOG(ERROR) << "ash :DataPath setup response success but SourceAddr is invalid";
        } else {
            auto entry_it =
                std::find_if(database.data_path_entries.rbegin(), database.data_path_entries.rend(),
                             [&](const db::sDataPathEntry &entry) {
                                 return entry.agent_mac == src_mac;
                             });
            if (entry_it == database.data_path_entries.rend()) {
                LOG(ERROR) << "ash :DataPath setup response: no data_path_entries for agent "
                           << src_mac;
            } else {
                auto &entry       = *entry_it;
                entry.source_addr = src_addr_str;
                entry.source_port = src_port;

                LOG(INFO) << "ash :DataPath setup response success, source_addr="
                          << entry.source_addr << " source_port=" << entry.source_port;

                const std::string device_dm_path = database.get_agent_data_model_path(src_mac);
                if (!device_dm_path.empty()) {
                    auto ambiorix_dm = database.get_ambiorix_obj();
                    if (ambiorix_dm) {
                        if (!ambiorix_dm->set(device_dm_path, "DatapathIPAddress",
                                              std::string(src_addr_str))) {
                            LOG(ERROR) << "Failed to set " << device_dm_path
                                       << ".DatapathIPAddress";
                        }
                        const uint32_t src_port_u32 = static_cast<uint32_t>(src_port);
                        if (!ambiorix_dm->set(device_dm_path, "DatapathPort", src_port_u32)) {
                            LOG(ERROR) << "Failed to set " << device_dm_path << ".DatapathPort";
                       }
                    }
                } else {
                    LOG(WARNING) << "DatapathIPAddress/DatapathPort not updated: no data model "
                                    "path for agent "
                                 << src_mac;
                }
            }
        }
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
