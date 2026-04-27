/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "service_prioritization_task.h"
#include "../agent_db.h"
#include "../son_slave_thread.h"
#include "../tid_to_link_utils.h"
#include <beerocks/tlvf/beerocks_message_apmanager.h>

#include <bcl/beerocks_utils.h>
#include <bcl/network/network_utils.h>
#include <bpl/bpl_service_prio_utils.h>
#include <tlvf/wfa_map/tlvDscpMappingTable.h>
#include <tlvf/wfa_map/tlvProfile2ErrorCode.h>
#include <tlvf/wfa_map/tlvQoSManagementDescriptor.h>
#include <tlvf/wfa_map/tlvTidToLinkMappingPolicy.h>

#include <vector>

namespace beerocks {
using namespace net;

constexpr int ServicePrioritizationTask::WDS_RETRY_TIMEOUT_MS;

ServicePrioritizationTask::ServicePrioritizationTask(slave_thread &btl_ctx,
                                                     ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::SERVICE_PRIORITIZATION), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx)
{
    service_prio_utils = bpl::register_service_prio_utils();
    if (!service_prio_utils) {
        LOG(ERROR) << "failed to register service prio utils";
    }
}

bool ServicePrioritizationTask::clear_configuration()
{
    m_pending_wds_ifaces.clear();
    clear_scheduled_work();
    return qos_flush_setup();
}

bool ServicePrioritizationTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx,
                                            uint32_t iface_index, const sMacAddr &dst_mac,
                                            const sMacAddr &src_mac, int fd,
                                            std::shared_ptr<beerocks_header> beerocks_header)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::SERVICE_PRIORITIZATION_REQUEST_MESSAGE: {
        handle_service_prioritization_request(cmdu_rx, src_mac);
        break;
    }
    default: {
        // Message was not handled, therefore return false.
        return false;
    }
    }
    return true;
}

void ServicePrioritizationTask::work()
{
    if (!should_run_now()) {
        return;
    }

    clear_scheduled_work();

    if (!retry_pending_wds_ifaces()) {
        LOG(WARNING) << "pending WDS QoS retry failed";
    }
}

void ServicePrioritizationTask::handle_event(uint8_t event_enum_value, const void *event_obj)
{
    switch (eEvent(event_enum_value)) {
    case QOS_NEW_WDS_IFACE: {
        if (!event_obj) {
            LOG(ERROR) << "QOS_NEW_WDS_IFACE requires event payload";
            break;
        }

        auto iface_name = static_cast<const char *>(event_obj);
        if (!handle_new_wds_iface(iface_name)) {
            LOG(WARNING) << "add WDS QoS iface failed";
        }
        break;
    }
    case QOS_CLEAR_WDS_IFACE: {
        if (!event_obj) {
            LOG(ERROR) << "QOS_CLEAR_WDS_IFACE requires event payload";
            break;
        }

        if (!service_prio_utils) {
            LOG(ERROR) << "Service Priority Utilities are not found";
            break;
        }

        auto iface_name = static_cast<const char *>(event_obj);
        if (!handle_clear_wds_iface(iface_name)) {
            LOG(WARNING) << "clear WDS QoS iface failed";
        }
        break;
    }
    default:
        break;
    }
}

void ServicePrioritizationTask::run_at(std::chrono::steady_clock::time_point due)
{
    m_pending = true;

    if (m_next_run == std::chrono::steady_clock::time_point::min() || due < m_next_run) {
        m_next_run = due;
    }
}

void ServicePrioritizationTask::request_wds_retry(const std::string &iface_name,
                                                  std::chrono::steady_clock::time_point not_before,
                                                  std::chrono::steady_clock::time_point deadline)
{
    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return;
    }

    auto pending_it = m_pending_wds_ifaces.find(iface_name);
    if (pending_it == m_pending_wds_ifaces.end()) {
        pending_it =
            m_pending_wds_ifaces.emplace(iface_name, sPendingWdsIfaceState{not_before, deadline})
                .first;
    }

    run_at(pending_it->second.not_before);
}

