/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "base_wlan_hal_whm.h"

#include <bcl/beerocks_string_utils.h>
#include <bcl/beerocks_utils.h>
#include <bcl/network/network_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <bwl/key_value_parser.h>
#include <bwl/nl80211_client_factory.h>

#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <easylogging++.h>

extern "C" {
#include <libubox/utils.h>
}

using namespace beerocks;
using namespace wbapi;

namespace bwl {
namespace whm {

base_wlan_hal_whm::base_wlan_hal_whm(HALType type, const std::string &iface_name,
                                     hal_event_cb_t callback, const hal_conf_t &hal_conf)
    : base_wlan_hal(type, iface_name, IfaceType::Intel, callback, hal_conf),
      beerocks::beerocks_fsm<whm_fsm_state, whm_fsm_event>(whm_fsm_state::Delay),
      m_iso_nl80211_client(nl80211_client_factory::create_instance())
{

    LOG_IF(!m_ambiorix_cl.connect(AMBIORIX_USP_BACKEND_PATH, AMBIORIX_PWHM_USP_BACKEND_URI), FATAL)
        << "Unable to connect to the ambiorix backend!";

    m_fds_ext_events.clear();
    m_ambiorix_cl.resolve_path(wbapi_utils::search_path_radio_by_iface(iface_name), m_radio_path);

    LOG(DEBUG) << "init base_wlan_hal_whm for " << m_radio_path;
    get_radio_mac();
    refresh_radio_info();

    // Initialize the FSM
    fsm_setup();
}

base_wlan_hal_whm::~base_wlan_hal_whm() { base_wlan_hal_whm::detach(); }

void base_wlan_hal_whm::subscribe_to_radio_events()
{
    // subscribe to radio wpaCtrlEvents notifications
    auto wpaCtrl_Event_handler         = std::make_shared<sAmbiorixEventHandler>();
    wpaCtrl_Event_handler->event_type  = AMX_CL_WPA_CTRL_EVT;
    wpaCtrl_Event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string data;
        if (!event_data || !event_data.read_child(data, "eventData")) {
            return;
        }
        process_wpa_ctrl_event(event_data);
    };
    std::string wpaCtrl_filter =
        "(path matches '" + m_radio_path + "$') && (notification == '" + AMX_CL_WPA_CTRL_EVT + "')";

    m_ambiorix_cl.subscribe_to_object_event(m_radio_path, wpaCtrl_Event_handler, wpaCtrl_filter);

    // subscribe to the WiFi.Radio.iface_name.Status
    auto event_handler        = std::make_shared<sAmbiorixEventHandler>();
    event_handler->event_type = AMX_CL_OBJECT_CHANGED_EVT;

    event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        auto parameters = event_data.find_child("parameters");
        if (!parameters || parameters->empty()) {
            return;
        }
        auto params_map = parameters->read_children<AmbiorixVariantMapSmartPtr>();
        if (!params_map) {
            return;
        }
        for (auto &param_it : *params_map) {
            auto key   = param_it.first;
            auto value = param_it.second.find_child("to");
            if (key.empty() || !value || value->empty()) {
                continue;
            }
            process_radio_event(get_iface_name(), key, value.get());
        }
    };

    std::string filter = "(path matches '" + m_radio_path +
                         "$')"
                         " && (notification == '" +
                         AMX_CL_OBJECT_CHANGED_EVT +
                         "')"
                         " && (contains('parameters.Status'))";

    m_ambiorix_cl.subscribe_to_object_event(m_radio_path, event_handler, filter);

    std::string wifi_radio_channel_path = m_radio_path + "ChannelMgt.TargetChanspec.";
    std::string filter_radio_channel    = "(path matches '" + wifi_radio_channel_path +
                                       "$')"
                                       " && (notification == '" +
                                       AMX_CL_OBJECT_CHANGED_EVT +
                                       "')"
                                       " && (contains('parameters.Channel'))";

    m_ambiorix_cl.subscribe_to_object_event(wifi_radio_channel_path, event_handler,
                                            filter_radio_channel);
}

void base_wlan_hal_whm::subscribe_to_radio_channel_change_events()
{

    auto event_handler         = std::make_shared<sAmbiorixEventHandler>();
    event_handler->event_type  = AMX_CL_CHANNEL_CHANGE_EVT;
    event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string notif_name;
        if (!event_data.read_child(notif_name, "notification")) {
            LOG(DEBUG) << "Received Notification  without 'notification' param!";
            return;
        }
        if (notif_name != AMX_CL_CHANNEL_CHANGE_EVT) {
            LOG(DEBUG) << "Received wrong Notification : " << notif_name
                       << " instead of: " << AMX_CL_CHANNEL_CHANGE_EVT;
            return;
        }

        process_radio_channel_change_event(&event_data);
    };

    std::string filter = "(path matches '" + m_radio_path +
                         "$')"
                         " && (notification == '" +
                         AMX_CL_CHANNEL_CHANGE_EVT + "')";

    m_ambiorix_cl.subscribe_to_object_event(m_radio_path, event_handler, filter);
}

void base_wlan_hal_whm::subscribe_to_ap_events()
{
    std::string wifi_ap_path = wbapi_utils::search_path_ap();
    // subscribe to accesspoint wpaCtrlEvents notifications
    auto wpaCtrl_Event_handler         = std::make_shared<sAmbiorixEventHandler>();
    wpaCtrl_Event_handler->event_type  = AMX_CL_WPA_CTRL_EVT;
    wpaCtrl_Event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string ap_path;
        if (!event_data.read_child(ap_path, "path") || ap_path.empty()) {
            return;
        }

        auto vap_it =
            std::find_if(m_vapsExtInfo.begin(), m_vapsExtInfo.end(),
                         [&](const auto &element) { return element.second.path == ap_path; });
        if (vap_it == m_vapsExtInfo.end()) {
            return;
        }
        std::string data;
        if (!event_data || !event_data.read_child(data, "eventData")) {
            return;
        }
        process_wpa_ctrl_event(event_data);
    };
    std::string wpaCtrl_filter = "(path matches '" + wifi_ap_path +
                                 "[0-9]+.$') && (notification == '" + AMX_CL_WPA_CTRL_EVT + "')";

    m_ambiorix_cl.subscribe_to_object_event(wifi_ap_path, wpaCtrl_Event_handler, wpaCtrl_filter);

    // subscribe to the WiFi.Accesspoint.iface_name.Status
    auto event_handler         = std::make_shared<sAmbiorixEventHandler>();
    event_handler->event_type  = AMX_CL_OBJECT_CHANGED_EVT;
    event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string ap_path;
        if (!event_data.read_child(ap_path, "path") || ap_path.empty()) {
            return;
        }

        auto vap_it =
            std::find_if(m_vapsExtInfo.begin(), m_vapsExtInfo.end(),
                         [&](const auto &element) { return element.second.path == ap_path; });
        if (vap_it == m_vapsExtInfo.end()) {
            return;
        }
        LOG(WARNING) << "event from iface " << vap_it->first;
        auto parameters = event_data.find_child("parameters");
        if (!parameters || parameters->empty()) {
            return;
        }
        auto params_map = parameters->read_children<AmbiorixVariantMapSmartPtr>();
        if (!params_map) {
            return;
        }
        for (auto &param_it : *params_map) {
            auto key   = param_it.first;
            auto value = param_it.second.find_child("to");
            if (key.empty() || !value || value->empty()) {
                continue;
            }
            if (key == "Status") {
                auto status = value->get<std::string>();
                if (status == "Enabled" && !has_enabled_vap()) {
                    process_radio_event(get_iface_name(), "AccessPointNumberOfEntries",
                                        AmbiorixVariant::copy(1).get());
                }
                vap_it->second.status = status;
                process_ap_event(vap_it->first, key, value.get());
            } else {
                process_ap_event(vap_it->first, key, value.get());
            }
        }
    };

    std::string filter = "(path matches '" + wifi_ap_path +
                         "[0-9]+.$')"
                         " && (notification == '" +
                         AMX_CL_OBJECT_CHANGED_EVT +
                         "')"
                         " && (contains('parameters.Status'))";

    m_ambiorix_cl.subscribe_to_object_event(wifi_ap_path, event_handler, filter);
}

