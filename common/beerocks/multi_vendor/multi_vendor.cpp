/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include <algorithm>
#include <chrono>

using multi_vendor::tlvf_handler;

// Static storage for handler vectors and synchronization
std::array<std::vector<tlvf_handler::tlv_function_t>, tlvf_handler::msgTypeCount>
    tlvf_handler::s_handlers;
std::mutex tlvf_handler::s_mu;

namespace {
constexpr std::array<ieee1905_1::eMessageType, tlvf_handler::msgTypeCount> kSupportedTypes = {{
    ieee1905_1::eMessageType::TOPOLOGY_DISCOVERY_MESSAGE,
    ieee1905_1::eMessageType::TOPOLOGY_NOTIFICATION_MESSAGE,
    ieee1905_1::eMessageType::TOPOLOGY_QUERY_MESSAGE,
    ieee1905_1::eMessageType::TOPOLOGY_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::VENDOR_SPECIFIC_MESSAGE,
    ieee1905_1::eMessageType::LINK_METRIC_QUERY_MESSAGE,
    ieee1905_1::eMessageType::LINK_METRIC_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE,
    ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE,
    ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_RENEW_MESSAGE,
    ieee1905_1::eMessageType::PUSH_BUTTON_EVENT_NOTIFICATION_MESSAGE,
    ieee1905_1::eMessageType::PUSH_BUTTON_JOIN_NOTIFICATION_MESSAGE,
    ieee1905_1::eMessageType::HIGHER_LAYER_QUERY_MESSAGE,
    ieee1905_1::eMessageType::HIGHER_LAYER_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::INTERFACE_POWER_CHANGE_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::INTERFACE_POWER_CHANGE_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::GENERIC_PHY_QUERY_MESSAGE,
    ieee1905_1::eMessageType::GENERIC_PHY_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::ACK_MESSAGE,
    ieee1905_1::eMessageType::AP_CAPABILITY_QUERY_MESSAGE,
    ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE,
    ieee1905_1::eMessageType::MULTI_AP_POLICY_CONFIG_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::CHANNEL_PREFERENCE_QUERY_MESSAGE,
    ieee1905_1::eMessageType::CHANNEL_PREFERENCE_REPORT_MESSAGE,
    ieee1905_1::eMessageType::CHANNEL_SELECTION_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::CHANNEL_SELECTION_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::OPERATING_CHANNEL_REPORT_MESSAGE,
    ieee1905_1::eMessageType::CLIENT_CAPABILITY_QUERY_MESSAGE,
    ieee1905_1::eMessageType::CLIENT_CAPABILITY_REPORT_MESSAGE,
    ieee1905_1::eMessageType::AP_METRICS_QUERY_MESSAGE,
    ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::ASSOCIATED_STA_LINK_METRICS_QUERY_MESSAGE,
    ieee1905_1::eMessageType::ASSOCIATED_STA_LINK_METRICS_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::UNASSOCIATED_STA_LINK_METRICS_QUERY_MESSAGE,
    ieee1905_1::eMessageType::UNASSOCIATED_STA_LINK_METRICS_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::BEACON_METRICS_QUERY_MESSAGE,
    ieee1905_1::eMessageType::BEACON_METRICS_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::COMBINED_INFRASTRUCTURE_METRICS_MESSAGE,
    ieee1905_1::eMessageType::CLIENT_STEERING_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::CLIENT_STEERING_BTM_REPORT_MESSAGE,
    ieee1905_1::eMessageType::CLIENT_ASSOCIATION_CONTROL_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::STEERING_COMPLETED_MESSAGE,
    ieee1905_1::eMessageType::HIGHER_LAYER_DATA_MESSAGE,
    ieee1905_1::eMessageType::BACKHAUL_STEERING_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::BACKHAUL_STEERING_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::CHANNEL_SCAN_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::CHANNEL_SCAN_REPORT_MESSAGE,
    ieee1905_1::eMessageType::DPP_CCE_INDICATION_MESSAGE,
    ieee1905_1::eMessageType::IEEE1905_REKEY_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::IEEE1905_DECRYPTION_FAILURE_MESSAGE,
    ieee1905_1::eMessageType::CAC_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::CAC_TERMINATION_MESSAGE,
    ieee1905_1::eMessageType::CLIENT_DISASSOCIATION_STATS_MESSAGE,
    ieee1905_1::eMessageType::SERVICE_PRIORITIZATION_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::ERROR_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::ASSOCIATION_STATUS_NOTIFICATION_MESSAGE,
    ieee1905_1::eMessageType::TUNNELLED_MESSAGE,
    ieee1905_1::eMessageType::BACKHAUL_STA_CAPABILITY_QUERY_MESSAGE,
    ieee1905_1::eMessageType::BACKHAUL_STA_CAPABILITY_REPORT_MESSAGE,
    ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE,
    ieee1905_1::eMessageType::DIRECT_ENCAP_DPP_MESSAGE,
    ieee1905_1::eMessageType::RECONFIGURATION_TRIGGER_MESSAGE,
    ieee1905_1::eMessageType::BSS_CONFIGURATION_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::BSS_CONFIGURATION_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::BSS_CONFIGURATION_RESULT_MESSAGE,
    ieee1905_1::eMessageType::CHIRP_NOTIFICATION_MESSAGE,
    ieee1905_1::eMessageType::IEEE1905_ENCAP_EAPOL_MESSAGE,
    ieee1905_1::eMessageType::DPP_BOOTSTRAPPING_URI_NOTIFICATION_MESSAGE,
    ieee1905_1::eMessageType::FAILED_CONNECTION_MESSAGE,
    ieee1905_1::eMessageType::AGENT_LIST_MESSAGE,
    ieee1905_1::eMessageType::QOS_MANAGEMENT_NOTIFICATION_MESSAGE,
    ieee1905_1::eMessageType::VIRTUAL_BSS_CAPABILITIES_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::VIRTUAL_BSS_CAPABILITIES_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::VIRTUAL_BSS_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::VIRTUAL_BSS_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::CLIENT_SECURITY_CONTEXT_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::CLIENT_SECURITY_CONTEXT_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::TRIGGER_CHANNEL_SWITCH_ANNOUNCEMENT_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::TRIGGER_CHANNEL_SWITCH_ANNOUNCEMENT_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::VIRTUAL_BSS_MOVE_PREPARATION_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::VIRTUAL_BSS_MOVE_PREPARATION_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::VIRTUAL_BSS_MOVE_CANCEL_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::EARLY_AP_CAPABILITY_REPORT_MESSAGE,
    ieee1905_1::eMessageType::AP_MLD_CONFIGURATION_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::AP_MLD_CONFIGURATION_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::BSTA_MLD_CONFIGURATION_REQUEST_MESSAGE,
    ieee1905_1::eMessageType::BSTA_MLD_CONFIGURATION_RESPONSE_MESSAGE,
    ieee1905_1::eMessageType::VIRTUAL_BSS_MOVE_CANCEL_RESPONSE_MESSAGE,
}};

constexpr std::size_t INVALID_INDEX = kSupportedTypes.size();

static_assert(kSupportedTypes.size() == tlvf_handler::msgTypeCount,
              "msgTypeCount must match kSupportedTypes size.");
} // namespace