void ServicePrioritizationTask::clear_scheduled_work()
{
    m_pending  = false;
    m_next_run = std::chrono::steady_clock::time_point::min();
}

bool ServicePrioritizationTask::should_run_now() const
{
    if (!m_pending) {
        return false;
    }
    return std::chrono::steady_clock::now() >= m_next_run;
}

bool ServicePrioritizationTask::handle_new_wds_iface(const std::string &iface_name)
{
    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return false;
    }

    const auto now  = std::chrono::steady_clock::now();
    auto pending_it = m_pending_wds_ifaces.find(iface_name);
    if (pending_it == m_pending_wds_ifaces.end()) {
        const auto settle_due = now + std::chrono::milliseconds(WDS_SETTLE_MS);
        const auto timeout_at = now + std::chrono::milliseconds(WDS_RETRY_TIMEOUT_MS);

        LOG(DEBUG) << "Deferring WDS QoS apply for settle window: " << iface_name;
        request_wds_retry(iface_name, settle_due, timeout_at);
        return true;
    }

    if (now < pending_it->second.not_before) {
        run_at(pending_it->second.not_before);
        return true;
    }

    const bool iface_exists = network_utils::linux_iface_exists(iface_name);
    const bool iface_ready =
        iface_exists && network_utils::linux_iface_is_up_and_running(iface_name);

    if (!iface_ready) {
        if (now >= pending_it->second.deadline) {
            if (!iface_exists) {
                LOG(WARNING) << "WDS iface is still missing after " << WDS_RETRY_TIMEOUT_MS
                             << "ms, skip QoS apply iface=" << iface_name;
            } else {
                LOG(WARNING) << "WDS iface is still not up and running after "
                             << WDS_RETRY_TIMEOUT_MS << "ms, skip QoS apply iface=" << iface_name;
            }
            m_pending_wds_ifaces.erase(pending_it);
            return true;
        }

        auto retry_due = now + std::chrono::milliseconds(DEBOUNCE_MS);
        if (retry_due > pending_it->second.deadline) {
            retry_due = pending_it->second.deadline;
        }
        run_at(retry_due);
        return true;
    }

    if (!qos_apply_active_rule()) {
        if (now < pending_it->second.deadline) {
            auto retry_due = now + std::chrono::milliseconds(DEBOUNCE_MS);
            if (retry_due > pending_it->second.deadline) {
                retry_due = pending_it->second.deadline;
            }
            run_at(retry_due);
            return true;
        }

        m_pending_wds_ifaces.erase(pending_it);
        LOG(WARNING) << "Failed setting up QoS active rule on WDS iface=" << iface_name;
        return false;
    }

    m_pending_wds_ifaces.erase(pending_it);
    return true;
}

bool ServicePrioritizationTask::handle_clear_wds_iface(const std::string &iface_name)
{
    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return false;
    }

    m_pending_wds_ifaces.erase(iface_name);
    if (m_pending_wds_ifaces.empty()) {
        clear_scheduled_work();
    }

    if (!service_prio_utils) {
        LOG(ERROR) << "Service Priority Utilities are not found";
        return false;
    }

    return service_prio_utils->flush_iface_rules(iface_name);
}

bool ServicePrioritizationTask::retry_pending_wds_ifaces()
{
    if (m_pending_wds_ifaces.empty()) {
        return true;
    }

    bool success = true;
    std::vector<std::string> ifaces;
    ifaces.reserve(m_pending_wds_ifaces.size());

    for (const auto &pending_iface : m_pending_wds_ifaces) {
        ifaces.push_back(pending_iface.first);
    }

    for (const auto &iface_name : ifaces) {
        if (!handle_new_wds_iface(iface_name)) {
            success = false;
        }
    }

    return success;
}