void base_wlan_hal_whm::subscribe_to_sta_events()
{
    std::string wifi_ad_path  = wbapi_utils::search_path_ap() + "[0-9]+.AssociatedDevice.";
    auto event_handler        = std::make_shared<sAmbiorixEventHandler>();
    event_handler->event_type = AMX_CL_OBJECT_CHANGED_EVT;

    event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string sta_path;
        if (!event_data.read_child(sta_path, "path") || sta_path.empty()) {
            return;
        }
        std::string ap_path = wbapi_utils::get_path_ap_of_assocDev(sta_path);

        auto vap_it =
            std::find_if(m_vapsExtInfo.begin(), m_vapsExtInfo.end(),
                         [&](const auto &element) { return element.second.path == ap_path; });
        if (vap_it == m_vapsExtInfo.end()) {
            return;
        }

        auto sta_it = std::find_if(m_stations.begin(), m_stations.end(), [&](const auto &element) {
            return element.second.path == sta_path;
        });
        std::string sta_mac;
        auto sta_mac_obj = event_data.find_child_deep("parameters.MACAddress.to");
        if (sta_mac_obj && !sta_mac_obj->empty()) {
            sta_mac = sta_mac_obj->get<std::string>();
        } else if (sta_it != m_stations.end()) {
            sta_mac = sta_it->first;
        } else if (!m_ambiorix_cl.get_param<>(sta_mac, sta_path, "MACAddress")) {
            LOG(WARNING) << "unknown sta path " << sta_path;
            return;
        }
        if (sta_it != m_stations.end()) {
            sta_it->second.path = sta_path;
        } else if (!sta_mac.empty()) {
            m_stations.insert(std::make_pair(sta_mac, sStationInfo(sta_path)));
        } else {
            LOG(WARNING) << "missing station mac";
            return;
        }
        auto parameters = event_data.find_child("parameters");
        if (!parameters || parameters->empty()) {
            return;
        }
        auto params_map = parameters->read_children<AmbiorixVariantMapSmartPtr>();
        if (!params_map) {
            return;
        }
        for (auto &param_it : *params_map) {
            auto key   = param_it.first;
            auto value = param_it.second.find_child("to");
            if (key.empty() || key == "MACAddress" || !value || value->empty()) {
                continue;
            }
            process_sta_connected_event(vap_it->first, sta_mac, key, value.get());
        }
    };

    std::string filter = "(path matches '" + wifi_ad_path +
                         "[0-9]+.$')"
                         " && (notification == '" +
                         AMX_CL_OBJECT_CHANGED_EVT +
                         "')"
                         " && ((contains('parameters.AuthenticationState'))"
                         " || (contains('parameters.MACAddress')))";

    // TODO : switch the subscription object path back to wifi_ad_path once libamxb client start supporting large path subscriptions
    m_ambiorix_cl.subscribe_to_object_event(wbapi_utils::search_path_ap(), event_handler, filter);

    // station instances cleanup
    auto sta_del_event_handler        = std::make_shared<sAmbiorixEventHandler>();
    sta_del_event_handler->event_type = AMX_CL_INSTANCE_REMOVED_EVT;

    sta_del_event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string sta_templ_path;
        uint32_t sta_index;
        if (!event_data.read_child(sta_templ_path, "path") || sta_templ_path.empty() ||
            !event_data.read_child(sta_index, "index") || !sta_index) {
            return;
        }
        std::string sta_path = sta_templ_path + std::to_string(sta_index) + ".";
        LOG(DEBUG) << "Station instance " << sta_path << " deleted";

        auto sta_it = std::find_if(m_stations.begin(), m_stations.end(), [&](const auto &element) {
            return element.second.path == sta_path;
        });
        if (sta_it != m_stations.end()) {
            LOG(DEBUG) << "Clearing Station " << sta_it->first;
            m_stations.erase(sta_it);
        }
    };

    filter = "(path matches '" + wifi_ad_path +
             "$')"
             " && (notification == '" +
             AMX_CL_INSTANCE_REMOVED_EVT + "')";

    m_ambiorix_cl.subscribe_to_object_event(wbapi_utils::search_path_ap(), sta_del_event_handler,
                                            filter);

    auto disasoc_event_handler         = std::make_shared<sAmbiorixEventHandler>();
    disasoc_event_handler->event_type  = AMX_CL_DISASSOC_EVT;
    disasoc_event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string ap_path;
        if (!event_data.read_child(ap_path, "path") || ap_path.empty()) {
            return;
        }

        auto vap_it =
            std::find_if(m_vapsExtInfo.begin(), m_vapsExtInfo.end(),
                         [&](const auto &element) { return element.second.path == ap_path; });
        if (vap_it == m_vapsExtInfo.end()) {
            LOG(DEBUG) << "vap_it not found";
            return;
        }
        LOG(DEBUG) << "event from iface " << vap_it->first;

        if (!process_sta_disassoc_event(vap_it->first, &event_data)) {
            LOG(ERROR) << "Failed to process sta disassoc event from iface " << vap_it->first;
        }
    };

    filter = "(path matches '" + wbapi_utils::search_path_ap() +
             "[0-9]+.$')"
             " && (notification == '" +
             AMX_CL_DISASSOC_EVT + "')";

    m_ambiorix_cl.subscribe_to_object_event(wbapi_utils::search_path_ap(), disasoc_event_handler,
                                            filter);
}

void base_wlan_hal_whm::subscribe_to_rssi_eventing_events()
{

    m_rssi_event_handler              = std::make_shared<sAmbiorixEventHandler>();
    m_rssi_event_handler->event_type  = AMX_CL_RSSI_UPDATE_EVT;
    m_rssi_event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string notif_name;
        if (!event_data.read_child(notif_name, "notification")) {
            LOG(DEBUG) << " Received Notification  without 'notification' param!";
            return;
        }
        if (notif_name != AMX_CL_RSSI_UPDATE_EVT) {
            LOG(DEBUG) << " Received wrong Notification : " << notif_name
                       << " instead of: " << AMX_CL_RSSI_UPDATE_EVT;
            return;
        }

        auto updates = event_data.find_child("Update"); //htable
        if (!updates) {
            LOG(ERROR) << " received ScanComplete event without Updates param!";
            return;
        }

        process_rssi_eventing_event(get_iface_name(), updates.get());
    };

    //std::string filter = "(notification == '" + std::string(AMX_CL_RSSI_UPDATE_EVT) + "')";
    std::string filter = "";
    if (!m_ambiorix_cl.subscribe_to_object_event(m_radio_path + "NaStaMonitor.RssiEventing.",
                                                 m_rssi_event_handler, filter)) {
        LOG(ERROR) << "failed to subscribe to RSSSI eventing";
    };
}

bool base_wlan_hal_whm::process_radio_event(const std::string &interface, const std::string &key,
                                            const AmbiorixVariant *value)
{
    return true;
}

bool base_wlan_hal_whm::process_radio_channel_change_event(const AmbiorixVariant *value)
{
    return true;
}

bool base_wlan_hal_whm::process_ap_event(const std::string &interface, const std::string &key,
                                         const AmbiorixVariant *value)
{
    return true;
}

bool base_wlan_hal_whm::process_sta_connected_event(const std::string &interface,
                                                    const std::string &sta_mac,
                                                    const std::string &key,
                                                    const AmbiorixVariant *value)
{
    return true;
}

bool base_wlan_hal_whm::process_sta_disassoc_event(
    const std::string &interface, const beerocks::wbapi::AmbiorixVariant *event_data)
{
    return true;
}

void base_wlan_hal_whm::process_rssi_eventing_event(const std::string &interface,
                                                    beerocks::wbapi::AmbiorixVariant *updates)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
}

bool base_wlan_hal_whm::process_scan_complete_event(const std::string &result) { return true; }

bool base_wlan_hal_whm::process_wpa_ctrl_event(const beerocks::wbapi::AmbiorixVariant &event_data)
{
    return true;
}

