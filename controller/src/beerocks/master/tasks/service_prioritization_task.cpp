/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "service_prioritization_task.h"

#include "son_actions.h"

#include <tlvf/wfa_map/tlvDscpMappingTable.h>
#include <tlvf/wfa_map/tlvQoSManagementDescriptor.h>
#include <tlvf/wfa_map/tlvQoSManagementPolicy.h>
#include <tlvf/wfa_map/tlvServicePrioritizationRule.h>

namespace son {

service_prioritization_task::service_prioritization_task(db &database_,
                                                         ieee1905_1::CmduMessageTx &cmdu_tx_,
                                                         const std::string &task_name_)
    : task(task_name_), m_db(database_), m_cmdu_tx(cmdu_tx_)
{
}

bool service_prioritization_task::handle_ieee1905_1_msg(const sMacAddr &src_mac,
                                                        ieee1905_1::CmduMessageRx &cmdu_rx)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::QOS_MANAGEMENT_NOTIFICATION_MESSAGE:
        return handle_cmdu_1905_qos_management_notification_message(src_mac, cmdu_rx);
    default: {
        return false;
    }
    }
}

bool service_prioritization_task::handle_cmdu_1905_qos_management_notification_message(
    const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto mid = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received QOS_MANAGEMENT_NOTIFICATION_MESSAGE, mid=" << std::hex << mid;

    // --- Prepare SERVICE_PRIORITIZATION_REQUEST_MESSAGE to be sent to all agents ---
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::SERVICE_PRIORITIZATION_REQUEST_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type SERVICE_PRIORITIZATION_REQUEST_MESSAGE has failed";
        return false;
    }

    // --- Handle Zero or more Service Prioritization Rule TLVs ---
    auto agent = m_db.m_agents.get(src_mac);
    if (!agent) {
        LOG(ERROR) << "Agent with mac is not found in database mac=" << src_mac;
        return false;
    }

    for (const auto &rule : agent->service_prioritization.rules) {

        auto service_prioritization_rule_tlv =
            m_cmdu_tx.addClass<wfa_map::tlvServicePrioritizationRule>();
        if (!service_prioritization_rule_tlv) {
            LOG(ERROR) << "addClass wfa_map::tlvServicePrioritizationRule has failed";
            return false;
        }

        service_prioritization_rule_tlv->rule_params() = rule.second;
    }

    // --- Handle Zero or one DSCP Mapping Table TLV  ---
    auto dscp_mapping_table_tlv = m_cmdu_tx.addClass<wfa_map::tlvDscpMappingTable>();
    if (!dscp_mapping_table_tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvDscpMappingTable has failed";
        return false;
    }

    if (!dscp_mapping_table_tlv->set_dscp_pcp_mapping(
            agent->service_prioritization.dscp_mapping_table.data(),
            agent->service_prioritization.dscp_mapping_table.size())) {
        LOG(ERROR) << "Failed to set DSCP mapping list in tlvDscpMappingTable!";
        return false;
    }

    if (!m_db.dm_set_service_prioritization_rules(*agent)) {
        LOG(ERROR) << "Failed to set service prioritization rules in DM for Agent" << agent->al_mac;
        return false;
    }

    // --- Handle Zero or more QoS Management Descriptor TLVs ---
    for (auto const &qos_management_descriptor_tlv_in :
         cmdu_rx.getClassList<wfa_map::tlvQoSManagementDescriptor>()) {
        if (!qos_management_descriptor_tlv_in) {
            LOG(DEBUG) << "getClass wfa_map::tlvQoSManagementDescriptor has failed";
            return false;
        }

        LOG(DEBUG) << "QoS Management Descriptor TLV is received:" << std::endl
                   << "Rule ID: " << qos_management_descriptor_tlv_in->qmid() << std::endl
                   << "BSSID: " << qos_management_descriptor_tlv_in->bssid() << std::endl
                   << "Client MAC: " << qos_management_descriptor_tlv_in->client_mac() << std::endl;

        m_descriptors[m_qmid_next] = qos_management_descriptor_tlv_in;

        // forwarding the QoS Management Descriptor TLVs to the agent with specified QMIDs
        auto qos_management_descriptor_tlv_out =
            m_cmdu_tx.addClass<wfa_map::tlvQoSManagementDescriptor>();
        if (!qos_management_descriptor_tlv_out) {
            LOG(ERROR) << "addClass wfa_map::tlvQoSManagementDescriptor for output has failed";
            return false;
        }
        qos_management_descriptor_tlv_out->bssid() = qos_management_descriptor_tlv_in->bssid();
        qos_management_descriptor_tlv_out->client_mac() =
            qos_management_descriptor_tlv_in->client_mac();
        qos_management_descriptor_tlv_out->qmid() = m_qmid_next;
        qos_management_descriptor_tlv_out->set_descriptor_element(
            qos_management_descriptor_tlv_in->descriptor_element(),
            qos_management_descriptor_tlv_in->descriptor_element_length());

        m_qmid_next++;
    }

    // TODO: PPM-3268: Check for agent SCS/MSCS capabilities before sending
    bool result = true;
    for (const auto &agent_entry : m_db.m_agents) {
        result = son_actions::send_cmdu_to_agent(agent_entry.first, m_cmdu_tx, m_db);
        if (!result) {
            LOG(ERROR) << "Failed to send SERVICE_PRIORITIZATION_REQUEST_MESSAGE to agent "
                       << agent_entry.first;
            result = false;
        }
    }

    return result;
}

} // namespace son