void ServicePrioritizationTask::handle_service_prioritization_request(
    ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &src_mac)
{
    const auto mid = cmdu_rx.getMessageId();

    LOG(DEBUG) << "Received SERVICE_PRIORITIZATION_REQUEST_MESSAGE, mid=" << std::hex << mid;

    m_cmdu_tx.create(mid, ieee1905_1::eMessageType::ACK_MESSAGE);

    LOG(DEBUG) << "Sending ACK message to the originator, mid=" << std::hex << mid;
    m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);

    auto service_prioritization_rules =
        cmdu_rx.getClassList<wfa_map::tlvServicePrioritizationRule>();

    // Split rules to lists of rules to remove and rules to add.
    // If rule is being added, but it already exist only overwite it.
    std::vector<std::shared_ptr<wfa_map::tlvServicePrioritizationRule>> rules_to_remove;
    std::vector<std::shared_ptr<wfa_map::tlvServicePrioritizationRule>> rules_to_add;
    auto db = AgentDB::get();
    for (auto &rule : service_prioritization_rules) {
        LOG(DEBUG) << "Service Prioritization Rule TLV Dump" << std::endl
                   << "Rule id=" << rule->rule_params().id << std::endl
                   << "add_remove=" << rule->rule_params().bits_field1.add_remove << std::endl
                   << "precedence=" << rule->rule_params().precedence << std::endl
                   << "output=" << rule->rule_params().output << std::endl
                   << "always_match=" << rule->rule_params().bits_field2.always_match;
        // Remove
        if (!rule->rule_params().bits_field1.add_remove) {
            rules_to_remove.push_back(rule);
            continue;
        }

        auto rule_found_it = db->service_prioritization.rules.find(rule->rule_params().id);

        // Overwrite existing rule
        if (rule_found_it != db->service_prioritization.rules.end()) {
            rule_found_it->second = rule->rule_params();
            continue;
        }

        // Rule to Add
        rules_to_add.push_back(rule);
    }

    // Prepare error response message, in case we will need to fill it.
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::ERROR_RESPONSE_MESSAGE)) {
        LOG(ERROR) << "CMDU creation has failed";
        return;
    }

    for (const auto &rule_to_remove : rules_to_remove) {
        auto rule_found_it =
            db->service_prioritization.rules.find(rule_to_remove->rule_params().id);
        if (rule_found_it != db->service_prioritization.rules.end()) {
            db->service_prioritization.rules.erase(rule_found_it);
            continue;
        }
        // If we were asked to remove a rule we don't have, add Profile-2 Error Code TLV.
        auto profile2_error_code_tlv = m_cmdu_tx.addClass<wfa_map::tlvProfile2ErrorCode>();
        if (!profile2_error_code_tlv) {
            LOG(ERROR) << "addClass has failed";
            return;
        }
        profile2_error_code_tlv->reason_code() =
            wfa_map::tlvProfile2ErrorCode::eReasonCode::SERVICE_PRIORITIZATION_RULE_NOT_FOUND;
        profile2_error_code_tlv->set_service_prioritization_rule_id(
            rule_to_remove->rule_params().id);
    }

    for (const auto &rule_to_add : rules_to_add) {
        // '1' is the current maximum allowed ruled specified in the
        // tlvProfile2ApCapability::max_prioritization_rules
        if (db->service_prioritization.rules.size() >= db->device_conf.max_prioritization_rules) {
            auto profile2_error_code_tlv = m_cmdu_tx.addClass<wfa_map::tlvProfile2ErrorCode>();
            if (!profile2_error_code_tlv) {
                LOG(ERROR) << "addClass has failed";
                return;
            }
            profile2_error_code_tlv->reason_code() = wfa_map::tlvProfile2ErrorCode::
                NUMBER_OF_SERVICE_PRIORITIZATION_RULES_EXCEEDED_THE_MAXIMUM_SUPPORTED;
            profile2_error_code_tlv->set_service_prioritization_rule_id(
                rule_to_add->rule_params().id);
            break;
        }

        db->service_prioritization.rules[rule_to_add->rule_params().id] =
            rule_to_add->rule_params();
    }

    // If added Profile2ErrorCode TLVs, send the ERROR_RESPONSE_MESSAGE.
    if (m_cmdu_tx.getClass<wfa_map::tlvProfile2ErrorCode>()) {
        m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
    }

    auto dscp_mapping_table_tlv = cmdu_rx.getClass<wfa_map::tlvDscpMappingTable>();
    if (dscp_mapping_table_tlv) {
        auto dscp_mapping_table = dscp_mapping_table_tlv->dscp_pcp_mapping(0);
        std::copy(dscp_mapping_table, dscp_mapping_table + 64,
                  db->service_prioritization.dscp_mapping_table.begin());
    }

    if (!handle_qos_management_descriptors(cmdu_rx)) {
        LOG(ERROR) << "Failed handling QoS management descriptors";
    }

    if (!qos_apply_active_rule()) {
        LOG(ERROR) << "Failed setting up QoS active rule";
    }

    // Tid-To-Link Mapping policy TLV Handler
    auto tlvTidToLinkMapping = cmdu_rx.getClass<wfa_map::tlvTidToLinkMappingPolicy>();
    if (tlvTidToLinkMapping) {
        handle_tid_to_link_mapping_policy_tlv(tlvTidToLinkMapping);
    }
}