bool base_wlan_hal_whm::fsm_setup() { return true; }

HALState base_wlan_hal_whm::attach(bool block)
{
    m_radio_info.radio_state = eRadioState::ENABLED;
    populate_channels_max_tx_power();
    refresh_radio_info();
    return (m_hal_state = HALState::Operational);
}

void base_wlan_hal_whm::update_eht_capabilities()
{

    auto *ap_mode_support   = &m_radio_info.ap_modes_support;
    auto *sta_mode_support  = &m_radio_info.bsta_modes_support;
    bool ap_role_supported  = false;
    bool sta_role_supported = false;

    const std::string ap_role_path  = m_radio_path + "Capabilities.WiFi7APRole.";
    const std::string sta_role_path = m_radio_path + "Capabilities.WiFi7STARole.";

    // Wi-Fi 7 AP Role
    m_ambiorix_cl.get_param(ap_role_supported, ap_role_path, "EMLMRSupport");
    ap_mode_support->emlmr_support = ap_role_supported;

    m_ambiorix_cl.get_param(ap_role_supported, ap_role_path, "EMLSRSupport");
    ap_mode_support->emlsr_support = ap_role_supported;

    m_ambiorix_cl.get_param(ap_role_supported, ap_role_path, "NSTRSupport");
    ap_mode_support->nstr_support = ap_role_supported;

    m_ambiorix_cl.get_param(ap_role_supported, ap_role_path, "STRSupport");
    ap_mode_support->str_support = ap_role_supported;

    // Wi-Fi 7 STA Role
    m_ambiorix_cl.get_param(sta_role_supported, sta_role_path, "EMLMRSupport");
    sta_mode_support->emlmr_support = sta_role_supported;

    m_ambiorix_cl.get_param(sta_role_supported, sta_role_path, "EMLSRSupport");
    sta_mode_support->emlsr_support = sta_role_supported;

    m_ambiorix_cl.get_param(sta_role_supported, sta_role_path, "NSTRSupport");
    sta_mode_support->nstr_support = sta_role_supported;

    m_ambiorix_cl.get_param(sta_role_supported, sta_role_path, "STRSupport");
    sta_mode_support->str_support = sta_role_supported;

    LOG(DEBUG) << "Param method, EHT Supported: " << m_radio_info.eht_supported << "\n"
               << "AP Modes [STR=" << ap_mode_support->str_support
               << ", NSTR=" << ap_mode_support->nstr_support
               << ", EMLSR=" << ap_mode_support->emlsr_support
               << ", EMLMR=" << ap_mode_support->emlmr_support << "]\n"
               << "STA Modes [STR=" << sta_mode_support->str_support
               << ", NSTR=" << sta_mode_support->nstr_support
               << ", EMLSR=" << sta_mode_support->emlsr_support
               << ", EMLMR=" << sta_mode_support->emlmr_support << "]";
}

bool base_wlan_hal_whm::detach() { return true; }

bool base_wlan_hal_whm::set(const std::string &param, const std::string &value, int vap_id)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool base_wlan_hal_whm::ping() { return true; }
bool base_wlan_hal_whm::reassociate() { return true; }

/* 9.4.2.55.4 Supported MCS Set field */
#define RX_HT_MCS_BITMASK_LEN 10

/* 9.4.2.158.3 Supported VHT-MCS and NSS Set field */
#define RX_VHT_MCS_MAP_OFFSET 0
#define TX_VHT_MCS_MAP_OFFSET 4

/* 9.4.2.248.4 Supported HE-MCS And NSS Set Field */
#define RX_HE_MCS_MAP_80_OFFSET 0
#define TX_HE_MCS_MAP_80_OFFSET 2
#define RX_HE_MCS_MAP_160_OFFSET 4
#define TX_HE_MCS_MAP_160_OFFSET 6
#define RX_HE_MCS_MAP_8080_OFFSET 8
#define TX_HE_MCS_MAP_8080_OFFSET 10