std::size_t tlvf_handler::to_index(ieee1905_1::eMessageType t)
{
    const auto key = static_cast<int>(t);
    const auto it =
        std::lower_bound(kSupportedTypes.begin(), kSupportedTypes.end(), key,
                         [](ieee1905_1::eMessageType a, int k) { return static_cast<int>(a) < k; });

    if (it == kSupportedTypes.end() || static_cast<int>(*it) != key) {
        return INVALID_INDEX;
    }

    return static_cast<std::size_t>(std::distance(kSupportedTypes.begin(), it));
}

// Register handler
void tlvf_handler::register_handler(ieee1905_1::eMessageType msg_type, tlv_function_t fn)
{
    const std::size_t idx = to_index(msg_type);
    if (idx == INVALID_INDEX) {
        LOG(ERROR) << "register_handler: unknown message type " << msg_type;
        return;
    }

    std::lock_guard<std::mutex> lk(s_mu);
    s_handlers[idx].push_back(fn);
}

// Execute all registered TLV handlers for the given message type
bool tlvf_handler::add_vs_tlv(ieee1905_1::CmduMessageTx &cmdu_tx, ieee1905_1::eMessageType msg_type)
{
    const std::size_t idx = to_index(msg_type);
    if (idx == INVALID_INDEX) {
        LOG(WARNING) << "add_vs_tlv: no mapping for message type " << msg_type;
        return false;
    }

    const std::vector<tlv_function_t> &v = s_handlers[idx];
    if (v.empty()) {
        return true;
    }

    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!v[i](cmdu_tx)) {
            LOG(WARNING) << "add_vs_tlv: handler #" << i << " failed for message type " << msg_type;
        }
    }

    return true;
}