bool ServicePrioritizationTask::handle_qos_management_descriptors(
    ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto qos_mgmt_descriptors = cmdu_rx.getClassList<wfa_map::tlvQoSManagementDescriptor>();
    if (qos_mgmt_descriptors.empty()) {
        return true;
    }

    auto db = AgentDB::get();
    for (const auto &descriptor_tlv : qos_mgmt_descriptors) {
        if (!descriptor_tlv) {
            LOG(ERROR) << "Received null tlvQoSManagementDescriptor";
            return false;
        }

        auto radio = db->get_radio_by_mac(descriptor_tlv->bssid(), AgentDB::eMacType::BSSID);
        if (!radio) {
            LOG(ERROR) << "Failed to resolve BSSID " << descriptor_tlv->bssid()
                       << " for QoS management descriptor";
            return false;
        }

        auto ap_manager_fd = m_btl_ctx.get_ap_manager_fd(radio->front.iface_name);
        if (ap_manager_fd == beerocks::net::FileDescriptor::invalid_descriptor) {
            LOG(ERROR) << "Failed to get AP manager fd for radio " << radio->front.iface_name;
            return false;
        }

        auto descriptor_request = message_com::create_vs_message<
            beerocks_message::cACTION_APMANAGER_QOS_MANAGEMENT_DESCRIPTOR_REQUEST>(m_cmdu_tx);
        if (!descriptor_request) {
            LOG(ERROR) << "Failed building QoS management descriptor request";
            return false;
        }
        (void)descriptor_request;

        auto descriptor_out = m_cmdu_tx.addClass<wfa_map::tlvQoSManagementDescriptor>();
        if (!descriptor_out) {
            LOG(ERROR) << "Failed adding QoS management descriptor TLV";
            return false;
        }

        descriptor_out->qmid()       = descriptor_tlv->qmid();
        descriptor_out->bssid()      = descriptor_tlv->bssid();
        descriptor_out->client_mac() = descriptor_tlv->client_mac();
        if (!descriptor_out->set_descriptor_element(descriptor_tlv->descriptor_element(),
                                                    descriptor_tlv->descriptor_element_length())) {
            LOG(ERROR) << "Failed copying descriptor element for BSSID " << descriptor_tlv->bssid()
                       << ", client " << descriptor_tlv->client_mac();
            return false;
        }

        m_btl_ctx.send_cmdu(ap_manager_fd, m_cmdu_tx);
    }

    return true;
}