bool base_wlan_hal_whm::refresh_radio_info()
{
    if (m_radio_path.empty()) {
        m_ambiorix_cl.resolve_path(wbapi_utils::search_path_radio_by_iface(m_radio_info.iface_name),
                                   m_radio_path);
    }
    auto radio = m_ambiorix_cl.get_object(m_radio_path);
    if (!radio) {
        LOG(ERROR) << " cannot refresh radio info, radio object missing ";
        return false;
    }

    std::string s_chipset_vendor;
    radio->read_child(s_chipset_vendor, "ChipsetVendor");
    m_radio_info.chipset_vendor = s_chipset_vendor;

    std::string s_val;
    if (radio->read_child(s_val, "OperatingFrequencyBand")) {
        m_radio_info.frequency_band = wbapi_utils::band_to_freq(s_val);
    }
    m_radio_info.is_5ghz = (m_radio_info.frequency_band == beerocks::eFreqType::FREQ_5G);
    radio->read_child(m_radio_info.wifi_ctrl_enabled, "Enable");

    if (radio->read_child(s_val, "CurrentOperatingChannelBandwidth")) {
        m_radio_info.bandwidth = wbapi_utils::bandwith_from_string(s_val);
    }
    radio->read_child(m_radio_info.channel, "Channel");
    m_radio_info.is_dfs_channel =
        son::wireless_utils::is_dfs_channel(m_radio_info.channel, m_radio_info.frequency_band);

    std::unordered_set<std::string> cleared_channels_set;
    std::unordered_set<std::string> radar_triggered_channels_set;

    std::string channel_mgt_path = m_radio_path + "ChannelMgt.";
    auto channel_mgt_obj         = m_ambiorix_cl.get_object(channel_mgt_path);
    if (!channel_mgt_obj) {
        LOG(ERROR) << "failed to get radio Stats object " << channel_mgt_path;
        return true;
    }
    channel_mgt_obj->read_child(s_val, "ClearedDfsChannels");
    auto cleared_channels_vec = beerocks::string_utils::str_split(s_val, ',');
    cleared_channels_set.insert(cleared_channels_vec.cbegin(), cleared_channels_vec.cend());

    channel_mgt_obj->read_child(s_val, "RadarTriggeredDfsChannels");
    auto radar_triggered_channels_vec = beerocks::string_utils::str_split(s_val, ',');
    radar_triggered_channels_set.insert(radar_triggered_channels_vec.cbegin(),
                                        radar_triggered_channels_vec.cend());

    if (radio->read_child(s_val, "PossibleChannels")) {
        auto channels_vec = beerocks::string_utils::str_split(s_val, ',');
        for (auto &chan_str : channels_vec) {
            uint32_t chanNum   = beerocks::string_utils::stoi(chan_str);
            auto &channel_info = m_radio_info.channels_list[chanNum];

            if (son::wireless_utils::is_dfs_channel(chanNum, m_radio_info.frequency_band)) {
                if (cleared_channels_set.find(chan_str) != cleared_channels_set.end()) {
                    channel_info.dfs_state = beerocks::eDfsState::AVAILABLE;
                } else if (radar_triggered_channels_set.find(chan_str) !=
                           radar_triggered_channels_set.end()) {
                    channel_info.dfs_state = beerocks::eDfsState::UNAVAILABLE;
                } else {
                    channel_info.dfs_state = beerocks::eDfsState::USABLE;
                }

            } else {
                channel_info.dfs_state = beerocks::eDfsState::DFS_STATE_MAX;
            }

            if (radio->read_child(s_val, "MaxChannelBandwidth")) {
                m_radio_info.max_bandwidth = wbapi_utils::bandwith_from_string(s_val);
                std::vector<beerocks::eWiFiBandwidth> bandwidths;
                if (radio->read_child(s_val, "SupportedOperatingChannelBandwidth")) {
                    auto supported_bandwiths_vec = beerocks::string_utils::str_split(s_val, ',');
                    for (const auto &bw : supported_bandwiths_vec) {
                        if (!bw.compare("Auto")) {
                            continue;
                        }
                        bandwidths.push_back(wbapi_utils::bandwith_from_string(bw));
                    }
                }
                for (auto &bandw_iter : bandwidths) {
                    if (bandw_iter > m_radio_info.max_bandwidth) {
                        continue;
                    }
                    // Fill with the lowest operable preference value
                    channel_info.bw_info_list[bandw_iter] = 1;
                }
            }
        }
    }

    // Capabilities
    std::string op_std_format;
    std::string operating_standards;
    radio->read_child(op_std_format, "OperatingStandardsFormat");
    radio->read_child(operating_standards, "OperatingStandards");
    if (op_std_format == "Legacy") {
        // Legacy - maximum supported operating standard
        uint8_t rank = 0;
        if (operating_standards == "n")
            rank = 1;
        if (operating_standards == "ac")
            rank = 2;
        if (operating_standards == "ax")
            rank = 3;
        if (operating_standards == "be")
            rank = 4;

        m_radio_info.ht_supported  = (rank >= 1);
        m_radio_info.vht_supported = (rank >= 2);
        m_radio_info.he_supported  = (rank >= 3);
        m_radio_info.eht_supported = (rank >= 4);
    } else {
        // Standard - comma separated list
        m_radio_info.ht_supported  = operating_standards.find("n") != std::string::npos ? 1 : 0;
        m_radio_info.vht_supported = operating_standards.find("ac") != std::string::npos ? 1 : 0;
        m_radio_info.he_supported  = operating_standards.find("ax") != std::string::npos ? 1 : 0;
        m_radio_info.eht_supported = operating_standards.find("be") != std::string::npos ? 1 : 0;
    }

    //HT capabilities
    if (m_radio_info.ht_supported) {
        struct beerocks::net::sHTCapabilities *ht_caps_ptr =
            (struct beerocks::net::sHTCapabilities *)(&m_radio_info.ht_capability);

        if (radio->read_child(s_val, "RadCapabilitiesHTStr")) {
            m_radio_info.ht_capability = 0;
            auto ht_pwhm_vec           = beerocks::string_utils::str_split(s_val, ',');
            if (std::find(ht_pwhm_vec.begin(), ht_pwhm_vec.end(), "SHORT_GI_20") !=
                ht_pwhm_vec.end()) {
                ht_caps_ptr->short_gi_support_20mhz = 1;
            }
            if (std::find(ht_pwhm_vec.begin(), ht_pwhm_vec.end(), "SHORT_GI_40") !=
                ht_pwhm_vec.end()) {
                ht_caps_ptr->short_gi_support_40mhz = 1;
            }
            if (std::find(ht_pwhm_vec.begin(), ht_pwhm_vec.end(), "CAP_40") != ht_pwhm_vec.end()) {
                ht_caps_ptr->ht_support_40mhz = 1;
            }
        }

        if (radio->read_child(s_val, "SupportedHtMcsSet")) {
            auto nBytes = b64_decode(s_val.c_str(), m_radio_info.ht_mcs_set.data(),
                                     m_radio_info.ht_mcs_set.size());
            if (nBytes != beerocks::message::HT_MCS_SET_SIZE) {
                LOG(ERROR) << "Failed to decode SupportedHtMcsSet str";
            }
        }

        /*
         * 9.4.2.55.4 Supported HT-MCS Set field
         * First 10 bytes represent supported spatial streams indexed from 0.
         * The variable `ss` holds the highest supported stream index (0-based),
         * so total spatial streams = ss + 1.
         */
        uint8_t ss = 0;
        for (uint8_t i = 0; i < RX_HT_MCS_BITMASK_LEN; i++) {
            if (m_radio_info.ht_mcs_set[i]) {
                ss = i;
            }
        }
        ht_caps_ptr->max_num_of_supported_rx_spatial_streams = ss;
        ht_caps_ptr->max_num_of_supported_tx_spatial_streams = ss;
    }

    /*
     * Lambda to calculate spatial streams count for VHT and HE MCS sets.
     * According to IEEE 802.11-2020 (VHT) and 802.11ax-2021 (HE),
     * each spatial stream is encoded as 2 bits in a 16-bit MCS map.
     * Supported streams have 2-bit value < 3.
     * Returns highest supported stream index (0-based), so total streams = ss + 1.
     */
    auto calc_ss = [](const uint8_t *mcsSet, uint8_t offset) {
        uint8_t ss   = 0;
        uint16_t mcs = 0xffff;
        memcpy(&mcs, &mcsSet[offset], sizeof(mcs));
        /* up to 8 ss */
        for (int i = 0; i < 8; i++) {
            if (((mcs >> (2 * i)) & 0x3) < 3) {
                ss = i;
            }
        }
        return ss;
    };

    //VHT capabilities
    if (m_radio_info.vht_supported) {
        struct beerocks::net::sVHTCapabilities *vht_caps_ptr =
            (struct beerocks::net::sVHTCapabilities *)(&m_radio_info.vht_capability);

        if (radio->read_child(s_val, "RadCapabilitiesVHTStr")) {
            m_radio_info.vht_capability = 0;
            auto vht_pwhm_vec           = beerocks::string_utils::str_split(s_val, ',');
            if (std::find(vht_pwhm_vec.begin(), vht_pwhm_vec.end(), "SGI_80") !=
                vht_pwhm_vec.end()) {
                vht_caps_ptr->short_gi_support_80mhz = 1;
            }
            if (std::find(vht_pwhm_vec.begin(), vht_pwhm_vec.end(), "SGI_160") !=
                vht_pwhm_vec.end()) {
                vht_caps_ptr->short_gi_support_160mhz_and_80_80mhz = 1;
                vht_caps_ptr->vht_support_160mhz                   = 1;
            }
            if (std::find(vht_pwhm_vec.begin(), vht_pwhm_vec.end(), "SU_BFR") !=
                vht_pwhm_vec.end()) {
                vht_caps_ptr->su_beamformer_capable = 1;
            }
            if (std::find(vht_pwhm_vec.begin(), vht_pwhm_vec.end(), "MU_BFR") !=
                vht_pwhm_vec.end()) {
                vht_caps_ptr->mu_beamformer_capable = 1;
            }
        }

        if (radio->read_child(s_val, "SupportedVhtMcsNssSet")) {
            auto nBytes = b64_decode(s_val.c_str(), m_radio_info.vht_mcs_set.data(),
                                     m_radio_info.vht_mcs_set.size());
            if (nBytes != beerocks::message::VHT_MCS_SET_SIZE) {
                LOG(ERROR) << "Failed to decode SupportedVhtMcsNssSet str";
            }
        }

        vht_caps_ptr->max_num_of_supported_rx_spatial_streams =
            calc_ss(m_radio_info.vht_mcs_set.data(), RX_VHT_MCS_MAP_OFFSET);
        vht_caps_ptr->max_num_of_supported_tx_spatial_streams =
            calc_ss(m_radio_info.vht_mcs_set.data(), TX_VHT_MCS_MAP_OFFSET);
    }

    //HE capabilities
    if (m_radio_info.he_supported) {
        struct beerocks::net::sHECapabilities *he_caps_ptr =
            (struct beerocks::net::sHECapabilities *)(&m_radio_info.he_capability);

        if (radio->read_child(s_val, "RadCapabilitiesHePhysStr")) {
            m_radio_info.he_capability = 0;
            auto he_pwhm_vec           = beerocks::string_utils::str_split(s_val, ',');
            if (std::find(he_pwhm_vec.begin(), he_pwhm_vec.end(), "SU_BEAMFORMER") !=
                he_pwhm_vec.end()) {
                he_caps_ptr->su_beamformer_capable = 1;
            }
            if (std::find(he_pwhm_vec.begin(), he_pwhm_vec.end(), "MU_BEAMFORMER") !=
                he_pwhm_vec.end()) {
                he_caps_ptr->mu_beamformer_capable = 1;
            }
            if (std::find(he_pwhm_vec.begin(), he_pwhm_vec.end(), "160MHZ_5GHZ") !=
                he_pwhm_vec.end()) {
                he_caps_ptr->he_support_160mhz = 1;
            }
            if (std::find(he_pwhm_vec.begin(), he_pwhm_vec.end(), "160_80_80_MHZ_5GHZ") !=
                he_pwhm_vec.end()) {
                he_caps_ptr->he_support_80_80mhz = 1;
            }
        }
        if (radio->read_child(s_val, "HeCapsSupported")) {
            auto he_pwhm_vec = beerocks::string_utils::str_split(s_val, ',');
            if (std::find(he_pwhm_vec.begin(), he_pwhm_vec.end(), "DL_OFDMA") !=
                he_pwhm_vec.end()) {
                he_caps_ptr->dl_ofdm_capable = 1;
            }
            if (std::find(he_pwhm_vec.begin(), he_pwhm_vec.end(), "UL_OFDMA") !=
                he_pwhm_vec.end()) {
                he_caps_ptr->ul_ofdm_capable = 1;
            }
            if (std::find(he_pwhm_vec.begin(), he_pwhm_vec.end(), "DL_MUMIMO") !=
                he_pwhm_vec.end()) {
                he_caps_ptr->dl_mu_mimo_and_ofdm_capable = 1;
            }
            if (std::find(he_pwhm_vec.begin(), he_pwhm_vec.end(), "UL_MUMIMO") !=
                he_pwhm_vec.end()) {
                he_caps_ptr->ul_mu_mimo_and_ofdm_capable = 1;
                he_caps_ptr->ul_mu_mimo_capable          = 1;
            }
        }

        if (radio->read_child(s_val, "SupportedHeMcsNssSet")) {
            auto nBytes = b64_decode(s_val.c_str(), m_radio_info.he_mcs_set.data(),
                                     m_radio_info.he_mcs_set.size());
            if (nBytes != beerocks::message::HE_MCS_SET_SIZE) {
                LOG(ERROR) << "Failed to decode SupportedHeMcsNssSet str";
            }
        }

        he_caps_ptr->max_num_of_supported_rx_spatial_streams =
            std::max<uint8_t>({calc_ss(m_radio_info.he_mcs_set.data(), RX_HE_MCS_MAP_80_OFFSET),
                               calc_ss(m_radio_info.he_mcs_set.data(), RX_HE_MCS_MAP_160_OFFSET),
                               calc_ss(m_radio_info.he_mcs_set.data(), RX_HE_MCS_MAP_8080_OFFSET)});

        he_caps_ptr->max_num_of_supported_tx_spatial_streams =
            std::max<uint8_t>({calc_ss(m_radio_info.he_mcs_set.data(), TX_HE_MCS_MAP_80_OFFSET),
                               calc_ss(m_radio_info.he_mcs_set.data(), TX_HE_MCS_MAP_160_OFFSET),
                               calc_ss(m_radio_info.he_mcs_set.data(), TX_HE_MCS_MAP_8080_OFFSET)});

        //Wi-Fi 6 capabilities
        struct beerocks::net::sWIFI6Capabilities *wifi6_caps_ptr =
            (struct beerocks::net::sWIFI6Capabilities *)(&m_radio_info.wifi6_capability);
        wifi6_caps_ptr->spatial_reuse = 0;

        const std::string spatial_reuse_path = m_radio_path + "IEEE80211ax.";
        bool spatial_reuse_supported         = false;
        uint8_t tmp_bss_color                = 0;
        bool tmp_bool                        = false;
        std::string tmp_bitmap;

        if (m_ambiorix_cl.get_param<>(tmp_bss_color, spatial_reuse_path, "BssColor")) {
            spatial_reuse_supported = true;
        }
        if (m_ambiorix_cl.get_param<>(tmp_bool, spatial_reuse_path, "BssColorPartial")) {
            spatial_reuse_supported = true;
        }
        if (m_ambiorix_cl.get_param<>(tmp_bool, spatial_reuse_path, "SRGInformationValid") &&
            tmp_bool) {
            spatial_reuse_supported = true;
        }
        if (m_ambiorix_cl.get_param<>(tmp_bool, spatial_reuse_path, "NonSRGOffsetValid") &&
            tmp_bool) {
            spatial_reuse_supported = true;
        }
        if (m_ambiorix_cl.get_param<>(tmp_bitmap, spatial_reuse_path, "SRGBSSColorBitmap") &&
            !tmp_bitmap.empty()) {
            spatial_reuse_supported = true;
        }
        if (m_ambiorix_cl.get_param<>(tmp_bitmap, spatial_reuse_path, "SRGPartialBSSIDBitmap") &&
            !tmp_bitmap.empty()) {
            spatial_reuse_supported = true;
        }
        if (m_ambiorix_cl.get_param<>(tmp_bitmap, spatial_reuse_path,
                                      "NeighborBSSColorInUseBitmap") &&
            !tmp_bitmap.empty()) {
            spatial_reuse_supported = true;
        }

        if (spatial_reuse_supported) {
            LOG(INFO) << "Spatial Reuse support is true";
            wifi6_caps_ptr->spatial_reuse = 1;
        }

        if (radio->read_child(s_val, "RadCapabilitiesHeMacStr")) {
            auto wifi6_pwhm_vec = beerocks::string_utils::str_split(s_val, ',');
            if (std::find(wifi6_pwhm_vec.begin(), wifi6_pwhm_vec.end(), "TWT_REQ") !=
                wifi6_pwhm_vec.end()) {
                wifi6_caps_ptr->twt_requester = 1;
            }
            if (std::find(wifi6_pwhm_vec.begin(), wifi6_pwhm_vec.end(), "TWT_RESP") !=
                wifi6_pwhm_vec.end()) {
                wifi6_caps_ptr->twt_responder = 1;
            }
        }

        wifi6_caps_ptr->dl_ofdma            = he_caps_ptr->dl_ofdm_capable;
        wifi6_caps_ptr->ul_ofdma            = he_caps_ptr->ul_ofdm_capable;
        wifi6_caps_ptr->ul_mu_mimo          = he_caps_ptr->ul_mu_mimo_and_ofdm_capable;
        wifi6_caps_ptr->he_support_160mhz   = he_caps_ptr->he_support_160mhz;
        wifi6_caps_ptr->he_support_80_80mhz = he_caps_ptr->he_support_80_80mhz;

        /*
         * 17.2.72 AP Wi-Fi 6 Capabilities TLV (EMR6)
         * The MCS NSS length shall be one of these values: 4, 8, or 12.
         * length = 4 (base) +4 if 160MHz, +4 if 80+80MHz supported.
         */
        wifi6_caps_ptr->mcs_nss_length =
            4 + (4 * !!he_caps_ptr->he_support_160mhz) + (4 * !!he_caps_ptr->he_support_80_80mhz);

        if (radio->read_child(s_val, "RadCapabilitiesHePhysStr")) {
            auto wifi6_pwhm_vec = beerocks::string_utils::str_split(s_val, ',');
            if (std::find(wifi6_pwhm_vec.begin(), wifi6_pwhm_vec.end(),
                          "BEAMFORMEE_STS_LE_80MHZ") != wifi6_pwhm_vec.end()) {
                wifi6_caps_ptr->beamformee_sts_less_80mhz = 1;
            }
            if (std::find(wifi6_pwhm_vec.begin(), wifi6_pwhm_vec.end(),
                          "BEAMFORMEE_STD_GT_80MHZ") != wifi6_pwhm_vec.end()) {
                wifi6_caps_ptr->beamformee_sts_greater_80mhz = 1;
            }
            if (std::find(wifi6_pwhm_vec.begin(), wifi6_pwhm_vec.end(), "SU_BEAMFORMER") !=
                wifi6_pwhm_vec.end()) {
                wifi6_caps_ptr->su_beamformer = 1;
            }
            if (std::find(wifi6_pwhm_vec.begin(), wifi6_pwhm_vec.end(), "SU_BEAMFORMEE") !=
                wifi6_pwhm_vec.end()) {
                wifi6_caps_ptr->su_beamformee = 1;
            }
            if (std::find(wifi6_pwhm_vec.begin(), wifi6_pwhm_vec.end(), "MU_BEAMFORMER") !=
                wifi6_pwhm_vec.end()) {
                wifi6_caps_ptr->mu_Beamformer_status = 1;
            }
        }
    }

    if (m_radio_info.eht_supported) {
        update_eht_capabilities();
    }

    if (radio->read_child(s_val, "ExtensionChannel")) {
        bool channel_ext_above = (s_val == "AboveControlChannel");
        if (!channel_ext_above && (s_val == "Auto") &&
            (m_radio_info.bandwidth > beerocks::eWiFiBandwidth::BANDWIDTH_20)) {
            if (m_radio_info.frequency_band != beerocks::eFreqType::FREQ_24G) {
                channel_ext_above = ((m_radio_info.channel / 4) % 2);
            } else {
                channel_ext_above = (m_radio_info.channel < 7);
            }
        }
        m_radio_info.channel_ext_above = channel_ext_above;
    }
    m_radio_info.vht_center_freq = son::wireless_utils::channel_to_vht_center_freq(
        m_radio_info.channel, m_radio_info.frequency_band, m_radio_info.bandwidth,
        m_radio_info.channel_ext_above);

    AmbiorixVariant result;
    AmbiorixVariant args;
    if (m_ambiorix_cl.call(m_radio_path, "getCurrentTransmitPowerdBm", args, result)) {
        auto results_as_list = result.read_children<AmbiorixVariantListSmartPtr>();
        if (!results_as_list) {
            LOG(ERROR) << "failed reading results!";
        }
        if (!(*results_as_list)[0].get(m_radio_info.tx_power)) {
            LOG(ERROR) << "failed getting results!";
        }
    }
    bool enable_flag = false;
    // because of the async nature of pwhm calls, and because of the strict Radio.Status
    // implementation, Radio.Status follows a path that goes through the Status=Down / Disabled
    // when certain operations, like enable(), are performed on AccessPoint instances above it;
    // reading the Radio.Status() during onboarding (system in transient state) returns various
    // values that would be long to enumerate; and some of these transient values forbid enabling
    // AccessPoints when they are non-transient
    // moreover, Radio.Enable constitutes a "promise" that
    // upon activating an AccessPoint, Radio.Status will go to Enable eventually (in a matter of seconds)
    if (radio->read_child(enable_flag, "Enable")) {
        m_radio_info.radio_state = utils_wlan_hal_whm::radio_state_from_bool(enable_flag);
        if (m_radio_info.radio_state == eRadioState::ENABLED) {
            m_radio_info.wifi_ctrl_enabled = 2; // Assume Operational
            m_radio_info.tx_enabled        = 1;
        }
    }
    m_ambiorix_cl.get_param(m_radio_info.ant_num, m_radio_path + "DriverStatus.", "NrTxAntenna");

    uint32_t max_bss;
    if (!m_ambiorix_cl.get_param(max_bss, m_radio_path, "MaxSupportedSSIDs")) {
        max_bss = 0;
        // agent shall use legacy method counting current instances of WiFi.AccessPoint.[RadioReference == m_radio_path].
    }
    LOG(INFO) << "set radio_max_bss_supported for [" << m_radio_path << "] to " << max_bss;
    m_radio_info.radio_max_bss_supported = uint8_t(max_bss);

    if (!m_radio_info.available_vaps.size()) {
        LOG(DEBUG) << "calling refresh_vaps_info because local info struct is empty";
        if (!refresh_vaps_info(beerocks::IFACE_RADIO_ID)) {
            LOG(ERROR) << " could not refresh vaps info for radio " << beerocks::IFACE_RADIO_ID;
            return false;
        }
    }

    read_rsn_support();

    return true;
}

