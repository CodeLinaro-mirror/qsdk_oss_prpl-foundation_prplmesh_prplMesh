/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

/**
 * This file uses code copied from `iw` (http://git.sipsolutions.net/iw.git/)
 *
 * Copyright (c) 2007, 2008 Johannes Berg
 * Copyright (c) 2007    Andy Lutomirski
 * Copyright (c) 2007    Mike Kershaw
 * Copyright (c) 2008-2009   Luis R. Rodriguez
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "capability_reporting_task.h"
#include "../son_slave_thread.h"
#include "../tlvf_utils.h"
#include "multi_vendor.h"

#include <tlvf/wfa_map/tlvAkmSuiteCapabilities.h>
#include <tlvf/wfa_map/tlvApCapability.h>
#include <tlvf/wfa_map/tlvApHeCapabilities.h>
#include <tlvf/wfa_map/tlvApHtCapabilities.h>
#include <tlvf/wfa_map/tlvApVhtCapabilities.h>
#include <tlvf/wfa_map/tlvApWifi6Capabilities.h>
#include <tlvf/wfa_map/tlvBackhaulStaRadioCapabilities.h>
#include <tlvf/wfa_map/tlvChannelScanCapabilities.h>
#include <tlvf/wfa_map/tlvClientCapabilityReport.h>
#include <tlvf/wfa_map/tlvClientInfo.h>
#include <tlvf/wfa_map/tlvDeviceInventory.h>
#include <tlvf/wfa_map/tlvEHTOperations.h>
#include <tlvf/wfa_map/tlvErrorCode.h>
#include <tlvf/wfa_map/tlvProfile2ApCapability.h>
#include <tlvf/wfa_map/tlvProfile2ApRadioAdvancedCapabilities.h>
#include <tlvf/wfa_map/tlvProfile2CacCapabilities.h>
#include <tlvf/wfa_map/tlvProfile2MetricCollectionInterval.h>
#include <tlvf/wfa_map/tlvWifi7AgentCapabilities.h>

using namespace multi_vendor;

namespace beerocks {

CapabilityReportingTask::CapabilityReportingTask(slave_thread &btl_ctx,
                                                 ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::CAPABILITY_REPORTING), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx)
{
}

bool CapabilityReportingTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                          const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                                          std::shared_ptr<beerocks_header> beerocks_header)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::CLIENT_CAPABILITY_QUERY_MESSAGE: {
        handle_client_capability_query(cmdu_rx, src_mac);
        break;
    }
    case ieee1905_1::eMessageType::AP_CAPABILITY_QUERY_MESSAGE: {
        handle_ap_capability_query(cmdu_rx, src_mac);
        break;
    }
    case ieee1905_1::eMessageType::BACKHAUL_STA_CAPABILITY_QUERY_MESSAGE: {
        handle_backhaul_sta_capability_query(cmdu_rx, src_mac);
        break;
    }
    default: {
        // Message was not handled, therefore return false.
        return false;
    }
    }
    return true;
}

void CapabilityReportingTask::handle_client_capability_query(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                             const sMacAddr &src_mac)
{
    const auto mid = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received CLIENT_CAPABILITY_QUERY_MESSAGE , mid=" << std::hex << mid;

    auto db = AgentDB::get();
    if (src_mac != db->controller_info.bridge_mac) {
        LOG(ERROR) << "[Multiple Controllers Detected] Ignoring CLIENT_CAPABILITY_QUERY_MESSAGE "
                      "from an unknown Controller: "
                   << src_mac;
        return;
    }

    auto client_info_tlv_r = cmdu_rx.getClass<wfa_map::tlvClientInfo>();
    if (!client_info_tlv_r) {
        LOG(ERROR) << "getClass wfa_map::tlvClientInfo failed";
        return;
    }

    // send CLIENT_CAPABILITY_REPORT_MESSAGE back to the controller
    if (!m_cmdu_tx.create(mid, ieee1905_1::eMessageType::CLIENT_CAPABILITY_REPORT_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type CLIENT_CAPABILITY_REPORT_MESSAGE, has failed";
        return;
    }

    auto client_info_tlv_t = m_cmdu_tx.addClass<wfa_map::tlvClientInfo>();
    if (!client_info_tlv_t) {
        LOG(ERROR) << "addClass wfa_map::tlvClientInfo has failed";
        return;
    }
    client_info_tlv_t->bssid()      = client_info_tlv_r->bssid();
    client_info_tlv_t->client_mac() = client_info_tlv_r->client_mac();

    auto client_capability_report_tlv = m_cmdu_tx.addClass<wfa_map::tlvClientCapabilityReport>();
    if (!client_capability_report_tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvClientCapabilityReport has failed";
        return;
    }

    // Check if it is an error scenario - if the STA specified in the Client Capability Query
    // message is not associated with any of the BSS operated by the Multi-AP Agent [ though the
    // TLV does contain a BSSID, the specification says that we should answer if the client is
    // associated with any BSS on this agent.]
    auto radio = db->get_radio_by_mac(client_info_tlv_r->client_mac(), AgentDB::eMacType::CLIENT);
    if (!radio) {
        LOG(ERROR) << "radio for client mac " << client_info_tlv_r->client_mac() << " not found";

        // If it is an error scenario, set Success status to 0x01 = Failure and do nothing after it.
        client_capability_report_tlv->result_code() = wfa_map::tlvClientCapabilityReport::FAILURE;

        LOG(DEBUG) << "Result Code: FAILURE";
        LOG(DEBUG) << "STA specified in the Client Capability Query message is not associated with "
                      "any of the BSS operated by the Multi-AP Agent ";
        // Add an Error Code TLV
        auto error_code_tlv = m_cmdu_tx.addClass<wfa_map::tlvErrorCode>();
        if (!error_code_tlv) {
            LOG(ERROR) << "addClass wfa_map::tlvErrorCode has failed";
            return;
        }
        error_code_tlv->reason_code() =
            wfa_map::tlvErrorCode::STA_NOT_ASSOCIATED_WITH_ANY_BSS_OPERATED_BY_THE_AGENT;
        error_code_tlv->sta_mac() = client_info_tlv_r->client_mac();
        m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
        return;
    }

    client_capability_report_tlv->result_code() = wfa_map::tlvClientCapabilityReport::SUCCESS;
    LOG(DEBUG) << "Result Code: SUCCESS";

    // Add frame body of the most recently received (Re)Association Request frame from this client.
    auto &client_info = radio->associated_clients.at(client_info_tlv_r->client_mac());
    client_capability_report_tlv->set_association_frame(client_info.association_frame.data(),
                                                        client_info.association_frame_length);

    LOG(DEBUG) << "Send a CLIENT_CAPABILITY_REPORT_MESSAGE back to controller";
    m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

void CapabilityReportingTask::handle_event(uint8_t event_enum_value, const void *event_obj)
{
    switch (eEvent(event_enum_value)) {
    case EARLY_AP_CAPABILITY: {
        LOG(DEBUG) << "EARLY_AP_CAPABILITY event is received";
        create_early_ap_capability_report_message();
        break;
    }
    default: {
        LOG(DEBUG) << "Message handler doesn't exists for event type " << event_enum_value;
        break;
    }
    }
}

void CapabilityReportingTask::create_early_ap_capability_report_message()
{
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::EARLY_AP_CAPABILITY_REPORT_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type EARLY_AP_CAPABILITY_REPORT_MESSAGE, has failed";
        return;
    }

    if (!prepare_ap_capability_message(true)) {
        LOG(ERROR) << "EARLY_AP_CAPABILITY_REPORT_MESSAGE filling has failed";
        return;
    }

    // send the constructed report
    LOG(DEBUG) << "Sending EARLY_AP_CAPABILITY_REPORT_MESSAGE";
    m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

bool CapabilityReportingTask::add_akm_suites_capabilities_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    auto akm_suites_capabilities_tlv = cmdu_tx.addClass<wfa_map::tlvAkmSuiteCapabilities>();
    if (!akm_suites_capabilities_tlv) {
        LOG(ERROR) << "Error creating tlvAkmSuiteCapabilities";
        return false;
    }

    // TODO: Fill correct values for TLV (PPM-3618)

    LOG(INFO) << "Add AKM Suites capabilities TLV";
    return true;
}

bool CapabilityReportingTask::add_wifi7_agent_capabilities_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    auto db = AgentDB::get();

    int eht_capable_radio_nb = 0;
    for (auto radio : db->get_radios_list()) {
        if (radio->eht_supported) {
            ++eht_capable_radio_nb;
        }
    }
    if (eht_capable_radio_nb == 0) {
        LOG(DEBUG) << "No radio with EHT support";
        return true;
    }

    auto wifi7_agent_capabilities_tlv = cmdu_tx.addClass<wfa_map::tlvWifi7AgentCapabilities>();
    if (!wifi7_agent_capabilities_tlv) {
        LOG(ERROR) << "Error creating TLV_WIFI7_AGENT_CAPABILITIES";
        return false;
    }

    // Corresponds to the maximum number of MLDs we can have
    // Hard-coded for now, may be specified somehwere in the DM
    wifi7_agent_capabilities_tlv->max_num_mlds() = db->device_conf.max_num_mlds;

    wifi7_agent_capabilities_tlv->flags1().ap_maximum_links   = db->device_conf.ap_maximum_links;
    wifi7_agent_capabilities_tlv->flags1().bsta_maximum_links = db->device_conf.bsta_maximum_links;
    wifi7_agent_capabilities_tlv->flags2().tid_to_link_mapping_capability = 0;

    LOG(DEBUG) << "Max AP MLD Links " << wifi7_agent_capabilities_tlv->flags1().ap_maximum_links
               << ", Max bSTA MLD Links ="
               << wifi7_agent_capabilities_tlv->flags1().bsta_maximum_links
               << ", Max num of MLDs =" << wifi7_agent_capabilities_tlv->max_num_mlds();

    for (auto radio : db->get_radios_list()) {
        if (!radio) {
            LOG(ERROR) << "radio does not exist in the db";
            continue;
        }

        if (!radio->eht_supported) {
            continue;
        }

        auto radio_wifi7_capabilities(
            wifi7_agent_capabilities_tlv->create_radio_wifi7_capabilities());

        radio_wifi7_capabilities->ruid() = radio->front.iface_mac;

        auto &ap_radio_mode_support         = radio_wifi7_capabilities->ap_modes_support();
        ap_radio_mode_support.str_support   = radio->ap_modes_support.str_support;
        ap_radio_mode_support.nstr_support  = radio->ap_modes_support.nstr_support;
        ap_radio_mode_support.emlsr_support = radio->ap_modes_support.emlsr_support;
        ap_radio_mode_support.emlmr_support = radio->ap_modes_support.emlmr_support;

        auto &bsta_radio_mode_support         = radio_wifi7_capabilities->bsta_modes_support();
        bsta_radio_mode_support.str_support   = radio->bsta_modes_support.str_support;
        bsta_radio_mode_support.nstr_support  = radio->bsta_modes_support.nstr_support;
        bsta_radio_mode_support.emlsr_support = radio->bsta_modes_support.emlsr_support;
        bsta_radio_mode_support.emlmr_support = radio->bsta_modes_support.emlmr_support;

        auto radio_ap_capabilities = radio_wifi7_capabilities->create_ap_wifi7_capabilities();

        // Fill AP WiFi7 capabilities attribute
        if (ap_radio_mode_support.str_support) {
            for (auto radio2 : db->get_radios_list()) {
                if (!radio2 || radio->front.iface_mac == radio2->front.iface_mac) {
                    continue;
                }

                if (radio2->ap_modes_support.str_support) {
                    auto ap_str_config(radio_ap_capabilities->create_str_config());

                    ap_str_config->frequency_separation().freq_separation = 0;
                    if (ap_str_config->frequency_separation().freq_separation) {
                        ap_str_config->ruid() = radio2->front.iface_mac;
                    }

                    if (!radio_ap_capabilities->add_str_config(ap_str_config)) {
                        LOG(ERROR) << "add_str_config() failed in tlvWifi7AgentCapabilities";
                        return false;
                    }
                }
            }
        }

        if (ap_radio_mode_support.nstr_support) {
            for (auto radio2 : db->get_radios_list()) {
                if (!radio2 || radio->front.iface_mac == radio2->front.iface_mac) {
                    continue;
                }

                if (radio2->ap_modes_support.nstr_support) {
                    auto ap_nstr_config(radio_ap_capabilities->create_nstr_config());

                    ap_nstr_config->frequency_separation().freq_separation = 0;
                    if (ap_nstr_config->frequency_separation().freq_separation) {
                        ap_nstr_config->ruid() = radio2->front.iface_mac;
                    }

                    if (!radio_ap_capabilities->add_nstr_config(ap_nstr_config)) {
                        LOG(ERROR) << "add_nstr_config() failed in tlvWifi7AgentCapabilities";
                        return false;
                    }
                }
            }
        }

        if (ap_radio_mode_support.emlsr_support) {
            for (auto radio2 : db->get_radios_list()) {
                if (!radio2 || radio->front.iface_mac == radio2->front.iface_mac) {
                    continue;
                }

                if (radio2->ap_modes_support.emlsr_support) {
                    auto ap_emlsr_config(radio_ap_capabilities->create_emlsr_config());

                    ap_emlsr_config->frequency_separation().freq_separation = 0;
                    if (ap_emlsr_config->frequency_separation().freq_separation) {
                        ap_emlsr_config->ruid() = radio2->front.iface_mac;
                    }

                    if (!radio_ap_capabilities->add_emlsr_config(ap_emlsr_config)) {
                        LOG(ERROR) << "add_emlsr_config() failed in tlvWifi7AgentCapabilities";
                        return false;
                    }
                }
            }
        }

        if (ap_radio_mode_support.emlmr_support) {
            for (auto radio2 : db->get_radios_list()) {
                if (!radio2 || radio->front.iface_mac == radio2->front.iface_mac) {
                    continue;
                }

                if (radio2->ap_modes_support.emlmr_support) {
                    auto ap_emlmr_config(radio_ap_capabilities->create_emlmr_config());

                    ap_emlmr_config->frequency_separation().freq_separation = 0;
                    if (ap_emlmr_config->frequency_separation().freq_separation) {
                        ap_emlmr_config->ruid() = radio2->front.iface_mac;
                    }

                    if (!radio_ap_capabilities->add_emlmr_config(ap_emlmr_config)) {
                        LOG(ERROR) << "add_emlmr_config() failed in tlvWifi7AgentCapabilities";
                        return false;
                    }
                }
            }
        }

        if (!radio_wifi7_capabilities->add_ap_wifi7_capabilities(radio_ap_capabilities)) {
            LOG(ERROR) << "add_ap_wifi7_capabilities() failed in tlvWifi7AgentCapabilities";
            return false;
        }

        // Fill bsta WiFi7 capabilities attribute
        auto radio_bsta_capabilities = radio_wifi7_capabilities->create_bsta_wifi7_capabilities();

        if (bsta_radio_mode_support.str_support) {
            for (auto radio2 : db->get_radios_list()) {
                if (!radio2 || radio->front.iface_mac == radio2->front.iface_mac) {
                    continue;
                }

                if (radio2->bsta_modes_support.str_support) {
                    auto bsta_str_config(radio_bsta_capabilities->create_str_config());

                    bsta_str_config->frequency_separation().freq_separation = 0;
                    if (bsta_str_config->frequency_separation().freq_separation) {
                        bsta_str_config->ruid() = radio2->front.iface_mac;
                    }

                    if (!radio_bsta_capabilities->add_str_config(bsta_str_config)) {
                        LOG(ERROR) << "add_str_config() failed in tlvWifi7AgentCapabilities";
                        return false;
                    }
                }
            }
        }

        if (bsta_radio_mode_support.nstr_support) {
            for (auto radio2 : db->get_radios_list()) {
                if (!radio2 || radio->front.iface_mac == radio2->front.iface_mac) {
                    continue;
                }

                if (radio2->bsta_modes_support.nstr_support) {
                    auto bsta_nstr_config(radio_bsta_capabilities->create_nstr_config());

                    bsta_nstr_config->frequency_separation().freq_separation = 0;
                    if (bsta_nstr_config->frequency_separation().freq_separation) {
                        bsta_nstr_config->ruid() = radio2->front.iface_mac;
                    }

                    if (!radio_bsta_capabilities->add_nstr_config(bsta_nstr_config)) {
                        LOG(ERROR) << "add_nstr_config() failed in tlvWifi7AgentCapabilities";
                        return false;
                    }
                }
            }
        }

        if (bsta_radio_mode_support.emlsr_support) {
            for (auto radio2 : db->get_radios_list()) {
                if (!radio2 || radio->front.iface_mac == radio2->front.iface_mac) {
                    continue;
                }

                if (radio2->bsta_modes_support.emlsr_support) {
                    auto bsta_emlsr_config(radio_bsta_capabilities->create_emlsr_config());

                    bsta_emlsr_config->frequency_separation().freq_separation = 0;
                    if (bsta_emlsr_config->frequency_separation().freq_separation) {
                        bsta_emlsr_config->ruid() = radio2->front.iface_mac;
                    }

                    if (!radio_bsta_capabilities->add_emlsr_config(bsta_emlsr_config)) {
                        LOG(ERROR) << "add_emlsr_config() failed in tlvWifi7AgentCapabilities";
                        return false;
                    }
                }
            }
        }

        if (bsta_radio_mode_support.emlmr_support) {
            for (auto radio2 : db->get_radios_list()) {
                if (!radio2 || radio->front.iface_mac == radio2->front.iface_mac) {
                    continue;
                }

                if (radio2->bsta_modes_support.emlmr_support) {
                    auto bsta_emlmr_config(radio_bsta_capabilities->create_emlmr_config());

                    bsta_emlmr_config->frequency_separation().freq_separation = 0;
                    if (bsta_emlmr_config->frequency_separation().freq_separation) {
                        bsta_emlmr_config->ruid() = radio2->front.iface_mac;
                    }

                    if (!radio_bsta_capabilities->add_emlmr_config(bsta_emlmr_config)) {
                        LOG(ERROR) << "add_emlmr_config() failed in tlvWifi7AgentCapabilities";
                        return false;
                    }
                }
            }
        }

        if (!radio_wifi7_capabilities->add_bsta_wifi7_capabilities(radio_bsta_capabilities)) {
            LOG(ERROR) << "add_bsta_wifi7_capabilities() failed in tlvWifi7AgentCapabilities";
            return false;
        }

        if (!wifi7_agent_capabilities_tlv->add_radio_wifi7_capabilities(radio_wifi7_capabilities)) {
            LOG(ERROR) << "add_radio_wifi7_capabilities() failed in tlvWifi7AgentCapabilities";
            return false;
        }
    }

    LOG(INFO) << "Add WiFi 7 capabilities TLV";

    return true;
}

void CapabilityReportingTask::handle_ap_capability_query(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                         const sMacAddr &src_mac)
{
    const auto mid = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received AP_CAPABILITY_QUERY_MESSAGE, mid=" << std::hex << mid;

    auto db = AgentDB::get();
    if (src_mac != db->controller_info.bridge_mac) {
        LOG(ERROR) << "[Multiple Controllers Detected] Ignoring AP_CAPABILITY_QUERY_MESSAGE from "
                      "an unknown Controller: "
                   << src_mac;
        return;
    }

    if (!m_cmdu_tx.create(mid, ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type AP_CAPABILITY_REPORT_MESSAGE, has failed";
        return;
    }

    if (!prepare_ap_capability_message(false)) {
        LOG(ERROR) << "AP_CAPABILITY_REPORT_MESSAGE filling has failed";
        return;
    }

    // send the constructed report
    LOG(DEBUG) << "Sending AP_CAPABILITY_REPORT_MESSAGE , mid: " << std::hex << mid;
    m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

bool CapabilityReportingTask::prepare_ap_capability_message(bool early)
{
    /**
     * The add_vs_tlv method invokes the handler to add Vendor specific TLVs to the
     * AP Capability Report message.
     */
    if (!multi_vendor::tlvf_handler::add_vs_tlv(
            m_cmdu_tx, ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE)) {
        LOG(ERROR) << "Failed adding few TLVs in AP_CAPABILITY_REPORT_MESSAGE";
    }

    auto ap_capability_tlv = m_cmdu_tx.addClass<wfa_map::tlvApCapability>();
    if (!ap_capability_tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvApCapability has failed";
        return false;
    }
    auto db = AgentDB::get();

    /**
     * TODO : These looks like a vendor specific params, where to read them from ?
     * For now, lets  enable all of them to be able to develop/test the unassociated stations stats feature
     */
    ap_capability_tlv->value().support_agent_initiated_rcpi_based_steering                  = true;
    ap_capability_tlv->value().support_unassociated_sta_link_metrics_on_non_operating_bssid = true;
    ap_capability_tlv->value().support_unassociated_sta_link_metrics_on_operating_bssid     = true;
    ap_capability_tlv->value().support_agent_backhaul_sta_reconfiguration                   = true;

    /**
     * 1. The tlvs created in the loop are created per radio and are
     * defined in the specification as "Zero Or More" (multi-ap specification v2, 17.1.7)
     */
    for (auto radio : db->get_radios_list()) {
        if (!radio) {
            LOG(ERROR) << "radio does not exist in the db";
            continue;
        }

        if (!tlvf_utils::add_ap_radio_basic_capabilities(m_cmdu_tx, radio->front.iface_mac)) {
            return false;
        }

        if (!add_ap_ht_capabilities(radio->front.iface_name)) {
            return false;
        }

        if (!add_ap_vht_capabilities(radio->front.iface_name)) {
            return false;
        }

        if (!add_ap_he_capabilities(radio->front.iface_name)) {
            return false;
        }

        if (!add_ap_wifi6_capabilities(radio->front.iface_name)) {
            return false;
        }

        if (!add_ap_radio_advanced_capabilities_tlv(radio->front.iface_name)) {
            return false;
        }

        ap_capability_tlv->value().RSN_Overriding |= radio->front.rsn_override_support;
    }

    /**
     * 2. The tlvs created here are defined in the
     * specification as "One" (multi-ap specification v2, 17.1.7).
     * the one tlv may contain information about few radios
     */
    if (!add_akm_suites_capabilities_tlv(m_cmdu_tx)) {
        LOG(ERROR) << "Error filling tlvAkmSuiteCapabilities";
        return false;
    }
    if (!add_wifi7_agent_capabilities_tlv(m_cmdu_tx)) {
        LOG(ERROR) << "Error filling TLV_WIFI7_AGENT_CAPABILITIES";
        return false;
    }
    if (!slave_thread::add_eht_operations_tlv(m_cmdu_tx)) {
        LOG(ERROR) << "Error filling TLV_EHT_OPERATIONS";
        return false;
    }

    tlvf_utils::add_tlv_channel_scan_capabilities(m_cmdu_tx);

    /* 2.2 radio independent tlvs */

    if (!add_profile2_ap_capability_tlv(m_cmdu_tx)) {
        return false;
    }

    if (!add_metric_collection_interval_tlv()) {
        return false;
    }

    /* 3. tlvs added by external sources */

    if (!add_cac_capabilities_tlv()) {
        LOG(ERROR) << "error filling cac capabilities tlv";
        if (!early) {
            return false;
        }
    }

    if (!add_device_inventory_tlv()) {
        LOG(ERROR) << "error filling device inventory tlv";
        return false;
    }

    return true;
}