void ServicePrioritizationTask::gather_iface_details(
    std::list<bpl::ServicePrioritizationUtils::sInterfaceTagInfo> *iface_tag_info_list)
{
    if (!iface_tag_info_list) {
        LOG(ERROR) << "iface_tag_info_list is nullptr";
        return;
    }

    auto db                                                  = AgentDB::get();
    bpl::ServicePrioritizationUtils::sInterfaceTagInfo iface = {};

    // vlan ids for all ifaces
    std::unordered_set<uint16_t> common_vlan_ids;
    common_vlan_ids.insert(db->traffic_separation.primary_vlan_id);
    common_vlan_ids.insert(db->traffic_separation.secondary_vlans_ids.begin(),
                           db->traffic_separation.secondary_vlans_ids.end());

    // bridge interface is configured as Primary VLAN ID untagged Port with primary VLAN ID
    iface.iface_name = db->bridge.iface_name;
    iface.tag_info   = bpl::ServicePrioritizationUtils::ePortMode::TAGGED_PORT_PRIMARY_UNTAGGED;
    iface_tag_info_list->push_back(iface);

    // Update WAN and LAN Ports.
    LOG(DEBUG) << "Update WAN and LAN Ports | local_gateway=" << !db->device_conf.local_gw
               << " | WAN iface name: " << db->ethernet.wan.iface_name
               << " | LAN count: " << db->ethernet.lan.size();

    // Add WAN interface to list
    if (!db->device_conf.local_gw) {
        if (db->ethernet.wan.iface_name.empty()) {
            LOG(WARNING) << "WAN interface name is empty!";
        } else {
            LOG(DEBUG) << "WAN interface name: " << db->ethernet.wan.iface_name;
            iface.iface_name = db->ethernet.wan.iface_name;
            iface.tag_info =
                bpl::ServicePrioritizationUtils::ePortMode::TAGGED_PORT_PRIMARY_UNTAGGED;
            iface_tag_info_list->push_back(iface);
        }
    }

    // Add LAN interfaces to list
    for (const auto &lan_iface_info : db->ethernet.lan) {
        if (lan_iface_info.iface_name.empty()) {
            LOG(WARNING) << "LAN interface name is empty!";
        } else {
            iface            = {};
            iface.iface_name = lan_iface_info.iface_name;
            iface.tag_info =
                bpl::ServicePrioritizationUtils::ePortMode::TAGGED_PORT_PRIMARY_UNTAGGED;
            iface_tag_info_list->push_back(iface);
            LOG(DEBUG) << "Added LAN interface: " << lan_iface_info.iface_name;
        }
    }

    // Wireless Backhaul
    LOG(DEBUG) << "Wireless Backhaul | local_gateway=" << !db->device_conf.local_gw
               << " | Selected iface: " << db->backhaul.selected_iface_name
               << " | Connection type: " << db->backhaul.connection_type;
    if (!db->device_conf.local_gw && !db->backhaul.selected_iface_name.empty() &&
        db->backhaul.connection_type == AgentDB::sBackhaul::eConnectionType::Wireless) {
        iface      = {};
        auto radio = db->radio(db->backhaul.selected_iface_name);
        if (!radio) {
            LOG(ERROR) << "Could not find Backhaul Radio interface!";
            return;
        }
        iface.iface_name = radio->back.iface_name;

        // Effective Multi-AP Profile = minimum of local capability, and bBSS
        // (upstream AP) profile. This ensures the link never advertises or enables
        // features (TS, SP, R3/4) that are unsupported by any participant in the path.
        auto profile_min = std::min({int(db->backhaul.backhaul_bss_multi_ap_profile),
                                     int(db->device_conf.multi_ap_profile)});
        LOG(INFO) << "Service Prioritization decision: local ="
                  << int(db->device_conf.multi_ap_profile)
                  << " peer =" << db->backhaul.backhaul_bss_multi_ap_profile
                  << " -> effective=" << profile_min;
        iface.tag_info =
            profile_min > wfa_map::tlvProfile2MultiApProfile::eMultiApProfile::MULTIAP_PROFILE_1
                ? bpl::ServicePrioritizationUtils::ePortMode::TAGGED_PORT_PRIMARY_TAGGED
                : bpl::ServicePrioritizationUtils::ePortMode::UNTAGGED_PORT;
        iface_tag_info_list->push_back(iface);
    }

    for (auto radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }

        for (const auto &bss : radio->front.bssids) {
            // Skip unconfigured BSS.
            if (bss.ssid.empty()) {
                continue;
            }
            iface = {};

            LOG(DEBUG) << "BSS " << bss.mac << ", ssid:" << bss.ssid;

            std::string bss_iface;

            if (!network_utils::linux_iface_get_name(bss.mac, bss_iface)) {
                LOG(WARNING) << "Interface with MAC " << bss.mac << " does not exist";
                continue;
            }

            if (bss.fronthaul_bss && !bss.backhaul_bss) { // fBSS
                iface.iface_name = bss_iface;
                iface.tag_info   = bpl::ServicePrioritizationUtils::ePortMode::UNTAGGED_PORT;
                iface_tag_info_list->push_back(iface);
            } else if (!bss.fronthaul_bss && bss.backhaul_bss) { // bBSS
                auto bss_iface_netdevs =
                    network_utils::get_bss_ifaces(bss_iface, db->bridge.iface_name);

                for (const auto &bss_iface_netdev : bss_iface_netdevs) {
                    iface.iface_name = bss_iface_netdev;
                    iface.tag_info =
                        bss.backhaul_bss_disallow_profile1_agent_association
                            ? bpl::ServicePrioritizationUtils::ePortMode::TAGGED_PORT_PRIMARY_TAGGED
                            : bpl::ServicePrioritizationUtils::ePortMode::UNTAGGED_PORT;
                    iface_tag_info_list->push_back(iface);
                }
            } else { // Combined fBSS & bBSS - Currently Support only Profile-1 (PPM-1418)
                iface.iface_name = bss_iface;
                iface.tag_info   = bpl::ServicePrioritizationUtils::ePortMode::UNTAGGED_PORT;
                iface_tag_info_list->push_back(iface);

                auto bss_iface_netdevs =
                    network_utils::get_bss_ifaces(bss_iface, db->bridge.iface_name);

                for (const auto &bss_iface_netdev : bss_iface_netdevs) {
                    iface.iface_name = bss_iface_netdev;
                    iface.tag_info   = bpl::ServicePrioritizationUtils::ePortMode::UNTAGGED_PORT;
                    iface_tag_info_list->push_back(iface);
                }
            }
        }
    }

    // Setting common vlan_ids for all interfaces
    for (auto &interface : *iface_tag_info_list) {
        interface.vlan_ids.insert(common_vlan_ids.begin(), common_vlan_ids.end());
    }
}