bool base_wlan_hal_whm::get_radio_vaps(AmbiorixVariantMap &aps)
{
    aps.clear();

    auto result = m_ambiorix_cl.get_object_multi<AmbiorixVariantMapSmartPtr>(
        wbapi_utils::search_path_ap_inst());

    if (!result) {
        LOG(ERROR) << "could not get ap multi object for " << wbapi_utils::search_path_ap();
        return false;
    }

    auto radio_index = wbapi_utils::get_object_id(m_radio_path);
    std::string radio_path;
    for (auto &it : *result) {
        auto ap_path = it.first;
        auto &ap_obj = it.second;

        if (ap_obj.empty()) {
            LOG(ERROR) << "iteration on ap " << ap_path << " empty AP object";
            continue;
        }

        const auto ref = wbapi_utils::get_path_radio_reference(ap_obj);
        bool resolved  = m_ambiorix_cl.resolve_path(ref, radio_path);

        // Try #2 (only if first failed)
        if (!resolved) {
            LOG(INFO) << "Radio resolve path re-attempt for ap " << ap_path << " ref=" << ref;
            radio_path.clear(); // avoid stale values
            resolved = m_ambiorix_cl.resolve_path(ref, radio_path);

            if (!resolved) {
                LOG(ERROR) << "ap " << ap_path
                           << ": failed to resolve radio reference after 2 attempts ref=" << ref
                           << ", m_radio_path=" << m_radio_path;
                continue;
            }
        }

        auto vap_radio_index = wbapi_utils::get_object_id(radio_path);

        if (radio_index != vap_radio_index) {
            continue;
        }
        auto ssid_obj = m_ambiorix_cl.get_object(wbapi_utils::get_path_ssid_reference(ap_obj));
        if (!ssid_obj) {
            LOG(ERROR) << "problem with ssid reference for " << ap_path;
            continue;
        }
        std::string mac_addr = wbapi_utils::get_ssid_mac(*ssid_obj);

        if (mac_addr == "00:00:00:00:00:00" || mac_addr.empty()) {
            continue;
        }
        LOG(DEBUG) << "add " << ap_path << " to list of APs, with MAC " << mac_addr;
        aps.emplace(std::move(mac_addr), std::move(ap_obj));
    }
    return true;
}

