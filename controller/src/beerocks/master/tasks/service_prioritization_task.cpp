/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "service_prioritization_task.h"

#include "../controller.h"

#include <bcl/beerocks_string_utils.h>
#include <tlvf/wfa_map/tlvQoSManagementDescriptor.h>

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

    auto controller = m_db.get_controller_ctx();
    if (!controller) {
        LOG(ERROR) << "Failed to get controller context";
        return false;
    }

    bool updated_descriptors = false;

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

        const auto bssid_owner =
            m_db.get_bss_parent_agent(qos_management_descriptor_tlv_in->bssid());
        if (bssid_owner != src_mac) {
            LOG(ERROR) << "Rejecting QoS Management Descriptor TLV for BSSID "
                       << qos_management_descriptor_tlv_in->bssid() << " from agent " << src_mac
                       << " because it belongs to agent " << bssid_owner;
            return false;
        }

        const auto descriptor_element_length =
            qos_management_descriptor_tlv_in->descriptor_element_length();
        auto descriptor_element = qos_management_descriptor_tlv_in->descriptor_element();
        if (!descriptor_element || descriptor_element_length == 0) {
            LOG(ERROR) << "Descriptor element is empty for BSSID "
                       << qos_management_descriptor_tlv_in->bssid() << ", client "
                       << qos_management_descriptor_tlv_in->client_mac();
            return false;
        }

        if (!m_db.set_controller_qm_descriptor(
                qos_management_descriptor_tlv_in->bssid(),
                qos_management_descriptor_tlv_in->client_mac(),
                beerocks::string_utils::bytes_to_hex_string(descriptor_element,
                                                            descriptor_element_length))) {
            LOG(ERROR) << "Failed to update controller QoS management descriptor for BSSID "
                       << qos_management_descriptor_tlv_in->bssid() << ", client "
                       << qos_management_descriptor_tlv_in->client_mac();
            return false;
        }

        updated_descriptors = true;
    }

    if (updated_descriptors) {
        controller->trigger_prioritization_config();
    }

    // TODO: PPM-3990: optimize QoS Management Descriptor propagation.
    return true;
}

} // namespace son