bool ServicePrioritizationTask::qos_apply_active_rule()
{
    const auto &rules = AgentDB::get()->service_prioritization.rules;
    auto it           = rules.cbegin();
    auto active       = rules.cend();
    while (it != rules.cend()) {
        if (it->second.bits_field2.always_match) {
            if ((active == rules.cend()) || (active->second.precedence < it->second.precedence)) {
                active = it;
            }
        }
        ++it;
    }
    if (active != rules.cend()) {
        if (!service_prio_utils) {
            LOG(ERROR) << "Service Priority Utilities are not found";
            return false;
        }
        auto db                                      = AgentDB::get();
        beerocks_message::sServicePrioConfig request = {};
        request.mode                                 = active->second.output;
        std::copy(db->service_prioritization.dscp_mapping_table.begin(),
                  db->service_prioritization.dscp_mapping_table.end(), request.data);
        beerocks::ServicePrioritizationTask::send_service_prio_config(request);
        switch (active->second.output) {
        case QOS_USE_DSCP_MAP:
            return qos_setup_dscp_map();
        case QOS_USE_UP:
            return qos_setup_up_map();
        default:
            return qos_setup_single_value_map(active->second.output);
        }
    }

    return true;
}

bool ServicePrioritizationTask::qos_flush_setup()
{
    //TODO: PPM-2389, drive ebtables or external software
    // as per vendor specific in the Service Prioritization utility
    return service_prio_utils->flush_rules();
}