bool base_wlan_hal_whm::get_accesspoint_by_ssid(std::string &ssid_path, std::string &ap_path)
{
    auto ssid_index = wbapi_utils::get_object_id(ssid_path);

    auto result = m_ambiorix_cl.get_object_multi<AmbiorixVariantMapSmartPtr>(
        wbapi_utils::search_path_ap_inst());

    if (!result) {
        LOG(ERROR) << "could not get ap multi object for " << wbapi_utils::search_path_ap();
        return false;
    }

    std::string ssid_ref;
    for (auto &it : *result) {
        auto &ap = it.second;
        if (ap.empty() ||
            !m_ambiorix_cl.resolve_path(wbapi_utils::get_path_ssid_reference(ap), ssid_ref)) {
            LOG(ERROR) << "VAP " << it.first << " inconsistent SSIDReference";
            continue;
        }
        auto vap_ssid_index = wbapi_utils::get_object_id(ssid_ref);
        if (vap_ssid_index == ssid_index) {
            ap_path = it.first;
            return true;
        }
    }
    return false;
}

bool base_wlan_hal_whm::has_enabled_vap() const
{
    auto vap_it =
        std::find_if(m_vapsExtInfo.begin(), m_vapsExtInfo.end(),
                     [&](const auto &element) { return element.second.status == "Enabled"; });
    return (vap_it != m_vapsExtInfo.end());
}

bool base_wlan_hal_whm::check_enabled_vap(const std::string &bss) const
{
    auto vap_it = m_vapsExtInfo.find(bss);
    return (vap_it != m_vapsExtInfo.end() && vap_it->second.status == "Enabled");
}

