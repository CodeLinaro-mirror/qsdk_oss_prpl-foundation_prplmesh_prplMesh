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
#include <cstring>
#include <tlvf/ieee_1905_1/sVendorOUI.h>
#include <tlvf/swap.h>

using multi_vendor::tlvf_handler;

// Static storage for handler vectors and synchronization
std::array<std::vector<tlvf_handler::tlv_function_t>, tlvf_handler::msgTypeCount>
    tlvf_handler::s_handlers;
std::array<std::vector<tlvf_handler::tlv_parser_function_t>, tlvf_handler::msgTypeCount>
    tlvf_handler::s_parsers;
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

// Register parser
void tlvf_handler::register_parser(ieee1905_1::eMessageType msg_type, tlv_parser_function_t fn)
{
    const std::size_t idx = to_index(msg_type);
    if (idx == INVALID_INDEX) {
        LOG(ERROR) << "register_parser: unknown message type " << msg_type;
        return;
    }

    std::lock_guard<std::mutex> lk(s_mu);
    s_parsers[idx].push_back(fn);
}

// Execute all registered TLV parsers for the given message type
std::shared_ptr<BaseClass> tlvf_handler::parse_vs_tlv(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                      ieee1905_1::eMessageType msg_type)
{
    const std::size_t idx = to_index(msg_type);
    if (idx == INVALID_INDEX) {
        LOG(WARNING) << "parse_vs_tlv: no mapping for message type " << msg_type;
        return nullptr;
    }

    const std::vector<tlv_parser_function_t> &v = s_parsers[idx];
    if (v.empty()) {
        return nullptr;
    }

    // Try each registered parser in order until one successfully handles the TLV
    for (std::size_t i = 0; i < v.size(); ++i) {
        auto parsed_tlv = v[i](cmdu_rx);
        if (parsed_tlv) {
            LOG(DEBUG) << "parse_vs_tlv: parser #" << i
                       << " successfully parsed TLV for message type " << msg_type;
            return parsed_tlv;
        }
    }

    LOG(DEBUG) << "parse_vs_tlv: no parser handled TLV for message type " << msg_type;
    return nullptr;
}

bool tlvf_handler::safe_read_vendor_tlv_data(ieee1905_1::CmduMessageRx &cmdu_rx,
                                             void *vendor_data_out, size_t vendor_data_size,
                                             size_t vendor_data_offset, uint32_t *oui_out)
{
    // Define constants for sizes
    constexpr size_t tlv_header_size = sizeof(ieee1905_1::sTlvHeader);
    constexpr size_t oui_size        = sizeof(sVendorOUI);

    // Get the raw buffer pointer to the current TLV
    uint8_t *tlv_buff = cmdu_rx.getTlvBuffPtr();
    if (!tlv_buff) {
        LOG(ERROR) << "safe_read_vendor_tlv_data: Failed to get TLV buffer pointer";
        return false;
    }

    // Get total message buffer length
    size_t total_buff_len = cmdu_rx.getMessageBuffLength();

    // Calculate current offset in buffer
    uint8_t *msg_buff = cmdu_rx.getMessageBuff();
    if (!msg_buff) {
        LOG(ERROR) << "safe_read_vendor_tlv_data: Failed to get message buffer pointer";
        return false;
    }

    size_t current_offset = tlv_buff - msg_buff;
    if (current_offset >= total_buff_len) {
        LOG(ERROR) << "safe_read_vendor_tlv_data: TLV buffer pointer beyond message buffer";
        return false;
    }

    size_t remaining_bytes = total_buff_len - current_offset;

    // Validate minimum TLV header size
    if (remaining_bytes < tlv_header_size) {
        LOG(ERROR) << "safe_read_vendor_tlv_data: Insufficient bytes for TLV header: "
                   << remaining_bytes << " bytes";
        return false;
    }

    // Now safe to read the length field
    const auto *tlv_hdr = reinterpret_cast<const ieee1905_1::sTlvHeader *>(tlv_buff);
    uint16_t tlv_length = tlv_hdr->length;
    swap_16(tlv_length);

    // Calculate minimum required length: OUI + vendor_data_offset + vendor_data_size
    size_t min_vendor_data_len = oui_size + vendor_data_offset + vendor_data_size;

    // Validate the TLV length field
    if (tlv_length < min_vendor_data_len) {
        LOG(ERROR) << "safe_read_vendor_tlv_data: TLV length too short: " << tlv_length
                   << " bytes (need at least " << min_vendor_data_len << ")";
        return false;
    }

    // Check if buffer contains the complete TLV: header + data
    if (remaining_bytes < (tlv_header_size + tlv_length)) {
        LOG(ERROR) << "safe_read_vendor_tlv_data: Insufficient bytes for complete TLV: need "
                   << (tlv_header_size + tlv_length) << ", have " << remaining_bytes;
        return false;
    }

    // Extract OUI if requested (after TLV header)
    if (oui_out) {
        sVendorOUI vendor_oui = *reinterpret_cast<sVendorOUI *>(tlv_buff + tlv_header_size);
        vendor_oui.struct_swap();
        *oui_out = vendor_oui;
    }

    // Extract vendor-specific data if requested (after TLV header + OUI + vendor_data_offset)
    if (vendor_data_out && vendor_data_size > 0) {
        size_t data_offset = tlv_header_size + oui_size + vendor_data_offset;
        std::memcpy(vendor_data_out, tlv_buff + data_offset, vendor_data_size);
    }

    return true;
}

// Static initializer to register the vendor TLV parser callback with CmduMessageRx
namespace {
struct VendorTlvParserRegistrar {
    VendorTlvParserRegistrar()
    {
        // Register our parse_vs_tlv function as the callback
        ieee1905_1::CmduMessageRx::setVendorTlvParser(&multi_vendor::tlvf_handler::parse_vs_tlv);
    }
} s_vendor_tlv_parser_registrar;
} // namespace