bool ServicePrioritizationTask::qos_setup_single_value_map(uint8_t pcp)
{
    if (pcp >= QOS_USE_DSCP_MAP) {
        LOG(ERROR) << "invalid output value for QoS single rule (" << static_cast<uint16_t>(pcp)
                   << ')';
        return false;
    }

    if (qos_flush_setup() == false) {
        return false;
    }

    std::list<bpl::ServicePrioritizationUtils::sInterfaceTagInfo> iface_list;
    ServicePrioritizationTask::gather_iface_details(&iface_list);

    //TODO: PPM-2389, drive ebtables or external software
    // as per vendor specific in the Service Prioritization utility
    return service_prio_utils->apply_single_value_map(&iface_list, pcp);
}

bool ServicePrioritizationTask::qos_setup_dscp_map()
{
    uint8_t pcp = 0;
    std::list<bpl::ServicePrioritizationUtils::sInterfaceTagInfo> iface_list;
    bpl::ServicePrioritizationUtils::sDscpMap dscp_map = {};
    auto db                                            = AgentDB::get();

    LOG(DEBUG) << "ServicePrioritizationTask::qos_setup_dscp_map - DSCP custom map used for PCP";

    pcp = db->traffic_separation.default_pcp;
    LOG(DEBUG) << "Default PCP = " << pcp;

    ServicePrioritizationTask::gather_iface_details(&iface_list);
    std::copy(db->service_prioritization.dscp_mapping_table.begin(),
              db->service_prioritization.dscp_mapping_table.end(), dscp_map.dscp);

    qos_flush_setup();

    //TODO: PPM-2389, drive ebtables or external software
    // as per vendor specific in the Service Prioritization utility
    return service_prio_utils->apply_dscp_map(&iface_list, &dscp_map, pcp);
}

bool ServicePrioritizationTask::qos_setup_up_map()
{
    LOG(DEBUG) << "ServicePrioritizationTask::qos_setup_up_map - UP used for PCP";

    qos_flush_setup();

    uint8_t pcp = AgentDB::get()->traffic_separation.default_pcp;
    std::list<bpl::ServicePrioritizationUtils::sInterfaceTagInfo> iface_list;
    ServicePrioritizationTask::gather_iface_details(&iface_list);

    //TODO: PPM-2389, drive ebtables or external software
    // as per vendor specific in the Service Prioritization utility
    return service_prio_utils->apply_up_map(&iface_list, pcp);
}