void base_wlan_hal_whm::read_rsn_support()
{
    auto result = m_ambiorix_cl.get_object_multi<AmbiorixVariantMapSmartPtr>(
        wbapi_utils::search_path_ap_inst());

    if (!result) {
        LOG(ERROR) << "could not get ap multi object for " << wbapi_utils::search_path_ap();
        return;
    }

    auto radio_index = wbapi_utils::get_object_id(m_radio_path);
    std::string radio_path;

    for (auto &it : *result) {
        auto ap_path = it.first;
        auto &ap_obj = it.second;

        if (ap_obj.empty()) {
            LOG(DEBUG) << "iteration on ap " << ap_path << " empty AP object";
            continue;
        }

        const auto ref = wbapi_utils::get_path_radio_reference(ap_obj);
        if (!m_ambiorix_cl.resolve_path(ref, radio_path)) {
            continue;
        }

        auto vap_radio_index = wbapi_utils::get_object_id(radio_path);

        if (radio_index != vap_radio_index) {
            continue;
        }

        auto security_obj = m_ambiorix_cl.get_object(ap_path + "Security.");
        if (!security_obj) {
            continue;
        }
        std::string modes_available;
        if (!security_obj->read_child(modes_available, "ModesAvailable")) {
            continue;
        }

        // OR-ing because capability is true if at least one AccessPoint advertises it
        m_radio_info.rsn_override_support |=
            (modes_available.find("WPA3-Personal-Compatibility") != std::string::npos);
    }

    //radio->wifi_channel.get_freq_type()
    LOG_IF(m_radio_info.rsn_override_support, DEBUG)
        << "Radio "
        << beerocks::utils::convert_frequency_type_to_string(m_radio_info.frequency_band)
        << " RSN Overriding Supported";
}

bool base_wlan_hal_whm::get_vap_status(
    const std::list<son::wireless_utils::sBssInfoConf> &bss_info_conf_list)
{
    bool ret             = true;
    bool all_teared_down = true;

    for (const auto &bss_info_conf : bss_info_conf_list) {
        if (bss_info_conf.teardown) {
            LOG(DEBUG) << "Teardown is true for BSSID: " << bss_info_conf.bssid;
            continue;
        }
        all_teared_down = false;

        const auto &vap_info =
            m_radio_info
                .available_vaps[get_vap_id_with_mac(tlvf::mac_to_string(bss_info_conf.bssid))];
        const auto &vap_it = m_vapsExtInfo.find(vap_info.bss);

        if (vap_it == m_vapsExtInfo.end()) {
            LOG(ERROR) << "Failed to get ifname for BSS: " << vap_info.bss;
            ret = false;
            continue;
        }

        const auto &wifi_vap_path = vap_it->second.path;
        LOG(DEBUG) << "VAP: " << vap_info.bss << " Path: " << wifi_vap_path;

        std::string status;
        if (!m_ambiorix_cl.get_param(status, wifi_vap_path, "Status")) {
            LOG(ERROR) << "Failed to get status for path: " << wifi_vap_path;
            return false;
        }

        LOG(DEBUG) << "Status of BSS: " << vap_info.bss << " from path: " << wifi_vap_path << " is "
                   << status;

        if (status != "Enabled") {
            LOG(DEBUG) << "Status is 'Disabled' for BSS: " << vap_info.bss;
            ret = false;
            break;
        }
    }

    if (all_teared_down) {
        LOG(DEBUG) << "Detected all vaps teared down. Maybe dev_reset_default is called";
        return true;
    }

    return ret;
}

bool base_wlan_hal_whm::refresh_vaps_info(int id)
{
    bool ret = false;

    AmbiorixVariantMap curr_vaps;
    get_radio_vaps(curr_vaps);

    AmbiorixVariant empty_vap;
    bool detectNewVaps = false;
    std::vector<std::string> newEnabledVaps;
    bool wasActive   = has_enabled_vap();
    int nb_curr_vaps = curr_vaps.size();

    auto &saved_vaps = m_radio_info.available_vaps;
    // iterate on saved_vaps and try to update with info from curr_vaps;

    std::vector<int> empty_slots;
    int max_slot = -1;
    // extract at runtime max(saved_vaps.keys()); instead of computing it

    auto handle_vap = [&](int vap_id, const bwl::VAPElement &vap) {
        bool wasEnabled = check_enabled_vap(vap.bss);
        auto updatedVAP = curr_vaps.find(vap.mac);
        if (updatedVAP != curr_vaps.end()) {
            LOG(INFO) << "update vap mac " << updatedVAP->first << ", insert in " << vap_id;
            ret |= refresh_vap_info(vap_id, updatedVAP->second);
            curr_vaps.erase(vap.mac);
        } else {
            LOG(INFO) << "reset vap_id " << vap_id;
            ret |= refresh_vap_info(vap_id, empty_vap);
            empty_slots.push_back(vap_id);
            // slot of vap_id was freed by refresh_vap_info(empty_vap);
        }
        bool isKnown   = (saved_vaps.find(vap_id) != saved_vaps.end());
        bool isEnabled = isKnown && check_enabled_vap(saved_vaps[vap_id].bss);
        detectNewVaps |= !wasActive && isEnabled;
        //!wasActive : no vaps enabled on this radio previously

        if (!wasEnabled && isEnabled) {
            LOG(INFO) << "wasEnabled :" << wasEnabled << " isEnabled:" << isEnabled;
            newEnabledVaps.push_back(saved_vaps[vap_id].bss);
        }
        max_slot = std::max(max_slot, vap_id);
    };

    if (id != IFACE_RADIO_ID) {
        auto v = saved_vaps.find(id);
        if (v != saved_vaps.end()) {
            handle_vap(id, v->second);
            empty_slots.clear();
        } else {
            LOG(ERROR) << "can't find vap_id " << id;
        }
    } else {
        // Copy to safely iterate while allowing modifications in the original map
        auto saved_vaps_copy = saved_vaps;
        for (const auto &vap : saved_vaps_copy) {
            if (!vap.second.bss.empty()) {
                handle_vap(vap.first, vap.second);
            }
        }
        for (int e = int(beerocks::IFACE_VAP_ID_MAX) - 1; e > (max_slot); e--) {
            empty_slots.push_back(e);
        }
    }

    while (!curr_vaps.empty() && !empty_slots.empty()) {
        int slot = empty_slots.back();
        LOG(INFO) << "insert new_vap with mac " << curr_vaps.begin()->first << " in slot " << slot;

        ret |= refresh_vap_info(slot, curr_vaps.begin()->second);
        if ((saved_vaps.find(slot) != saved_vaps.end()) &&
            check_enabled_vap(saved_vaps[slot].bss)) {
            newEnabledVaps.push_back(saved_vaps[slot].bss);
        }
        empty_slots.pop_back();
        curr_vaps.erase(curr_vaps.begin()->first);
        detectNewVaps |= true;
    }

    if (detectNewVaps) {
        process_radio_event(get_iface_name(), "AccessPointNumberOfEntries",
                            AmbiorixVariant::copy(nb_curr_vaps).get());
    }
    if (!newEnabledVaps.empty()) {
        auto status = AmbiorixVariant::copy("Enabled");
        for (const auto &bss : newEnabledVaps) {
            process_ap_event(bss, "Status", status.get());
        }
    }
    return ret;
}