void CapabilityReportingTask::handle_backhaul_sta_capability_query(
    ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &src_mac)
{
    const auto mid = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received BACKHAUL_STA_CAPABILITY_QUERY_MESSAGE, mid=" << std::hex << mid;

    auto db = AgentDB::get();
    if (src_mac != db->controller_info.bridge_mac) {
        LOG(ERROR) << "[Multiple Controllers Detected] Ignoring "
                      "BACKHAUL_STA_CAPABILITY_QUERY_MESSAGE from an unknown Controller: "
                   << src_mac;
        return;
    }

    if (!m_cmdu_tx.create(mid, ieee1905_1::eMessageType::BACKHAUL_STA_CAPABILITY_REPORT_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type BACKHAUL_STA_CAPABILITY_REPORT_MESSAGE, has failed";
        return;
    }

    for (auto radio : db->get_radios_list()) {

        if (!radio) {
            LOG(ERROR) << "radio does not exist in the db";
            continue;
        }

        if (radio->front.iface_mac == net::network_utils::ZERO_MAC) {
            continue;
        }

        auto backhaul_sta_radio_cap_tlv =
            m_cmdu_tx.addClass<wfa_map::tlvBackhaulStaRadioCapabilities>();
        if (!backhaul_sta_radio_cap_tlv) {
            LOG(ERROR) << "addClass wfa_map::tlvBackhaulStaRadioCapabilities has failed";
            return;
        }

        backhaul_sta_radio_cap_tlv->ruid() = radio->front.iface_mac;

        sMacAddr backhaul_mac;
        if (radio->back.iface_mac == net::network_utils::ZERO_MAC) {
            // This case occurs in case of wired backhaul
            LOG(INFO) << "Radio STA Interface HAL is not initialized, iface="
                      << radio->back.iface_name;
            backhaul_sta_radio_cap_tlv->sta_mac_included() =
                wfa_map::tlvBackhaulStaRadioCapabilities::eStaMacIncluded::FIELD_NOT_PRESENT;
            backhaul_mac = net::network_utils::ZERO_MAC;
        } else {
            backhaul_sta_radio_cap_tlv->sta_mac_included() =
                wfa_map::tlvBackhaulStaRadioCapabilities::eStaMacIncluded::FIELD_PRESENT;
            backhaul_mac = radio->back.iface_mac;

            if (!backhaul_sta_radio_cap_tlv->set_sta_mac(backhaul_mac)) {
                LOG(ERROR) << "Setting sta_mac for tlvBackhaulStaRadioCapabilities has failed";
                return;
            }
        }

        LOG(DEBUG) << "Backhaul STA Radio Capabilities, ruid=" << radio->front.iface_mac
                   << " sta mac=" << backhaul_mac;
    }

    m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

bool CapabilityReportingTask::add_ap_ht_capabilities(const std::string &iface_name)
{
    auto db    = AgentDB::get();
    auto radio = db->radio(iface_name);
    if (!radio) {
        return false;
    }

    if (!radio->ht_supported) {
        return true;
    }

    auto tlv = m_cmdu_tx.addClass<wfa_map::tlvApHtCapabilities>();
    if (!tlv) {
        LOG(ERROR) << "Error creating TLV_AP_HT_CAPABILITIES";
        return false;
    }

    tlv->radio_uid() = radio->front.iface_mac;
    struct beerocks::net::sHTCapabilities *HTCaps =
        (struct beerocks::net::sHTCapabilities *)(&radio->ht_capability);
    /**
     * See iw/util.c for details on how to compute fields.
     * Code has been preserved as close as possible to that in the iw command line tool.
     * AP HT Capabilities TLV Format : (Multi-AP Specification 1.0)
     * tlvValue :
     * Bits 7-6 : Maximum number of supported Tx spatial streams
     * Bits 5-4 : Maximum number of supported Rx spatial streams
     * Bit  3   : Short GI Support for 20 MHz.
     * Bit  2   : Short GI Support for 40 MHz.
     * Bit  1   : HT support for 40MHz
     * Bit  0   : Reserved
    */
    tlv->flags().max_num_of_supported_tx_spatial_streams =
        HTCaps->max_num_of_supported_tx_spatial_streams;
    tlv->flags().max_num_of_supported_rx_spatial_streams =
        HTCaps->max_num_of_supported_rx_spatial_streams;
    tlv->flags().short_gi_support_20mhz = HTCaps->short_gi_support_20mhz;
    tlv->flags().short_gi_support_40mhz = HTCaps->short_gi_support_40mhz;
    tlv->flags().ht_support_40mhz       = HTCaps->ht_support_40mhz;
    return true;
}

/* 9.4.2.157.3 Supported VHT-MCS and NSS Set field */
#define RX_VHT_MCS_MAP_OFFSET 0
#define TX_VHT_MCS_MAP_OFFSET 4

bool CapabilityReportingTask::add_ap_vht_capabilities(const std::string &iface_name)
{
    auto db    = AgentDB::get();
    auto radio = db->radio(iface_name);
    if (!radio) {
        return false;
    }

    if (!radio->vht_supported) {
        return true;
    }

    auto tlv = m_cmdu_tx.addClass<wfa_map::tlvApVhtCapabilities>();
    if (!tlv) {
        LOG(ERROR) << "Error creating TLV_AP_VHT_CAPABILITIES";
        return false;
    }

    tlv->radio_uid() = radio->front.iface_mac;
    struct beerocks::net::sVHTCapabilities *VHTCaps =
        (struct beerocks::net::sVHTCapabilities *)(&radio->vht_capability);
    /**
     * See iw/util.c for details on how to compute fields
     * Code has been preserved as close as possible to that in the iw command line tool.
     * AP VHT Capabilities TLV Format 16 bits: (Multi-AP Specification 3.0)
     * tlvValue (Flag1) :
     * Bits 15-13 : Maximum number of supported Tx spatial streams
     * Bits 12-10 : Maximum number of supported Rx spatial streams
     * Bit  9     : Short GI Support for 80 MHz.
     * Bit  8     : Short GI Support for 160Mhz and 80+80 MHz.
     * tlvValue (Flag2) :
     * Bit  7     : VHT support for 80+80 MHz.
     * Bit  6     : VHT support for 160 MHz.
     * Bit  5     : SU beamformer capable.
     * Bit  4     : MU beamformer capable.
     * Bit  3-0   : Reserved
     */
    uint16_t vht_rx_mcs = 0xffff;
    uint16_t vht_tx_mcs = 0xffff;
    memcpy(&vht_rx_mcs, &radio->vht_mcs_set[RX_VHT_MCS_MAP_OFFSET], sizeof(vht_rx_mcs));
    memcpy(&vht_tx_mcs, &radio->vht_mcs_set[TX_VHT_MCS_MAP_OFFSET], sizeof(vht_tx_mcs));

    tlv->supported_vht_rx_mcs() = vht_rx_mcs;
    tlv->supported_vht_tx_mcs() = vht_tx_mcs;
    tlv->flags1().max_num_of_supported_tx_spatial_streams =
        VHTCaps->max_num_of_supported_tx_spatial_streams;
    tlv->flags1().max_num_of_supported_rx_spatial_streams =
        VHTCaps->max_num_of_supported_rx_spatial_streams;
    tlv->flags1().short_gi_support_80mhz = VHTCaps->short_gi_support_80mhz;
    tlv->flags1().short_gi_support_160mhz_and_80_80mhz =
        VHTCaps->short_gi_support_160mhz_and_80_80mhz;
    tlv->flags2().vht_support_80_80mhz  = VHTCaps->vht_support_80_80mhz;
    tlv->flags2().vht_support_160mhz    = VHTCaps->vht_support_160mhz;
    tlv->flags2().su_beamformer_capable = VHTCaps->su_beamformer_capable;
    tlv->flags2().mu_beamformer_capable = VHTCaps->mu_beamformer_capable;

    return true;
}

bool CapabilityReportingTask::add_ap_he_capabilities(const std::string &iface_name)
{
    auto db    = AgentDB::get();
    auto radio = db->radio(iface_name);
    if (!radio) {
        return false;
    }

    if (!radio->he_supported) {
        return true;
    }

    auto tlv = m_cmdu_tx.addClass<wfa_map::tlvApHeCapabilities>();
    if (!tlv) {
        LOG(ERROR) << "Error creating TLV_AP_HE_CAPABILITIES";
        return false;
    }

    tlv->radio_uid() = radio->front.iface_mac;
    struct beerocks::net::sHECapabilities *HECaps =
        (struct beerocks::net::sHECapabilities *)(&radio->he_capability);
    /**
     * HE_MCS_SET[]: 13 Bytes with the first byte contains K-Bytes number
     * AP HE Capabilities TLV Format 16 bits: (Multi-AP Specification 1.0)
     * tlvValue (Flag1) :
     * Bits 15-13 : Maximum number of supported Tx spatial streams.
     * Bits 12-10 : Maximum number of supported Rx spatial streams.
     * Bit  9     : HE support for 80+80 MHz.
     * Bit  8     : HE support for 160 MHz..
     * tlvValue (Flag2) :
     * Bit  7     : SU beamformer capable.
     * Bit  6     : MU beamformer capable.
     * Bit  5     : UL MU-MIMO capable.
     * Bit  4     : UL MU-MIMO + OFDMA capable.
     * Bit  3     : DL MU-MIMO + OFDMA capable.
     * Bit  2     : UL OFDMA capable.
     * Bit  1     : DL OFDMA capable.
    */

    uint8_t mcs_nss_size = 4;
    if (HECaps->he_support_160mhz) {
        mcs_nss_size += 4;
    }
    if (HECaps->he_support_80_80mhz) {
        mcs_nss_size += 4;
    }

    /*
     * 9.4.2.248.4 Supported HE-MCS And NSS Set Field
     * [0] <=80MHz, [1] 160MHz, [2] 80+80MHz
     * Each entry: 2B RX map + 2B TX map
     */
    uint32_t he_mcs[3] = {0};
    memcpy(he_mcs, radio->he_mcs_set.begin(), sizeof(he_mcs));

    he_mcs[0] = htonl(he_mcs[0]);
    he_mcs[1] = htonl(he_mcs[1]);
    he_mcs[2] = htonl(he_mcs[2]);

    tlv->set_supported_he_mcs(he_mcs, mcs_nss_size);
    tlv->flags1().max_num_of_supported_tx_spatial_streams =
        HECaps->max_num_of_supported_tx_spatial_streams;
    tlv->flags1().max_num_of_supported_rx_spatial_streams =
        HECaps->max_num_of_supported_rx_spatial_streams;
    tlv->flags1().he_support_80_80mhz         = HECaps->he_support_80_80mhz;
    tlv->flags1().he_support_160mhz           = HECaps->he_support_160mhz;
    tlv->flags2().dl_ofdm_capable             = HECaps->dl_ofdm_capable;
    tlv->flags2().ul_ofdm_capable             = HECaps->ul_ofdm_capable;
    tlv->flags2().dl_mu_mimo_and_ofdm_capable = HECaps->dl_mu_mimo_and_ofdm_capable;
    tlv->flags2().ul_mu_mimo_and_ofdm_capable = HECaps->ul_mu_mimo_and_ofdm_capable;
    tlv->flags2().ul_mu_mimo_capable          = HECaps->ul_mu_mimo_capable;
    tlv->flags2().mu_beamformer_capable       = HECaps->mu_beamformer_capable;
    tlv->flags2().su_beamformer_capable       = HECaps->su_beamformer_capable;

    return true;
}

bool CapabilityReportingTask::add_ap_wifi6_capabilities(const std::string &iface_name)
{
    auto db    = AgentDB::get();
    auto radio = db->radio(iface_name);
    if (!radio) {
        return false;
    }

    // Include one AP Wi-Fi 6 Capabilities TLV in the AP Capability Report message
    // for each radio that supports 802.11
    // High Efficiency capability (Multi-AP Specification 3.0)
    // return true if Radio does not support Wi-Fi 6 Capabilities
    if (!radio->he_supported) {
        return true;
    }

    auto tlv = m_cmdu_tx.addClass<wfa_map::tlvApWifi6Capabilities>();
    if (!tlv) {
        LOG(ERROR) << "Error creating TLV_AP_WIFI6_CAPABILITIES";
        return false;
    }

    tlv->radio_uid() = radio->front.iface_mac;
    int number_of_role;
    //Check Management mode, if Repeater then number of role for agent will be 2.
    if (db->device_conf.management_mode == MGMT_MODE_MULTIAP_AGENT) {
        number_of_role = 2;
    } else {
        number_of_role = 1;
    }

    for (int agent_role = 0; agent_role < number_of_role; agent_role++) {
        auto role = tlv->create_role();
        if (!role) {
            LOG(ERROR) << "Failed creating role";
            return false;
        }

        auto wifi6_caps =
            reinterpret_cast<beerocks::net::sWIFI6Capabilities *>(&radio->wifi6_capability);

        /**
	 * AP WIFI6 Capabilities TLV Format 32 bits: (Multi-AP Specification 3.0)
	 * tlvValue (Flag1) :
	 * Bits 31-30 : Multi-AP Agent's Role
	 * Bits 29    : Support for HE 160 MHz
	 * Bits 28    : Support for HE 80+80 MHz
	 * Bits 27-24    : Length of MCS NSS
	 * tlvValue (Flag2):
	 * Bits 23    : Support for SU Beamformer
	 * Bits 22    : Support for SU Beamformee
	 * Bits 21    : Support for MU Beamformer Status
	 * Bits 20    : Support for Beamformee STS ≤ 80 MHz
	 * Bits 19    : Support for Beamformee STS > 80 MHz
	 * Bits 18    : Support for UL MU-MIMO
	 * Bits 17    : Support for UL OFDMA
	 * Bits 16    : Support for DL OFDMA
	 * tlvValue (Flag3)
	 * Bits 15-12 : Max number of users supported per DL MU-MIMO TX in an AP role
	 * Bits 11-8  : Max number of users supported per UL MU-MIMO RX in an AP role
	 * tlvValue (Flag4)
	 * Bits 7     : Support for RTS
	 * Bits 6     : Support for MU RTS
	 * Bits 5     : Support for Multi-BSSID
	 * Bits 4     : Support for MU EDCA
	 * Bits 3     : Support for TWT Requester
	 * Bits 2     : Support for TWT Responder
	 * Bits 1     : Support for Spatial Reuse
         * Bits 0     : Support for Anticipated Channel Usage
	*/

        //1 represents the Agent role is non-AP STA, default it supports AP role.
        if (agent_role) {
            role->flags1().agent_role = 1;
        } else {
            role->flags1().agent_role = 0;
        }
        role->flags1().he_support_160mhz   = wifi6_caps->he_support_160mhz;
        role->flags1().he_support_80_80mhz = wifi6_caps->he_support_80_80mhz;
        role->flags1().mcs_nss_length      = wifi6_caps->mcs_nss_length;

        /*
         * 9.4.2.248.4 Supported HE-MCS And NSS Set Field
         * [0] <=80MHz, [1] 160MHz, [2] 80+80MHz
         * Each entry: 2B RX map + 2B TX map
         */
        uint32_t he_mcs[3] = {0};
        memcpy(he_mcs, radio->he_mcs_set.begin(), sizeof(he_mcs));

        uint32_t mcs_nss_80 = htonl(he_mcs[0]);
        role->set_mcs_nss_80(&mcs_nss_80, sizeof(mcs_nss_80));
        if (role->flags1().he_support_160mhz) {
            role->set_mcs_nss_160(he_mcs[1]);
        }
        if (role->flags1().he_support_80_80mhz) {
            role->set_mcs_nss_80_80(he_mcs[2]);
        }
        role->flags2().su_beamformer                = wifi6_caps->su_beamformer;
        role->flags2().su_beamformee                = wifi6_caps->su_beamformee;
        role->flags2().mu_Beamformer_status         = wifi6_caps->mu_Beamformer_status;
        role->flags2().beamformee_sts_less_80mhz    = wifi6_caps->beamformee_sts_less_80mhz;
        role->flags2().beamformee_sts_greater_80mhz = wifi6_caps->beamformee_sts_greater_80mhz;
        role->flags2().ul_mu_mimo                   = wifi6_caps->ul_mu_mimo;
        role->flags2().ul_ofdma                     = wifi6_caps->ul_ofdma;
        role->flags2().dl_ofdma                     = wifi6_caps->dl_ofdma;
        role->flags3().max_dl_mu_mimo_tx            = wifi6_caps->max_dl_mu_mimo_tx;
        role->flags3().max_ul_mu_mimo_rx            = wifi6_caps->max_ul_mu_mimo_rx;
        role->max_ul_ofdma_rx()                     = wifi6_caps->max_ul_ofdma_rx;
        role->max_dl_ofdma_tx()                     = wifi6_caps->max_dl_ofdma_tx;
        role->flags4().rts                          = wifi6_caps->rts;
        role->flags4().mu_rts                       = wifi6_caps->mu_rts;
        role->flags4().multi_bssid                  = wifi6_caps->multi_bssid;
        role->flags4().mu_edca                      = wifi6_caps->mu_edca;
        role->flags4().twt_requester                = wifi6_caps->twt_requester;
        role->flags4().twt_responder                = wifi6_caps->twt_responder;
        role->flags4().spatial_reuse                = wifi6_caps->spatial_reuse;
        role->flags4().anticipated_channel_usage    = wifi6_caps->anticipated_channel_usage;

        if (!tlv->add_role(role)) {
            LOG(ERROR) << "add_role for index " << agent_role << " failed";
            return false;
        }
    }
    return true;
}

bool CapabilityReportingTask::add_ap_radio_advanced_capabilities_tlv(const std::string &iface_name)
{
    auto db    = AgentDB::get();
    auto radio = db->radio(iface_name);
    if (!radio) {
        return false;
    }

    auto ap_radio_advanced_capabilities_tlv =
        m_cmdu_tx.addClass<wfa_map::tlvProfile2ApRadioAdvancedCapabilities>();
    if (!ap_radio_advanced_capabilities_tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvProfile2ApRadioAdvancedCapabilities failed";
        return false;
    }

    ap_radio_advanced_capabilities_tlv->radio_uid() = radio->front.iface_mac;

    /* Currently Set the flag as we don't support traffic separation. */
    ap_radio_advanced_capabilities_tlv->advanced_radio_capabilities().combined_front_back =
        radio->front.hybrid_mode_supported;
    ap_radio_advanced_capabilities_tlv->advanced_radio_capabilities()
        .combined_profile1_and_profile2 = 0;

    return true;
}

bool CapabilityReportingTask::add_cac_capabilities_tlv()
{
    auto cac_capabilities_tlv = m_cmdu_tx.addClass<wfa_map::tlvProfile2CacCapabilities>();
    if (!cac_capabilities_tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvProfile2CacCapabilities has failed";
        return false;
    }

    // country code
    const auto &country_code               = m_cac_capabilities.get_country_code();
    *cac_capabilities_tlv->country_code(0) = country_code[0];
    *cac_capabilities_tlv->country_code(1) = country_code[1];

    // get all cac radios
    auto cac_radios = m_cac_capabilities.get_cac_radios();

    // fill in the tlv

    // for each radio
    for (const auto &radio : cac_radios) {
        // read cac methods for the radio
        auto cac_radio_methods = beerocks::get_radio_cac_methods(m_cac_capabilities, radio);

        // create tlv radios
        auto radios_tlv = cac_capabilities_tlv->create_cac_radios();
        if (!radios_tlv) {
            LOG(ERROR) << "unable to create cac radios";
            return false;
        }

        radios_tlv->radio_uid() = radio;

        // create cac type tlv for each CAC method
        for (const auto &cac_method : cac_radio_methods.second) {
            auto cac_type_tlv = radios_tlv->create_cac_types();
            if (!cac_type_tlv) {
                LOG(ERROR) << "unable to create cac types";
                return false;
            }
            cac_type_tlv->cac_method() = static_cast<wfa_map::eCacMethod>(cac_method);

            uint32_t duration = m_cac_capabilities.get_cac_completion_duration(radio, cac_method);
            memcpy(cac_type_tlv->duration(), &duration, 3);

            // operating classes
            const CacCapabilities::CacOperatingClasses &cac_operating_classes =
                m_cac_capabilities.get_cac_operating_classes(radio, cac_method);

            // for each {operating-class,[channels]}
            for (auto &operating_class_channels : cac_operating_classes) {
                auto operating_classes_tlv = cac_type_tlv->create_operating_classes();
                if (!operating_classes_tlv) {
                    LOG(ERROR) << "unable to create cac operating classes";
                    return false;
                }
                operating_classes_tlv->operating_class() = operating_class_channels.first;
                auto channels_tlv =
                    operating_classes_tlv->alloc_channels(operating_class_channels.second.size());
                if (!channels_tlv) {
                    LOG(ERROR) << "unable to create cac channles";
                    return false;
                }
                for (size_t i = 0; i < operating_class_channels.second.size(); ++i) {
                    *operating_classes_tlv->channels(i) = operating_class_channels.second[i];
                }

                // add to cac type
                if (!cac_type_tlv->add_operating_classes(operating_classes_tlv)) {
                    LOG(ERROR) << "Failed adding operating classes to CAC type TLV";
                    return false;
                }
            }
            if (!radios_tlv->add_cac_types(cac_type_tlv)) {
                LOG(ERROR) << "Failed adding CAC types to CAC radios tlv";
                return false;
            }
        }
        // add the cac type back to the radios tlv
        if (!cac_capabilities_tlv->add_cac_radios(radios_tlv)) {
            LOG(ERROR) << "Failed adding CAC radios to CAC capabilities TLV";
            return false;
        }
    }
    return true;
}

bool CapabilityReportingTask::add_metric_collection_interval_tlv()
{
    /* Note: at the moment we are not setting a value for collection_interval */
    auto profile2_metric_collection_interval_tlv =
        m_cmdu_tx.addClass<wfa_map::tlvProfile2MetricCollectionInterval>();
    if (!profile2_metric_collection_interval_tlv) {
        LOG(ERROR) << "error creating TLV_PROFILE2_METRIC_COLLECTION_INTERVAL";
        return false;
    }

    return true;
}

bool CapabilityReportingTask::add_device_inventory_tlv()
{
    auto device_inventory_tlv = m_cmdu_tx.addClass<wfa_map::tlvDeviceInventory>();
    if (!device_inventory_tlv) {
        LOG(ERROR) << "Failed building message!";
        return false;
    }
    auto db = AgentDB::get();

    device_inventory_tlv->set_serial_number(db->device_conf.device_serial_number);
    device_inventory_tlv->set_software_version(db->device_conf.software_version);
    device_inventory_tlv->set_execution_environment(db->device_conf.operating_system);

    for (auto radio : db->get_radios_list()) {
        auto radio_vendor    = device_inventory_tlv->create_radios_vendor_info();
        radio_vendor->ruid() = radio->front.iface_mac;
        radio_vendor->set_chipset_vendor(radio->chipset_vendor);
        device_inventory_tlv->add_radios_vendor_info(radio_vendor);
    }

    return true;
}

bool CapabilityReportingTask::add_profile2_ap_capability_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    auto profile2_ap_capability_tlv = cmdu_tx.addClass<wfa_map::tlvProfile2ApCapability>();
    if (!profile2_ap_capability_tlv) {
        LOG(ERROR) << "Failed building message!";
        return false;
    }

    auto db = AgentDB::get();
    if (db->controller_info.profile_support ==
        wfa_map::tlvProfile2MultiApProfile::eMultiApProfile::MULTIAP_PROFILE_1) {
        // If the Multi-AP Agent onboards to a Multi-AP Controller that implements Profile-1, the
        // Multi-AP Agent shall set the Byte Counter Units field to 0x00 (bytes) and report the
        // values of the BytesSent and BytesReceived fields in the Associated STA Traffic Stats TLV
        // in bytes. Section 9.1 of the spec.
        db->device_conf.byte_counter_units =
            wfa_map::tlvProfile2ApCapability::eByteCounterUnits::BYTES;
    } else {
        // If a Multi-AP Agent that implements Profile-2 sends a Profile-2 AP Capability TLV
        // shall set the Byte Counter Units field to 0x01 (KiB (kibibytes)). Section 9.1 of the spec.
        db->device_conf.byte_counter_units =
            wfa_map::tlvProfile2ApCapability::eByteCounterUnits::KIBIBYTES;
    }

    profile2_ap_capability_tlv->capabilities_bit_field().byte_counter_units =
        db->device_conf.byte_counter_units;

    // Calculate max total number of VLANs which can be configured on the Agent, and save it on
    // on the AgentDB.
    db->traffic_separation.max_number_of_vlans_ids =
        db->get_radios_list().size() * eBeeRocksIfaceIds::IFACE_TOTAL_VAPS;

    profile2_ap_capability_tlv->max_total_number_of_vids() =
        db->traffic_separation.max_number_of_vlans_ids;

    profile2_ap_capability_tlv->max_prioritization_rules() =
        db->device_conf.max_prioritization_rules;
    profile2_ap_capability_tlv->capabilities_bit_field().prioritization =
        (db->device_conf.max_prioritization_rules > 0) ? 1 : 0;
    return true;
}

} // namespace beerocks