bool ServicePrioritizationTask::send_service_prio_config(
    const beerocks_message::sServicePrioConfig &request)
{
    // Sending the config to all AP managers
    m_btl_ctx.m_radio_managers.do_on_each_radio_manager(
        [&](slave_thread::sManagedRadio &radio_manager,
            const std::string &fronthaul_iface) -> bool {
            auto request_msg = message_com::create_vs_message<
                beerocks_message::cACTION_APMANAGER_HOSTAP_SERVICE_PRIO_CONFIG>(m_cmdu_tx);

            if (!request_msg) {
                LOG(ERROR)
                    << "Failed to build cACTION_APMANAGER_HOSTAP_SERVICE_PRIO_CONFIG message";
                return false;
            }

            request_msg->cs_params().mode = request.mode;
            std::copy(request.data, request.data + beerocks::message::DSCP_MAPPING_LIST_LENGTH,
                      request_msg->cs_params().data);
            LOG(DEBUG) << "Sending service priority config to radio, mode: " << request.mode;

            m_btl_ctx.send_cmdu(radio_manager.ap_manager_fd, m_cmdu_tx);
            return true;
        });
    return true;
}
bool ServicePrioritizationTask::handle_tid_to_link_mapping_policy_tlv(
    std::shared_ptr<wfa_map::tlvTidToLinkMappingPolicy> tlvTidToLinkMapping)
{
    if (!tlvTidToLinkMapping) {
        LOG(ERROR) << "Invalid Tid-To-Link-Mapping TLV";
        return false;
    }
    auto db = AgentDB::get();
    // get AP MLD MAC
    sMacAddr mld_mac = tlvTidToLinkMapping->mld_mac_addr();

    //Select correct DB map
    bool is_bsta = tlvTidToLinkMapping->is_bsta_config().is_bsta_mld;
    auto &target_map = is_bsta ? db->service_prioritization.bsta_mld_client
                               : db->service_prioritization.ap_mld_client;

    //Clear old data for this MLD
    target_map[mld_mac].clear();
    // Config object
    beerocks::AgentDB::TID_to_Link_Mapping_Config config = {};

    config.is_bSTA_Config = is_bsta;
    config.MLD_MAC_Addr   = mld_mac;
    config.TID_To_Link_Mapping_Negotiation =
        tlvTidToLinkMapping->tid_to_link_mapping_negotiation().is_enabled;
    config.Num_Mapping = tlvTidToLinkMapping->num_mapping();

    // ===== LOOP: mappings =====

    for (size_t i = 0; i < tlvTidToLinkMapping->num_mapping(); i++) {
        auto mapping_tuple = tlvTidToLinkMapping->mapping(i);
        if (!std::get<0>(mapping_tuple)){
            LOG(ERROR) << "Invalid Mapping Index" << i;
	    continue;
	}

        auto &mapping                                   = std::get<1>(mapping_tuple);
        beerocks::AgentDB::sTidToLinkMappingEntry entry = {};
        // Basic fields
        entry.addRemove        = mapping.add_remove().should_be_removed;
        entry.STA_MLD_MAC_Addr = mapping.sta_mld_mac_addr();
        // Control field
        auto controlField = mapping.tid_to_link_control_field();
        uint8_t control   = 0;
        tid_to_link_utils::set_direction(control, controlField->tid_to_link_control().direction);
        tid_to_link_utils::set_default_link_mapping(
	    control, controlField->tid_to_link_control().default_link_mapping);
        tid_to_link_utils::set_mapping_switch_time(
	    control, controlField->tid_to_link_control().mapping_switch_time_present);
        tid_to_link_utils::set_expected_duration_present(
	    control, controlField->tid_to_link_control().expected_duration_present);
        tid_to_link_utils::set_link_mapping_size(
	    control, controlField->tid_to_link_control().link_mapping_size);
        entry.tid_to_link_control_field = control;

        // Presence bitmap
        uint8_t presence                      = controlField->link_mapping_presence_indicator();
        entry.Link_Mapping_Presence_Indicator = presence;
        // Expected duration
        if (controlField->tid_to_link_control().expected_duration_present) {
            auto duration           = controlField->expected_duration();
            entry.Expected_Duration = (duration[0] << 16) | (duration[1] << 8) | duration[2];
        }
        // TID → Link Mapping Parsing
        uint8_t tid_mapping_count = mapping.tid_to_link_mapping_length();
	if (tid_mapping_count == 0) {
            LOG(WARNING) << "No TID Mappings Present";
	    continue;
	}
        uint8_t tid_index         = 0;
        for (uint8_t tid = 0; tid < 8; tid++) {
            // Check presence bitmap
            if (!(presence & (1 << tid)))
                continue;

            if (tid_index >= tid_mapping_count)
                break;
            auto result   = mapping.tid_to_link_mapping(tid_index);
            bool ok       = std::get<0>(result);
            auto &tid_map = std::get<1>(result);
            if (!ok) {
                LOG(ERROR) << "Invalid TID mapping index";
                continue;
            }
            //  Extract bytes
            uint8_t lower                  = get_tid_byte(tid_map.loByte());
            uint8_t upper                  = get_tid_byte(tid_map.hiByte());
            uint16_t value                 = lower | (upper << 8);
            entry.TID_to_Link_Mapping[tid] = value;
            LOG(DEBUG) << "Parsed TID " << int(tid) << " mapping: " << std::bitset<16>(value);
            tid_index++;
        }
        // Add entry
        config.mappings.push_back(entry);
    }
    // Store in DB (key = STA MLD MAC)
    for (auto &entry : config.mappings) {
        target_map[mld_mac][entry.STA_MLD_MAC_Addr] = config;
        LOG(DEBUG) << "Stored TID-to-Link Mapping Policy in DB";
    }

    return true;
}

} // namespace beerocks