bool base_wlan_hal_whm::refresh_vap_info(int id, const AmbiorixVariant &ap_obj)
{
    VAPElement vap_element;
    VAPExtInfo vap_extInfo;

    auto wifi_ssid_path = wbapi_utils::get_path_ssid_reference(ap_obj);
    auto ifname         = wbapi_utils::get_ap_iface(ap_obj);

    LOG(INFO) << "refresh_vap_info " << id << " path " << wifi_ssid_path;
    if (!wifi_ssid_path.empty() && !ifname.empty() &&
        !wbapi_utils::get_path_radio_reference(ap_obj).empty()) {
        std::string mac;
        auto ssid_obj = m_ambiorix_cl.get_object(wifi_ssid_path);
        if (ssid_obj && ((mac = wbapi_utils::get_ssid_mac(*ssid_obj)) != "") &&
            (mac != beerocks::net::network_utils::ZERO_MAC_STRING)) {
            vap_element.bss = ifname;
            vap_element.mac = mac;
            ssid_obj->read_child(vap_element.ssid, "SSID");
            // This is to be aligned with NL80211 backend implementation, if ACCESSPOINT is disabled, SSID shall be null
            // in practice, setting SSID to null is not accepted by whm/hostapd.
            bool ap_enabled(false);
            ap_obj.read_child(ap_enabled, "Enable");
            if (ap_enabled == false) {
                vap_element.ssid.clear();
            }
            vap_element.fronthaul = false;
            vap_element.backhaul  = false;
            std::string multi_ap_type;
            if (ap_obj.read_child(multi_ap_type, "MultiAPType")) {
                if (multi_ap_type.find("FronthaulBSS") != std::string::npos) {
                    vap_element.fronthaul = true;
                }
                if (multi_ap_type.find("BackhaulBSS") != std::string::npos) {
                    vap_element.backhaul = true;
                }
            }
            m_ambiorix_cl.resolve_path(wbapi_utils::search_path_ap_by_iface(ifname),
                                       vap_extInfo.path);
            m_ambiorix_cl.resolve_path(wifi_ssid_path, vap_extInfo.ssid_path);
            vap_extInfo.status = wbapi_utils::get_ap_status(ap_obj);
            LOG(INFO) << "status for " << ifname << " " << vap_extInfo.status;
        }
    }

    // VAP does not exists
    if (vap_element.mac.empty()) {
        if (m_radio_info.available_vaps.find(id) != m_radio_info.available_vaps.end()) {
            LOG(WARNING) << "Removed VAP " << m_radio_info.available_vaps[id].bss << " id (" << id
                         << ") ";
            m_vapsExtInfo.erase(m_radio_info.available_vaps[id].bss);
            m_radio_info.available_vaps.erase(id);
        }
        return true;
    }

    // Store the VAP element
    LOG(WARNING) << "Detected VAP id (" << id << ") - MAC: " << vap_element.mac
                 << ", SSID: " << vap_element.ssid << ", BSS: " << vap_element.bss;

    auto &mapped_vap_element = m_radio_info.available_vaps[id];
    auto &mapped_vap_extInfo = m_vapsExtInfo[vap_element.bss];
    if (mapped_vap_element.bss.empty()) {
        LOG(WARNING) << "BSS " << vap_element.bss << " is not preconfigured!"
                     << "Overriding VAP element.";

        mapped_vap_element = vap_element;
        mapped_vap_extInfo = vap_extInfo;
        return true;

    } else if (mapped_vap_element.bss != vap_element.bss) {
        LOG(ERROR) << "bss mismatch! vap_element.bss=" << vap_element.bss
                   << ", mapped_vap_element.bss=" << mapped_vap_element.bss;
        return false;
    } else if (mapped_vap_element.ssid != vap_element.ssid) {
        LOG(DEBUG) << "SSID changed from " << mapped_vap_element.ssid << ", to " << vap_element.ssid
                   << ". Overriding VAP element.";
        mapped_vap_element = vap_element;
        mapped_vap_extInfo = vap_extInfo;
        return true;
    }

    mapped_vap_element.mac    = vap_element.mac;
    mapped_vap_extInfo.status = vap_extInfo.status;

    return true;
}

bool base_wlan_hal_whm::process_ext_events(int fd)
{
    if (m_ambiorix_cl.get_fd() == fd) {
        m_ambiorix_cl.read();
    } else if (m_ambiorix_cl.get_signal_fd() == fd) {
        m_ambiorix_cl.read_signal();
    }
    return true;
}

int base_wlan_hal_whm::whm_get_vap_id(const std::string &iface)
{
    LOG(INFO) << "whm_get_vap_id " << iface;
    AmbiorixVariantMap aps;
    if (get_radio_vaps(aps) && !aps.empty()) {
        int vap_id = beerocks::IFACE_VAP_ID_MIN;
        for (const auto &ap : aps) {
            if (wbapi_utils::get_ap_iface(ap.second) == iface) {
                return vap_id;
            }
            vap_id++;
        }
    }
    return int(beerocks::IFACE_ID_INVALID);
}

bool base_wlan_hal_whm::whm_get_radio_ref(const std::string &iface, std::string &ref)
{
    ref          = "";
    auto ap_path = wbapi_utils::search_path_ap_by_iface(iface);
    if (!m_ambiorix_cl.get_param(ref, ap_path, "RadioReference")) {
        LOG(ERROR) << "failed to get RadioReference of ap iface " << iface;
        return false;
    }
    if (ref.empty()) {
        LOG(ERROR) << "No radioReference for iface " << iface;
        return false;
    }
    return true;
}

bool base_wlan_hal_whm::whm_get_radio_path(const std::string &iface, std::string &path)
{
    return m_ambiorix_cl.resolve_path(wbapi_utils::search_path_radio_by_iface(iface), path);
}

std::string base_wlan_hal_whm::get_radio_mac()
{
    if (m_radio_mac_address.empty()) {
        if (!beerocks::net::network_utils::linux_iface_get_mac(m_radio_info.iface_name,
                                                               m_radio_mac_address)) {
            LOG(ERROR) << "Failed to get radio mac from ifname " << m_radio_info.iface_name;
        }
    }
    return m_radio_mac_address;
}

sMacAddr base_wlan_hal_whm::get_bsta_mld_mac()
{
    LOG(WARNING) << "not implemented";
    return beerocks::net::network_utils::ZERO_MAC;
}

sMacAddr base_wlan_hal_whm::get_ap_mld_mac()
{
    LOG(WARNING) << "not implemented";
    return beerocks::net::network_utils::ZERO_MAC;
}

bool base_wlan_hal_whm::get_channel_utilization(uint8_t &channel_utilization)
{
    uint16_t chLoad;
    m_ambiorix_cl.get_param(chLoad, m_radio_path, "ChannelLoad");

    //convert channel load from ratio 100 to ratio 255
    channel_utilization = (chLoad * UINT8_MAX) / 100;
    return true;
}

void base_wlan_hal_whm::subscribe_to_scan_complete_events()
{

    auto event_handler        = std::make_shared<sAmbiorixEventHandler>();
    event_handler->event_type = AMX_CL_SCAN_COMPLETE_EVT;

    event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        std::string notif_name;
        if (!event_data.read_child(notif_name, "notification") || notif_name.empty()) {
            LOG(DEBUG) << " Received Notification  without 'notification' param!";
            return;
        }
        if (notif_name != AMX_CL_SCAN_COMPLETE_EVT) {
            LOG(DEBUG) << " Received wrong Notification : " << notif_name
                       << " instead of: " << AMX_CL_SCAN_COMPLETE_EVT;
            return;
        }

        std::string result;
        if (!event_data.read_child(result, "Result")) {
            LOG(ERROR) << " received ScanComplete event without Result param!";
            return;
        }

        process_scan_complete_event(result);
    };

    std::string filter = "(path matches '" + m_radio_path +
                         "$')"
                         " && (notification == '" +
                         AMX_CL_SCAN_COMPLETE_EVT + "')";

    m_ambiorix_cl.subscribe_to_object_event(m_radio_path, event_handler, filter);
}

void base_wlan_hal_whm::populate_channels_max_tx_power()
{

    if (m_radio_path.empty()) {
        m_ambiorix_cl.resolve_path(wbapi_utils::search_path_radio_by_iface(m_radio_info.iface_name),
                                   m_radio_path);
    }
    auto radio = m_ambiorix_cl.get_object(m_radio_path);
    if (!radio) {
        LOG(ERROR) << " cannot fill channels max tx power, radio object missing ";
        return;
    }

    std::string s_val;
    if (radio->read_child(s_val, "PossibleChannels")) {
        auto channels_vec = beerocks::string_utils::str_split(s_val, ',');
        for (auto &chan_str : channels_vec) {
            uint16_t chanNum = beerocks::string_utils::stoi(chan_str);
            AmbiorixVariant result;
            AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
            args.add_child("channel", chanNum);
            if (m_ambiorix_cl.call(m_radio_path, "getMaxTransmitPowerdBm", args, result)) {
                auto &channel_info   = m_radio_info.channels_list[chanNum];
                auto results_as_list = result.read_children<AmbiorixVariantListSmartPtr>();
                if (!results_as_list) {
                    LOG(ERROR) << "failed reading results!";
                }
                if (!(*results_as_list)[0].get(channel_info.tx_power_dbm)) {
                    LOG(ERROR) << "failed getting results!";
                }
            }
        }
    }
}

} // namespace whm
} // namespace bwl
