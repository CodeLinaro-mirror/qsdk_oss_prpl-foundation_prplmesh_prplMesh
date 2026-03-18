/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "ap_wlan_hal_whm.h"

#include <algorithm>
#include <bcl/beerocks_defines.h>
#include <bcl/beerocks_os_utils.h>
#include <bcl/beerocks_string_utils.h>
#include <bcl/beerocks_utils.h>
#include <bcl/network/network_utils.h>
#include <bcl/son/son_assoc_frame_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <easylogging++.h>
#include <math.h>
#include <numeric>
#include <sstream>
#include <vector>
using namespace beerocks;
using namespace wbapi;

//////////////////////////////////////////////////////////////////////////////
////////////////////////// Local Module Definitions //////////////////////////
//////////////////////////////////////////////////////////////////////////////

namespace bwl {
namespace whm {

// Status & Reason codes used in Failed Connection message
constexpr char CODE_OK[]                     = "0";
constexpr char CODE_UNSPECIFIED[]            = "1";
constexpr char CODE_INSUFFICIENT_BANDWIDTH[] = "33";
//////////////////////////////////////////////////////////////////////////////
/////////////////////////// Local Module Functions ///////////////////////////
//////////////////////////////////////////////////////////////////////////////

static ap_wlan_hal::Event wpaCtrl_to_bwl_event(const std::string &opcode)
{
    if (opcode == "DFS-CAC-START") {
        return ap_wlan_hal::Event::DFS_CAC_Started;
    } else if (opcode == "DFS-CAC-COMPLETED") {
        return ap_wlan_hal::Event::DFS_CAC_Completed;
    } else if (opcode == "DFS-NOP-FINISHED") {
        return ap_wlan_hal::Event::DFS_NOP_Finished;
    } else if (opcode == "AP-CSA-FINISHED") {
        return ap_wlan_hal::Event::CSA_Finished;
    } else if (opcode == "CTRL-EVENT-EAP-FAILURE") {
        return ap_wlan_hal::Event::WPA_Event_EAP_Failure;
    } else if (opcode == "CTRL-EVENT-EAP-FAILURE2") {
        return ap_wlan_hal::Event::WPA_Event_EAP_Failure2;
    } else if (opcode == "CTRL-EVENT-EAP-TIMEOUT-FAILURE") {
        return ap_wlan_hal::Event::WPA_Event_EAP_Timeout_Failure;
    } else if (opcode == "CTRL-EVENT-EAP-TIMEOUT-FAILURE2") {
        return ap_wlan_hal::Event::WPA_Event_EAP_Timeout_Failure2;
    } else if (opcode == "CTRL-EVENT-SAE-UNKNOWN-PASSWORD-IDENTIFIER") {
        return ap_wlan_hal::Event::WPA_Event_SAE_Unknown_Password_Identifier;
    } else if (opcode == "AP-STA-POSSIBLE-PSK-MISMATCH") {
        return ap_wlan_hal::Event::AP_Sta_Possible_Psk_Mismatch;
    } else if (opcode == "ACL-DENY") {
        return ap_wlan_hal::Event::ACL_DENY;
    }

    return ap_wlan_hal::Event::Invalid;
}

static uint8_t wpaCtrl_bw_to_beerocks_bw(const uint8_t width)
{
    std::map<uint8_t, beerocks::eWiFiBandwidth> bandwidths{
        {0 /*CHAN_WIDTH_20_NOHT*/, beerocks::BANDWIDTH_20},
        {1 /*CHAN_WIDTH_20     */, beerocks::BANDWIDTH_20},
        {2 /*CHAN_WIDTH_40     */, beerocks::BANDWIDTH_40},
        {3 /*CHAN_WIDTH_80     */, beerocks::BANDWIDTH_80},
        {4 /*CHAN_WIDTH_80P80  */, beerocks::BANDWIDTH_80_80},
        {5 /*CHAN_WIDTH_160    */, beerocks::BANDWIDTH_160},
        {6 /*CHAN_WIDTH_320    */, beerocks::BANDWIDTH_320},
    };

    auto it = bandwidths.find(width);
    if (it == bandwidths.end()) {
        LOG(ERROR) << "Invalid bandwidth value: " << width;
        return beerocks::BANDWIDTH_UNKNOWN;
    }

    return it->second;
}

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Implementation ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// NOTE: Since *base_wlan_hal_whm* inherits *base_wlan_hal* virtually, we
//       need to explicitly call it's from any deriving class
ap_wlan_hal_whm::ap_wlan_hal_whm(const std::string &iface_name, hal_event_cb_t callback,
                                 const hal_conf_t &hal_conf)
    : base_wlan_hal(bwl::HALType::AccessPoint, iface_name, IfaceType::Intel, callback, hal_conf),
      base_wlan_hal_whm(bwl::HALType::AccessPoint, iface_name, callback, hal_conf)
{
    int amx_fd = m_ambiorix_cl.get_fd();
    LOG_IF((amx_fd == -1), FATAL) << "Failed to get amx  fd";
    int amxp_fd = m_ambiorix_cl.get_signal_fd();
    LOG_IF((amxp_fd == -1), FATAL) << "Failed to get amx signal fd";

    m_fds_ext_events = {amx_fd, amxp_fd};
    subscribe_to_radio_events();
    subscribe_to_radio_channel_change_events();
    subscribe_to_ap_events();
    subscribe_to_sta_events();
    subscribe_to_ap_bss_tm_events();
    subscribe_to_ap_mgmt_frame_events();
    subscribe_to_afc_update_events();
}

ap_wlan_hal_whm::~ap_wlan_hal_whm() {}

HALState ap_wlan_hal_whm::attach(bool block)
{
    auto state = base_wlan_hal_whm::attach(block);

    // On Operational send the AP_Attached event to the AP Manager
    if (state == HALState::Operational) {
        event_queue_push(Event::AP_Attached);
    }

    return state;
}

void ap_wlan_hal_whm::subscribe_to_ap_bss_tm_events()
{
    auto event_handler        = std::make_shared<sAmbiorixEventHandler>();
    event_handler->event_type = AMX_CL_BSS_TM_RESPONSE_EVT;

    event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
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

        process_ap_bss_event(vap_it->first, &event_data);
    };

    std::string filter = "(path matches '" + wbapi_utils::search_path_ap() +
                         "[0-9]+.$')"
                         " && (notification == '" +
                         AMX_CL_BSS_TM_RESPONSE_EVT + "')";

    m_ambiorix_cl.subscribe_to_object_event(wbapi_utils::search_path_ap(), event_handler, filter);
}

void ap_wlan_hal_whm::subscribe_to_ap_mgmt_frame_events()
{
    auto event_handler         = std::make_shared<sAmbiorixEventHandler>();
    event_handler->event_type  = AMX_CL_MGMT_ACT_FRAME_EVT;
    event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
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

        process_ap_bss_event(vap_it->first, &event_data);
    };

    std::string filter = "(path matches '" + wbapi_utils::search_path_ap() +
                         "[0-9]+.$')"
                         " && (notification == '" +
                         AMX_CL_MGMT_ACT_FRAME_EVT + "')";

    m_ambiorix_cl.subscribe_to_object_event(wbapi_utils::search_path_ap(), event_handler, filter);
}

bool ap_wlan_hal_whm::enable()
{
    if (m_radio_path.empty()) {
        m_ambiorix_cl.resolve_path(wbapi_utils::search_path_radio_by_iface(m_radio_info.iface_name),
                                   m_radio_path);
    }

    AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
    new_obj.add_child("Enable", true);
    if (!m_ambiorix_cl.update_object(m_radio_path, new_obj)) {
        return false;
    }

    return true;
}

bool ap_wlan_hal_whm::disable()
{
    if (m_radio_path.empty()) {
        m_ambiorix_cl.resolve_path(wbapi_utils::search_path_radio_by_iface(m_radio_info.iface_name),
                                   m_radio_path);
    }

    AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
    new_obj.add_child("Enable", false);
    if (!m_ambiorix_cl.update_object(m_radio_path, new_obj)) {
        return false;
    }

    return true;
}

bool ap_wlan_hal_whm::set_channel(int chan, beerocks::eWiFiBandwidth bw, int center_channel)
{
    bool auto_channel_enable = false;
    m_ambiorix_cl.get_param(auto_channel_enable, m_radio_path, "AutoChannelEnable");
    if (auto_channel_enable) {
        LOG(INFO) << "Don't need to set channel!, AutoChannelEnable mode was already set by pWHM";
        return true;
    }

    AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
    new_obj.add_child("Channel", uint8_t(chan));
    bool ret = m_ambiorix_cl.update_object(m_radio_path, new_obj);

    if (chan == 0) {
        LOG(INFO) << "return true for channel:0";
        return true;
        // ap_manager sometimes writes 0 value as part of resetting the radio
        // it expects a return true in this case, so give it that
    }

    if (!ret) {
        LOG(ERROR) << "unable to set channel! ch: " << chan << " center chan : " << center_channel;
        return false;
    }

    return true;
}

bool ap_wlan_hal_whm::sta_allow(const sMacAddr &mac, const sMacAddr &bssid)
{
    auto vap_id = get_vap_id_with_mac(tlvf::mac_to_string(bssid));
    if (vap_id < 0) {
        LOG(ERROR) << "no vap has bssid " << bssid;
        return false;
    }

    std::string ifname          = m_radio_info.available_vaps[vap_id].bss;
    std::string mac_filter_path = wbapi_utils::search_path_mac_filtering(ifname);

    std::string mode;
    if (!m_ambiorix_cl.get_param(mode, mac_filter_path, "Mode")) {
        LOG(ERROR) << "failed to get MACFiltering object";
        return false;
    }

    // check if the sta is included in accesslist entries
    std::string entry_path =
        wbapi_utils::search_path_mac_filtering_entry_by_mac(ifname, tlvf::mac_to_string(mac));
    bool sta_found = m_ambiorix_cl.resolve_path(entry_path, entry_path);

    if (sta_found && mode == "WhiteList") {
        LOG(TRACE) << "sta allowed in WhiteList mode";
        return true;
    }
    if (!sta_found && mode == "BlackList") {
        LOG(TRACE) << "sta allowed in BlackList mode";
        return true;
    }

    // delete sta from the BlackList
    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    args.add_child("mac", tlvf::mac_to_string(mac));
    bool ret = true;
    if (mode == "WhiteList") {
        ret = m_ambiorix_cl.call(mac_filter_path, "addEntry", args, result);
    } else if (mode == "BlackList") {
        ret = m_ambiorix_cl.call(mac_filter_path, "delEntry", args, result);
    }

    if (!ret) {
        LOG(ERROR) << "MACFiltering update entry failed!";
        return false;
    }
    LOG(TRACE) << "sta updated in accessList, sta allowed";
    return true;
}

bool ap_wlan_hal_whm::sta_deny(const sMacAddr &mac, const sMacAddr &bssid)
{
    auto vap_id = get_vap_id_with_mac(tlvf::mac_to_string(bssid));
    if (vap_id < 0) {
        LOG(ERROR) << "no vap has bssid " << bssid;
        return false;
    }

    std::string ifname          = m_radio_info.available_vaps[vap_id].bss;
    std::string mac_filter_path = wbapi_utils::search_path_mac_filtering(ifname);

    std::string mode;
    if (!m_ambiorix_cl.get_param(mode, mac_filter_path, "Mode")) {
        LOG(ERROR) << "failed to get MACFiltering object";
        return false;
    }

    // check if the sta is included in accesslist entries
    std::string entry_path =
        wbapi_utils::search_path_mac_filtering_entry_by_mac(ifname, tlvf::mac_to_string(mac));
    bool sta_found = m_ambiorix_cl.resolve_path(entry_path, entry_path);

    if (sta_found && mode == "BlackList") {
        LOG(TRACE) << "sta denied in BlackList mode";
        return true;
    }
    if (!sta_found && mode == "WhiteList") {
        LOG(TRACE) << "sta denied in WhiteList mode";
        return true;
    }

    bool ret = true;
    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    args.add_child("mac", tlvf::mac_to_string(mac));
    if (mode != "Blacklist" && mode != "WhiteList") {
        LOG(WARNING) << "change MACFiltering mode to BlackList";
        AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
        new_obj.add_child("Mode", "BlackList");
        ret = m_ambiorix_cl.update_object(mac_filter_path, new_obj);

        if (!ret) {
            LOG(ERROR) << "unable to change MACFiltering mode to BlackList!";
        } else {
            mode = "BlackList";
        }
    }
    if (!sta_found && mode == "BlackList") {
        ret = m_ambiorix_cl.call(mac_filter_path, "addEntry", args, result);
    } else if (sta_found && mode == "WhiteList") {
        ret = m_ambiorix_cl.call(mac_filter_path, "delEntry", args, result);
    }

    if (!ret) {
        LOG(ERROR) << "MACFiltering update entry failed!";
        return false;
    }
    return true;
}

bool ap_wlan_hal_whm::clear_blacklist()
{
    bool ret = true;
    for (const auto &vap_info : m_radio_info.available_vaps) {
        LOG(DEBUG) << "Turning off MACFiltering Mode for vap: " << vap_info.second.bss;
        const std::string mac_filter_path =
            wbapi_utils::search_path_mac_filtering(vap_info.second.bss);

        AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
        new_obj.add_child("Mode", "Off");
        ret = m_ambiorix_cl.update_object(mac_filter_path, new_obj);

        // Clear Entries
        auto entries = m_ambiorix_cl.get_object_multi<AmbiorixVariantMapSmartPtr>(
            wbapi_utils::search_path_mac_filtering_entries(vap_info.second.bss));
        if (entries) {
            int entry_index{};
            for (auto const &it : *entries) {
                auto &entry = it.second;
                entry_index++;

                // Getting STA MACAddress
                std::string sta_mac{};
                entry.read_child(sta_mac, "MACAddress");
                if (sta_mac.empty()) {
                    LOG(ERROR) << "clear_blacklist: failed to get MACAddress from Entry #"
                               << entry_index;
                    continue;
                }

                LOG(TRACE) << "clear_blacklist: deleting Entry #" << entry_index
                           << " MACAddress: " << sta_mac;
                AmbiorixVariant args(AMXC_VAR_ID_HTABLE), result;
                args.add_child("mac", sta_mac);
                if (m_ambiorix_cl.call(mac_filter_path, "delEntry", args, result)) {
                    LOG(ERROR) << "clear_blacklist: delEntry failed!";
                }
            }
        }
    }

    if (!ret) {
        LOG(ERROR) << "clear_blacklist failed!";
        return false;
    }
    return true;
}

bool ap_wlan_hal_whm::sta_acceptlist_modify(const sMacAddr &mac, const sMacAddr &bssid,
                                            bwl::sta_acl_action action)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::set_macacl_type(const eMacACLType &acl_type, const sMacAddr &bssid)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::sta_disassoc(int8_t vap_id, const std::string &mac, uint32_t reason)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::sta_deauth(int8_t vap_id, const std::string &mac, uint32_t reason)
{
    if (!check_vap_id(vap_id)) {
        LOG(ERROR) << "invalid vap_id " << vap_id;
        return false;
    }
    std::string ifname = m_radio_info.available_vaps[vap_id].bss;
    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    args.add_child("macaddress", mac);
    args.add_child("reason", reason);
    std::string wifi_ap_path = wbapi_utils::search_path_ap_by_iface(ifname);
    bool ret                 = m_ambiorix_cl.call(wifi_ap_path, "kickStationReason", args, result);

    if (!ret) {
        LOG(ERROR) << "sta_deauth() failed!";
        return false;
    }
    return true;
}

bool ap_wlan_hal_whm::sta_bss_steer(const sBtmRequestParams &params)
{
    if (!check_vap_id(params.vap_id)) {
        LOG(ERROR) << "invalid vap_id " << params.vap_id;
        return false;
    }
    std::string ifname = m_radio_info.available_vaps[params.vap_id].bss;

    // Matches the bit layout expected by pwhm: pref->bit0, abr->bit1, disassoc->bit2
    auto build_req_mode = [](bool pref, bool abr, bool disassoc) {
        return uint8_t((pref << 0) | (abr << 1) | (disassoc << 2));
    };

    uint8_t req_mode =
        build_req_mode(params.pref_list_included, params.abridged, params.disassoc_imminent);

    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    args.add_child("mac", tlvf::mac_to_string(params.mac));
    args.add_child("target", tlvf::mac_to_string(params.bssid));
    args.add_child("class", params.oper_class);
    args.add_child("channel", params.chan);
    args.add_child("validity", params.valid_int_btt);
    args.add_child("disassoc", params.disassoc_timer_btt);
    args.add_child("transitionReason", params.reason);
    args.add_child("mode", req_mode);
    auto wifi_ap_path = wbapi_utils::search_path_ap_by_iface(ifname);
    bool ret          = m_ambiorix_cl.call(wifi_ap_path, "sendBssTransferRequest", args, result);

    if (!ret) {
        LOG(ERROR) << "sta_bss_steer() failed!";
        return false;
    }
    return true;
}

bool ap_wlan_hal_whm::update_vap_credentials(
    std::list<son::wireless_utils::sBssInfoConf> &bss_info_conf_list,
    const std::string &backhaul_wps_ssid, const std::string &backhaul_wps_passphrase,
    const std::string &bridge_ifname)
{
    LOG(DEBUG) << "updating vap credentials of radio " << get_iface_name()
               << " and bridge=" << bridge_ifname;
    bool ret          = false;
    int new_vap_index = m_radio_info.available_vaps.size();

    for (const auto &bss_info_conf : bss_info_conf_list) {
        std::string wifi_vap_path, wifi_ssid_path;
        std::string ifname = "new_interface";

        const auto bssid = tlvf::mac_to_string(bss_info_conf.bssid);
        int vap_id       = get_vap_id_with_mac(bssid);

        if (!check_vap_id(vap_id) || (bssid == beerocks::net::network_utils::WILD_MAC_STRING)) {
            LOG(DEBUG) << "create new vap for wildcard bssid";

            auto freq_name = wbapi_utils::band_short_name(m_radio_info.frequency_band);

            std::string new_vap_name = "vap" + freq_name + std::to_string(new_vap_index++);

            std::string radio_name;
            if (!m_ambiorix_cl.get_param(radio_name, m_radio_path, "Alias")) {
                LOG(ERROR) << "cannot read radio name for " << m_radio_path;
                continue;
            }

            LOG(INFO) << "calling addVAPIntf with radio name " << radio_name << " vap name "
                      << new_vap_name;

            AmbiorixVariant result;
            AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
            args.add_child("vap", new_vap_name);
            args.add_child("radio", radio_name);

            m_ambiorix_cl.call("Device.WiFi.", "addVAPIntf", args, result);

            // ex of call: Device.WiFi.addVAPIntf(vap="new5g10", radio="radio2")
            // use the parameter 'vap', "new5g10", as Alias to retrieve the new
            // SSID instance; from there, retrieve AccessPoint by SSIDReference;

            std::string search_path = wbapi_utils::search_path_ssid_by_alias(new_vap_name);
            if (!m_ambiorix_cl.resolve_path(search_path, wifi_ssid_path)) {
                LOG(ERROR) << "new SSID not found";
                continue;
            }

            if (!get_accesspoint_by_ssid(wifi_ssid_path, wifi_vap_path)) {
                LOG(ERROR) << "new AccessPoint not found";
                continue;
            }

            LOG(INFO) << "added new instances " << wifi_ssid_path << " " << wifi_vap_path;

            args.set_type(AMXC_VAR_ID_HTABLE);
            args.add_child("BridgeInterface", bridge_ifname);
            args.add_child("IEEE80211kEnabled", 1);
            args.add_child("WDSEnable", 1);
            if (!m_ambiorix_cl.update_object(wifi_vap_path, args)) {
                LOG(INFO) << "cannot set bridge, 11k and wds for " << wifi_vap_path;
                //continue;
                // no continue here since these parameters are supposed to be handled
                // by other EasyMesh messages : ex bridging - traffic separation policy;
                // or not handled by EasyMesh : ex WDSEnable;
            }
        } else {
            auto &vap_info = m_radio_info.available_vaps[vap_id];
            ifname         = vap_info.bss;
            auto vap_it    = m_vapsExtInfo.find(ifname);
            if (vap_it == m_vapsExtInfo.end()) {
                LOG(ERROR) << "fail to get ifname of " << bssid;
                continue;
            }
            wifi_vap_path  = vap_it->second.path;
            wifi_ssid_path = vap_it->second.ssid_path;

            LOG(DEBUG) << "updating AP " << wifi_vap_path << " SSID " << wifi_ssid_path
                       << " ifname " << ifname << " vap_id " << std::to_string(vap_id);
        }
        /* here we need to know the :
        * wifi_vap_path
        * wifi_ssid_path
        * */

        AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
        if (bss_info_conf.teardown) {
            // Re-check validity right before use; VAP may have been removed meanwhile.
            if (!check_vap_id(vap_id)) {
                LOG(WARNING) << "teardown requested but vap_id invalid for bssid " << bssid
                             << " - skipping";
                continue;
            }
            auto &vap_info = m_radio_info.available_vaps[vap_id];
            ifname         = vap_info.bss;
            auto vap_it    = m_vapsExtInfo.find(ifname);
            if (vap_it == m_vapsExtInfo.end()) {
                LOG(WARNING) << "teardown requested but VAP ext info missing for ifname " << ifname
                             << " - skipping";
                continue;
            }
            vap_it->second.teardown = true;

            LOG(INFO) << "BSS " << bss_info_conf.bssid << " flagged for tear down.";
            new_obj.add_child<bool>("Enable", false);
            ret = m_ambiorix_cl.update_object(wifi_vap_path, new_obj);
            if (!ret) {
                LOG(ERROR) << "Failed to disable vap " << ifname;
            }
            continue;
        } else {
            LOG(DEBUG) << "enable vap " << wifi_vap_path;
            new_obj.add_child("Enable", true);
            std::string multi_ap;
            if (bss_info_conf.fronthaul) {
                multi_ap += "FronthaulBSS";
            }
            if (bss_info_conf.backhaul) {
                if (!multi_ap.empty()) {
                    multi_ap += ",";
                }
                multi_ap += "BackhaulBSS";
            }
            LOG(DEBUG) << "set multiaptype " << multi_ap;
            new_obj.add_child("MultiAPType", multi_ap);

            // MultiAPProfile is added here for all VAP types, not only for backhaul ones.
            // While the Multi-AP specification primarily associates profile information
            // with backhaul BSSs, there are corner cases where the Agent or HAL may not yet
            // know the exact MultiAPType when this object is created (e.g. during early
            // VAP setup or mixed-role configurations).
            // If someone is absolutely certain that the MultiAPType is always known at this
            // stage, the "MultiAPProfile" assignment could be moved inside the
            // `if (bss_info_conf.backhaul)` block below.
            LOG(DEBUG) << "set multiapprofile " << (get_hal_conf().multi_ap_profile);
            new_obj.add_child("MultiAPProfile", get_hal_conf().multi_ap_profile);

            if (bss_info_conf.hidden_ssid == WSC::eWscVendorExtHiddenSsid::ENABLED) {
                new_obj.add_child("SSIDAdvertisementEnabled", 0);
            } else if (bss_info_conf.hidden_ssid == WSC::eWscVendorExtHiddenSsid::DISABLED) {
                new_obj.add_child("SSIDAdvertisementEnabled", 1);
            }

            LOG(INFO) << "Hidden SSID-Bss_info: " << bss_info_conf.ssid
                      << ", SSIDAdvertisementEnabled is " << bss_info_conf.hidden_ssid;
            ret = m_ambiorix_cl.update_object(wifi_vap_path, new_obj);
            if (!ret) {
                LOG(ERROR) << "Failed to enable vap " << wifi_vap_path
                           << " or to configure MultiAPType thereof " << multi_ap;
            }
        }

        auto auth_type = son::wireless_utils::wsc_to_bwl_authentication(
            bss_info_conf.authentication_type, bss_info_conf.additional_auth);
        if (auth_type == "INVALID") {
            LOG(ERROR) << "Autoconfiguration: invalid auth_type "
                       << int(bss_info_conf.authentication_type);
            continue;
        }
        std::string encryption_mode =
            wbapi_utils::encryption_type_to_string(bss_info_conf.encryption_type);
        if (encryption_mode == "Default") {
            LOG(WARNING) << "Autoconfiguration: unsupported enc_type "
                         << int(bss_info_conf.encryption_type) << ", using Default";
        }

        const int8_t mld_unit = bss_info_conf.mld_id.empty()
                                    ? DISABLED_MLDUNIT
                                    : beerocks::string_utils::stoi(bss_info_conf.mld_id);

        LOG(DEBUG) << "Autoconfiguration for ssid: " << bss_info_conf.ssid
                   << " auth_type: " << auth_type << " encr_type: " << encryption_mode
                   << " network_key: " << bss_info_conf.network_key
                   << " fronthaul: " << bss_info_conf.fronthaul
                   << " backhaul: " << bss_info_conf.backhaul
                   << " hidden_SSID: " << bss_info_conf.hidden_ssid
                   << " authentication_type: " << bss_info_conf.authentication_type
                   << " mld_unit: " << mld_unit;

        new_obj.set_type(AMXC_VAR_ID_HTABLE);
        new_obj.add_child("SSID", bss_info_conf.ssid);
        new_obj.add_child("MLDUnit", mld_unit);

        ret = m_ambiorix_cl.update_object(wifi_ssid_path, new_obj);

        if (!ret) {
            LOG(ERROR) << "Failed to update SSID object";
            continue;
        }

        std::string security_mode = wbapi_utils::security_mode_to_string(
            bss_info_conf.authentication_type, bss_info_conf.additional_auth);

        LOG(DEBUG) << "Security Mode:" << security_mode << " Encryption Mode:" << encryption_mode;

        std::string wifi_ap_sec_path = wifi_vap_path + "Security.";
        new_obj.set_type(AMXC_VAR_ID_HTABLE);
        new_obj.add_child("ModeEnabled", security_mode);
        if (security_mode == "None") {
            new_obj.add_child("EncryptionMode", "Default");
        } else {
            std::string current_encryption_mode;
            m_ambiorix_cl.get_param(current_encryption_mode, wifi_vap_path + "Security.",
                                    "EncryptionMode");
            if (current_encryption_mode != "Default") {
                // In Default pWHM handles automatically, leave it as is
                new_obj.add_child("EncryptionMode", encryption_mode);
            }

            new_obj.add_child("KeyPassPhrase", bss_info_conf.network_key);
            if (security_mode.find("WPA3") != std::string::npos) {
                new_obj.add_child("SAEPassphrase", bss_info_conf.network_key);
            }
        }
        ret = m_ambiorix_cl.update_object(wifi_ap_sec_path, new_obj);

        if (!ret) {
            LOG(ERROR) << "Failed to update Security object " << wifi_ap_sec_path;
            continue;
        }
        if (ifname == "new_interface") {
            // skip update of vap_info, new instance in vap_info will be added asynchronously on a pwhm event
            continue;
        }

        // From here we must have a stable VAP in our maps; re-validate again to be safe.
        if (!check_vap_id(vap_id)) {
            LOG(WARNING) << "vap_id invalidated before local map update for ifname " << ifname
                         << " - skipping local state update";
            continue;
        }
        auto &vap_info = m_radio_info.available_vaps[vap_id];

        ifname      = vap_info.bss;
        auto vap_it = m_vapsExtInfo.find(ifname);
        if (vap_it == m_vapsExtInfo.end()) {
            LOG(WARNING) << "VAP ext info missing for ifname " << ifname
                         << " - skipping local state update";
            continue;
        }

        bool &prev_teardown = vap_it->second.teardown;

        if (prev_teardown) {
            prev_teardown = false;
            LOG(INFO) << "Re-enable BSS " << bss_info_conf.bssid << " after tear down.";
            new_obj.set_type(AMXC_VAR_ID_HTABLE);
            new_obj.add_child<bool>("Enable", true);
            ret = m_ambiorix_cl.update_object(wifi_vap_path, new_obj);
            if (!ret) {
                LOG(ERROR) << "Failed to enable vap " << ifname;
                continue;
            }
        }

        vap_info.bss       = ifname;
        vap_info.mac       = bssid;
        vap_info.fronthaul = bss_info_conf.fronthaul;
        vap_info.backhaul  = bss_info_conf.backhaul;
        vap_info.mld_id    = mld_unit;
        if (vap_info.backhaul) {
            vap_info.ssid = backhaul_wps_ssid;
            vap_info.profile1_backhaul_sta_association_disallowed =
                bss_info_conf.profile1_backhaul_sta_association_disallowed;
            vap_info.profile2_backhaul_sta_association_disallowed =
                bss_info_conf.profile2_backhaul_sta_association_disallowed;
        } else {
            vap_info.ssid                                         = bss_info_conf.ssid;
            vap_info.profile1_backhaul_sta_association_disallowed = false;
            vap_info.profile2_backhaul_sta_association_disallowed = false;
        }

        // re-notify previously enabled vaps to unblock autoconf task
        auto status = m_ambiorix_cl.get_param(wifi_vap_path, "Status");
        if (status && !status->empty()) {
            process_ap_event(ifname, "Status", status.get());
        }
    }

    return true;
}

bool ap_wlan_hal_whm::sta_unassoc_rssi_measurement(const std::string &mac, int chan,
                                                   beerocks::eWiFiBandwidth bw,
                                                   int vht_center_frequency, int delay,
                                                   int window_size)
{

    if (m_unassociated_stations.empty()) {
        subscribe_to_rssi_eventing_events();
    }
    m_unassociated_stations.insert(mac);

    return true;
}

bool ap_wlan_hal_whm::sta_softblock_add(const std::string &vap_name, const std::string &client_mac,
                                        uint8_t reject_error_code, uint8_t probe_snr_threshold_hi,
                                        uint8_t probe_snr_threshold_lo,
                                        uint8_t authetication_snr_threshold_hi,
                                        uint8_t authetication_snr_threshold_lo)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::sta_softblock_remove(const std::string &vap_name,
                                           const std::string &client_mac)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::switch_channel(int chan, beerocks::eWiFiBandwidth bw,
                                     int vht_center_frequency, int csa_beacon_count)
{
    LOG(TRACE) << " channel: " << chan << ", bw enum: " << bw
               << " bw mhz: " << wbapi_utils::bandwidth_to_string(bw)
               << ", vht_center_frequency: " << vht_center_frequency;

    AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
    bool amx_ret, status = true;

    if (bw == beerocks::eWiFiBandwidth::BANDWIDTH_40) {

        auto freq_type = son::wireless_utils::which_freq_type(vht_center_frequency);
        int freq       = son::wireless_utils::channel_to_freq(chan, freq_type);

        // Extension Channel
        if (freq < vht_center_frequency) {
            new_obj.add_child("ExtensionChannel", "AboveControlChannel");
        } else {
            new_obj.add_child("ExtensionChannel", "BelowControlChannel");
        }
    }
    // WiFi.Radio.2.OperatingChannelBandwidth
    new_obj.add_child("OperatingChannelBandwidth", wbapi_utils::bandwidth_to_string(bw));

    new_obj.add_child("Channel", chan);
    amx_ret = m_ambiorix_cl.update_object(m_radio_path, new_obj);
    if (!amx_ret) {
        LOG(ERROR) << "can't apply ExtensionCh, BW and Channel for " << m_radio_path;
        status = false;
    }

    return status;
}

bool ap_wlan_hal_whm::cancel_cac(int chan, beerocks::eWiFiBandwidth bw, int vht_center_frequency,
                                 int secondary_chan)
{
    return set_channel(chan, bw, vht_center_frequency);
}

bool ap_wlan_hal_whm::failsafe_channel_set(int chan, int bw, int vht_center_frequency)
{
    // when DFS_OFFLOAD- is not set(our case for now), DFS management will be handled by Hostapd.
    // Thus no need to implement this function.
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::failsafe_channel_get(int &chan, int &bw)
{
    // Failsafe will be handled by hostapd, thus no need to implement this function.
    // Morover, this function is not being called for now.
    LOG(TRACE) << __func__ << "- NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::is_zwdfs_supported()
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}

bool ap_wlan_hal_whm::set_zwdfs_antenna(bool enable)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::is_zwdfs_antenna_enabled()
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}

bool ap_wlan_hal_whm::hybrid_mode_supported()
{
    // Hybrid mode is always supported to allow configuring fBss/bBss on profile 1
    return true;
}

bool ap_wlan_hal_whm::restricted_channels_set(char *channel_list)
{
    // We chose not to implement it because it is a custom feature and has no reference in the prplmesh Spec.
    return true;
}

bool ap_wlan_hal_whm::restricted_channels_get(char *channel_list)
{
    // We chose not to implement it because it is a custom feature and has no reference in the prplmesh Spec.
    return false;
}

int32_t
ap_wlan_hal_whm::get_rank_of_channel(uint8_t chanNum,
                                     std::vector<std::tuple<uint8_t, int32_t>> &chan_survey_report)
{
    for (const auto &entry : chan_survey_report) {
        if (std::get<0>(entry) == chanNum) {
            return std::get<1>(entry);
        }
    }
    return INT32_MAX; // if channel not found return rank as max int value so that it will assign least multiap preference
}

bool ap_wlan_hal_whm::update_rank_for_channel(
    std::vector<std::tuple<uint8_t, int32_t>> &chan_survey_report)
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

    for (auto &pair : m_radio_info.channels_list) {
        uint8_t channel_num = pair.first;
        sChannelInfo &info  = pair.second;

        for (auto &bw : info.bw_info_list) {

            if (bw.first > m_radio_info.max_bandwidth) {
                continue;
            }
            bw.second = get_rank_of_channel(channel_num, chan_survey_report);
            LOG(INFO) << "Channel: " << int(channel_num) << " Bandwidth: " << int(bw.first)
                      << " Rank: " << bw.second;
        }
    }
    return true;
}

bool ap_wlan_hal_whm::read_acs_report()
{
    std::vector<std::tuple<uint8_t, int32_t>> chan_survey_report;
    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    if (!m_ambiorix_cl.call(m_radio_path, "getChanSurveyReport", args, result)) {
        LOG(ERROR) << " remote function call getChanSurveyReport Failed!";
        return false;
    }
    AmbiorixVariantListSmartPtr acs_scan_results_list =
        result.read_children<AmbiorixVariantListSmartPtr>();
    if (!acs_scan_results_list) {
        LOG(ERROR) << "failed reading scan_results!";
        return false;
    }
    if (acs_scan_results_list->empty()) {
        LOG(ERROR) << "scan_results are empty";
        return false;
    }
    chan_survey_report.clear();
    auto results_as_wrapped_list = acs_scan_results_list->front();
    auto acs_interference_results_as_list =
        results_as_wrapped_list.read_children<AmbiorixVariantListSmartPtr>();
    for (auto &acs_interference_results_map : *acs_interference_results_as_list) {
        auto data_map = acs_interference_results_map.read_children<AmbiorixVariantMapSmartPtr>();
        auto &map     = *data_map;

        int channel;
        int32_t factor;
        if (map.find("channel") != map.end()) {
            map["channel"].get(channel);
        } else {
            LOG(DEBUG) << "channel is missing,skipping";
            continue;
        }
        if (map.find("interferenceFactor") != map.end()) {
            map["interferenceFactor"].get(factor);
        } else {
            LOG(DEBUG) << "InterferenceFactor is missing,skipping";
            continue;
        }
        chan_survey_report.emplace_back(channel, factor);
    }

    if (chan_survey_report.empty()) {
        LOG(ERROR) << "channel survey report is empty!";
        return false;
    }

    update_rank_for_channel(chan_survey_report);
    return true;
}

bool ap_wlan_hal_whm::set_tx_power_limit(int tx_pow_limit)
{

    if (m_radio_info.channels_list.find(m_radio_info.channel) == m_radio_info.channels_list.end()) {
        LOG(WARNING) << "Unknown maxTxPower for current channel";
    }

    /*
     * at this point we expect m_radio_info.channels_list[channel].tx_power_dbm
     * to contain a valid value, as well as the controller, to know it, and request a valid txpower level
     *
     * the value of tx power limit, in EasyMesh, is an absolute value expressed in dBm.
     * tr-181 specifies Radio.{i}.TransmitPower as a percentage of maximum transmit power of the radio.
     * and also as a value from the list TransmitPowerSupported
     * tr-181 gives the following example for TransmitPowerSupported : "0,25,50,75,100".
     * pwhm typically accepts 100, 50, 25, 12, 6, and -1 for automatic tx power selection.
     * since Automatic tx power selection is not part of EasyMesh, it is not covered here.
     */

    /*
     * dB scale is a logarithmic scale.
     * the division between the target txPower [mW] and maximal txPower [mW] is equivalent
     * to a substraction between these values in their logarithmic representation.
     * since the dBm values are integers, the ratios that they can represent are discreet.
     * pow_lvls_rel table is used to compute an initial relative value of txPower
     * substracting 1dBm is approximated to substracting 20% (multiplication by 0.8)
     * substracting 2dBm is approximated to substracting 40% (multiplication by 0.6)
     * substracting 3dBm is identical to substracting 50%    (multiplication by 0.5)
     */
    /*
     * regarding the trailing end:
     * 5% means (minus 13dBm)
     * 4%       (minus 14dBm)
     * 3%       (minus 15dBm)
     *    (minus 16dBm) is 2.5% - tr-181 Device.Radio.{i}.TransmitPower requires integers, so round down to 2
     * 2%       (minus 17dBm)
     * 1%       (minus 20dBm)
     * mapping both 16dBm and 17dBm deltas to 2% (17dBm delta):
     * 2 repeats in pow_lvls_rel because the index is also the dBm delta: 16 and 17 dBm delta both map to 2%
     * mapping anything above a 17dBm delta (lower than 2%) to 1%
     */

    auto max_pow_abs = m_radio_info.channels_list[m_radio_info.channel].tx_power_dbm;

    const std::vector<int8_t> pow_lvls_rel = {100, 80, 60, 50, 40, 30, 25, 20, 15, 12,
                                              10,  8,  6,  5,  4,  3,  2,  2,  1};

    uint8_t max_selector = pow_lvls_rel.size() - 1;
    uint8_t selector     = 0;

    while ((max_pow_abs > tx_pow_limit) && (selector < max_selector)) {
        max_pow_abs -= 1;
        selector++;
    }

    auto selected_tx_power = pow_lvls_rel[selector];
    LOG(DEBUG) << "max tx_power " << m_radio_info.channels_list[m_radio_info.channel].tx_power_dbm
               << "dBm; requested power " << tx_pow_limit << "dBm; relative tx_pow "
               << selected_tx_power << "%";

    /*
     * the initial relative value of txpower may or may not be supported by pwhm.
     * identify pwhm power level that is closest
     */
    std::string power_list_str;
    m_ambiorix_cl.get_param(power_list_str, m_radio_path, "TransmitPowerSupported");
    std::stringstream ss(power_list_str);
    std::vector<int> power_levels_pwhm;

    while (ss.good()) {
        std::string substr;
        getline(ss, substr, ',');
        if (stoi(substr) > 0) { // skip special value of -1
            power_levels_pwhm.push_back(stoi(substr));
        }
    }
    std::sort(power_levels_pwhm.begin(), power_levels_pwhm.end(), std::greater<int>());
    // sort in decreasing order

    for (const auto pwr : power_levels_pwhm) {
        if (pwr <= selected_tx_power) {
            selected_tx_power = pwr;
            break;
        }
    }
    LOG(DEBUG) << "Final power lvl " << selected_tx_power << "%";

    /**
     * apply new power level
     */
    AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
    new_obj.add_child("TransmitPower", int8_t(selected_tx_power));
    bool ret = m_ambiorix_cl.update_object(m_radio_path, new_obj);

    if (!ret) {
        LOG(ERROR) << "unable to set tx power limit for " << m_radio_path;
        return false;
    }

    return true;
}

bool ap_wlan_hal_whm::generate_connected_clients_events(
    bool &is_finished_all_clients, std::chrono::steady_clock::time_point max_iteration_timeout)
{
    // For the pwhm, we belive the time requirement will be maintained all time, thus we will ignore the max_iteration_timeout
    for (auto &vap : m_vapsExtInfo) {

        std::string vap_path                = vap.second.path;
        std::string associated_devices_path = vap_path + "AssociatedDevice.";

        auto associated_devices_pwhm =
            m_ambiorix_cl.get_object_multi<AmbiorixVariantMapSmartPtr>(associated_devices_path);

        if (associated_devices_pwhm == nullptr) {
            LOG(DEBUG) << "Failed reading: " << associated_devices_path;
            return true;
        }

        auto vap_id = get_vap_id_with_bss(vap.first);
        if (vap_id == beerocks::IFACE_ID_INVALID) {
            LOG(DEBUG) << "Invalid vap_id";
            continue;
        }
        //Lets iterate through all instances
        for (auto &associated_device_pwhm : *associated_devices_pwhm) {
            bool is_active;
            if (!associated_device_pwhm.second.read_child(is_active, "Active") || !is_active) {
                // we are only interested in connected stations
                continue;
            }

            std::string mac_addr;
            if (!associated_device_pwhm.second.read_child(mac_addr, "MACAddress")) {
                LOG(DEBUG) << "Failed reading MACAddress";
                continue;
            }
            LOG(DEBUG) << "Processing active AssociatedDevice MAC: " << mac_addr;

            // Check if this is an MLO client by reading APMLDMacAddress first
            // Legacy clients have APMLDMacAddress="00:00:00:00:00:00" (ZERO_MAC)
            // MLO clients have APMLDMacAddress with a valid MAC address
            std::string ap_mld_mac_str;
            std::string sta_path = associated_device_pwhm.first;
            bool is_mlo_client   = false;

            if (!associated_device_pwhm.second.read_child(ap_mld_mac_str, "APMLDMacAddress")) {
                LOG(DEBUG) << "APMLDMacAddress is ZERO_MAC for " << mac_addr;
            }
            sMacAddr ap_mld_mac = tlvf::mac_from_string(ap_mld_mac_str);
            if (ap_mld_mac != net::network_utils::ZERO_MAC) {
                is_mlo_client = true;
            }

            sMloClientInfo mlo_info;
            if (is_mlo_client) {
                if (!collect_mlo_client_association_info(mac_addr, sta_path, ap_mld_mac,
                                                         mlo_info)) {
                    LOG(ERROR) << "Failed to collect MLO client information for " << mac_addr;
                }
            }

            auto msg_buff =
                ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_CLIENT_ASSOCIATED_NOTIFICATION));
            LOG_IF(msg_buff == nullptr, FATAL) << "Memory allocation failed!";
            memset(msg_buff.get(), 0, sizeof(sACTION_APMANAGER_CLIENT_ASSOCIATED_NOTIFICATION));
            auto msg = reinterpret_cast<sACTION_APMANAGER_CLIENT_ASSOCIATED_NOTIFICATION *>(
                msg_buff.get());

            msg->params.vap_id = vap_id;
            // msg->bssid will reflect AP MLD Mac for MLO, BSSID for legacy stations
            if (!is_mlo_client) {
                msg->params.bssid = tlvf::mac_from_string(m_radio_info.available_vaps[vap_id].mac);
            } else {
                msg->params.bssid = mlo_info.ap_mld_bssid;
            }
            msg->params.mac                          = tlvf::mac_from_string(mac_addr);
            msg->params.capabilities.band_5g_capable = m_radio_info.is_5ghz;
            msg->params.capabilities.band_2g_capable =
                (son::wireless_utils::which_freq_type(m_radio_info.vht_center_freq) ==
                 beerocks::eFreqType::FREQ_24G);
            msg->params.association_frame_length = 0;
            msg->params.is_mlo                   = is_mlo_client;
            msg->params.num_affiliated_sta       = static_cast<uint8_t>(std::min<size_t>(
                mlo_info.affiliated_links.size(), beerocks::message::DEV_MAX_RADIOS));
            msg->params.mlo_modes                = mlo_info.mlo_modes;
            for (size_t i = 0; i < msg->params.num_affiliated_sta; ++i) {
                msg->params.affiliated_sta[i] = mlo_info.affiliated_links[i];
            }

            auto answer = get_last_assoc_frame(vap.first, mac_addr);
            if (!answer) {
                LOG(ERROR) << "fail to get last frame";
                continue;
            }
            std::string frame_body_str;
            if (!answer->read_child(frame_body_str, "frame") || frame_body_str.empty()) {
                LOG(WARNING) << "STA connected without previously receiving a "
                                "(re-)association frame!";
            } else {
                auto assoc_frame_type = assoc_frame::AssocReqFrame::UNKNOWN;
                auto management_frame = create_mgmt_frame_notification(frame_body_str.c_str());
                if (management_frame) {
                    auto &frame_body = management_frame->data;
                    // Add the latest association frame
                    std::copy(frame_body.begin(), frame_body.end(), msg->params.association_frame);
                    msg->params.association_frame_length = frame_body.size();
                    assoc_frame_type = assoc_frame::AssocReqFrame::ASSOCIATION_REQUEST;
                    if (management_frame->type == eManagementFrameType::REASSOCIATION_REQUEST) {
                        assoc_frame_type = assoc_frame::AssocReqFrame::REASSOCIATION_REQUEST;
                    }

                    auto assoc_frame = assoc_frame::AssocReqFrame::parse(
                        msg->params.association_frame, msg->params.association_frame_length,
                        assoc_frame_type);

                    auto res = son::assoc_frame_utils::get_station_capabilities_from_assoc_frame(
                        assoc_frame, msg->params.capabilities);
                    if (!res) {
                        LOG(ERROR) << "Failed to get station capabilities.";
                    };
                }
            }

            LOG(DEBUG) << "Pushing STA_Connected event for MAC: " << msg->params.mac
                       << ", BSSID: " << msg->params.bssid
                       << ", is_mlo: " << int(msg->params.is_mlo)
                       << ", num_affiliated: " << int(msg->params.num_affiliated_sta);
            event_queue_push(Event::STA_Connected, msg_buff);
        }
    }
    is_finished_all_clients = true;
    return true;
}

bool ap_wlan_hal_whm::pre_generate_connected_clients_events()
{

    // For the pwhm and the evolution of prplmesh, we dont see a need to implement this function, all will be done throughh the main
    // function generate_connected_clients_events
    return true;
}

bool ap_wlan_hal_whm::start_wps_pbc()
{
    AmbiorixVariant args, result;
    std::string main_vap_ifname = m_radio_info.available_vaps[0].bss;
    std::string wps_path        = wbapi_utils::search_path_ap_by_iface(main_vap_ifname) + "WPS.";
    bool ret                    = m_ambiorix_cl.call(wps_path, "InitiateWPSPBC", args, result);

    if (!ret) {
        LOG(ERROR) << "start_wps_pbc() failed!";
        return false;
    }
    return true;
}

static bool set_mbo_assoc_disallow_vap(beerocks::wbapi::AmbiorixClient &ambiorix_cl,
                                       const std::string &vap_path, bool enable)
{
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);

    std::string reason = enable ? "Unspecified" : "Off";

    args.add_child("MBOAssocDisallowReason", reason);

    bool ret = ambiorix_cl.update_object(vap_path, args);

    if (!ret) {
        LOG(ERROR) << "vap " << vap_path << " set MBOEnable/MBOAssocDisallow:" << reason
                   << " failed";
    }
    return ret;
}

bool ap_wlan_hal_whm::set_mbo_assoc_disallow(const std::string &bssid, bool enable)
{
    int vap_id = get_vap_id_with_mac(bssid);
    if (!check_vap_id(vap_id)) {
        LOG(ERROR) << "no matching vap_id for bssid " << bssid;
        return false;
    }
    auto &vap_info = m_radio_info.available_vaps[vap_id];
    auto &ifname   = vap_info.bss;
    auto vap_it    = m_vapsExtInfo.find(ifname);
    if (vap_it == m_vapsExtInfo.end()) {
        LOG(ERROR) << "fail to get ifname of " << bssid;
        return false;
    }

    return set_mbo_assoc_disallow_vap(m_ambiorix_cl, vap_it->second.path, enable);
}

bool ap_wlan_hal_whm::set_radio_mbo_assoc_disallow(bool enable)
{
    bool ret = true;
    for (const auto &vap_it : m_vapsExtInfo) {
        if (!set_mbo_assoc_disallow_vap(m_ambiorix_cl, vap_it.second.path, enable)) {
            ret = false;
        }
    }
    return ret;
}

bool ap_wlan_hal_whm::set_primary_vlan_id(uint16_t primary_vlan_id)
{
    LOG(DEBUG) << "set_primary_vlan_id " << primary_vlan_id;
    std::string wifi_vap_path, ifname;
    auto vaps = m_radio_info.available_vaps;
    for (auto vap_it = vaps.begin(); vap_it != vaps.end(); vap_it++) {
        // only applicable for Backhaul BSS interfaces
        if (vap_it->second.backhaul == false) {
            LOG(DEBUG) << __func__ << " : " << vap_it->second.bss
                       << " ifname skipped for set_vlan ";
            continue;
        }
        ifname       = vap_it->second.bss;
        auto vap_ext = m_vapsExtInfo.find(ifname);
        if (vap_ext == m_vapsExtInfo.end()) {
            LOG(ERROR) << "fail to get ifname " << ifname;
            continue;
        }
        wifi_vap_path = vap_ext->second.path;
        AmbiorixVariant vlan(AMXC_VAR_ID_HTABLE);
        vlan.add_child("MultiAPVlanId", primary_vlan_id);
        bool ret = m_ambiorix_cl.update_object(wifi_vap_path, vlan);
        if (!ret) {
            LOG(ERROR) << __func__ << " failed for ifname " << ifname;
        }
    }
    return true;
}

bool ap_wlan_hal_whm::set_disabled_subchannels(uint16_t bitmap)
{
    if (m_radio_path.empty()) {
        m_ambiorix_cl.resolve_path(wbapi_utils::search_path_radio_by_iface(m_radio_info.iface_name),
                                   m_radio_path);
    }

    std::string channels_in_use;

    // ChannelsInUse always contains channels in order
    m_ambiorix_cl.get_param(channels_in_use, m_radio_path, "ChannelsInUse");

    auto channels = beerocks::string_utils::str_split(channels_in_use, ',');

    std::string disabled_subchannels;

    for (size_t i = 0; i < channels.size(); ++i) {
        if ((bitmap >> i) & 1) {
            if (!disabled_subchannels.empty()) {
                disabled_subchannels += ",";
            }
            disabled_subchannels += channels[i];
        }
    }

    LOG(DEBUG) << "Radio " << m_radio_path << " is using " << channels_in_use
               << " disabling: " << disabled_subchannels;

    AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
    new_obj.add_child("DisabledSubChannels", disabled_subchannels);

    if (!m_ambiorix_cl.update_object(m_radio_path + "StaticPuncturing.", new_obj)) {
        LOG(ERROR) << "could not set DisabledSubChannel for " << m_radio_path;
        return false;
    }

    return true;
}

bool ap_wlan_hal_whm::set_cce_indication(uint16_t advertise_cce)
{
    LOG(DEBUG) << "ap_wlan_hal_whm: set_cce_indication, advertise_cce=" << advertise_cce;
    return true;
}

AmbiorixVariantSmartPtr ap_wlan_hal_whm::get_last_assoc_frame(const std::string &vap_iface,
                                                              const std::string &sta_mac)
{
    AmbiorixVariant data;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    args.add_child("mac", sta_mac);

    std::string ap_path{};
    bool ret = m_ambiorix_cl.resolve_path(wbapi_utils::search_path_ap_by_iface(vap_iface), ap_path);
    if (!ret) {
        LOG(ERROR) << "can't resolve " << wbapi_utils::search_path_ap_by_iface(vap_iface);
    } else {
        LOG(DEBUG) << "get assoc frame path " << ap_path << " for " << sta_mac;
    }

    ret = m_ambiorix_cl.call(ap_path, "getLastAssocReq", args, data);

    AmbiorixVariantSmartPtr result = data.find_child(0);
    if (!ret || !result) {
        LOG(ERROR) << "getLastAssocReq() failed!";
    } else {
        result->detach();
    }

    return result;
}

bool ap_wlan_hal_whm::process_radio_event(const std::string &interface, const std::string &key,
                                          const AmbiorixVariant *value)
{
    if (key == "Status") {
        std::string status = value->get<std::string>();
        if (status.empty()) {
            return true;
        }
        LOG(WARNING) << "radio " << interface << " status " << status;
    } else if (key == "Channel") {

        refresh_radio_info();
        // Event not processed by ap_manager.cpp (agent)
        event_queue_push(Event::CTRL_Channel_Switch);
        return true;
    } else if (key == "AccessPointNumberOfEntries") {
        LOG(WARNING) << "request updating vaps list of radio " << interface;
        event_queue_push(Event::APS_update_list);
        return true;
    }
    return true;
}

bool ap_wlan_hal_whm::process_radio_channel_change_event(const AmbiorixVariant *value)
{

    auto parameters = value->find_child("Updates");
    if (!parameters || parameters->empty()) {
        LOG(ERROR) << "Received event without Updates parameter";
        return false;
    }
    std::string chan_change_reason;
    if (!parameters->read_child(chan_change_reason, "ChannelChangeReason")) {
        LOG(ERROR) << "Received event without ChannelChangeReason parameter" << chan_change_reason;
        return false;
    }
    if (chan_change_reason != "MANUAL" && chan_change_reason != "AUTO") {
        LOG(ERROR) << "chan_change_reason other than MANUAL or AUTO:" << chan_change_reason;

        if (chan_change_reason != "DFS" || !m_accept_dfs_channel_change_after_cac_failure) {
            return false;
        }

        LOG(INFO) << "Channel change event received following a failed CAC_Completed, "
                  << "handling it as part of the radar flow...";
        m_accept_dfs_channel_change_after_cac_failure = false;
    }
    event_queue_push(Event::CSA_Finished);
    return true;
}

bool ap_wlan_hal_whm::process_ap_event(const std::string &interface, const std::string &key,
                                       const AmbiorixVariant *value)
{
    auto vap_id = get_vap_id_with_bss(interface);
    if (vap_id == beerocks::IFACE_ID_INVALID) {
        return true;
    }
    if (key == "Status") {
        std::string status = value->get<std::string>();
        if (status.empty()) {
            return true;
        }
        LOG(WARNING) << "vap " << interface << " status " << status;
        if (status == "Enabled") {
            auto msg_buff = ALLOC_SMART_BUFFER(sizeof(sHOSTAP_ENABLED_NOTIFICATION));
            auto msg      = reinterpret_cast<sHOSTAP_ENABLED_NOTIFICATION *>(msg_buff.get());
            LOG_IF(!msg, FATAL) << "Memory allocation failed!";
            memset(msg_buff.get(), 0, sizeof(sHOSTAP_ENABLED_NOTIFICATION));
            msg->vap_id = vap_id;
            event_queue_push(Event::AP_Enabled, msg_buff);
        } else {
            refresh_vaps_info(vap_id);
            auto msg_buff = ALLOC_SMART_BUFFER(sizeof(sHOSTAP_DISABLED_NOTIFICATION));
            auto msg      = reinterpret_cast<sHOSTAP_DISABLED_NOTIFICATION *>(msg_buff.get());
            LOG_IF(!msg, FATAL) << "Memory allocation failed!";
            memset(msg_buff.get(), 0, sizeof(sHOSTAP_DISABLED_NOTIFICATION));
            msg->vap_id = vap_id;
            event_queue_push(Event::AP_Disabled, msg_buff);
        }
    }
    return true;
}

bool ap_wlan_hal_whm::collect_mlo_client_association_info(const std::string &sta_mac,
                                                          const std::string &sta_path,
                                                          const sMacAddr &ap_mld_bssid,
                                                          sMloClientInfo &mlo_info)
{
    mlo_info.mlo_modes      = 0;
    mlo_info.client_mld_mac = {};
    mlo_info.ap_mld_bssid   = ap_mld_bssid;
    mlo_info.affiliated_links.clear();

    // Get all AffiliatedSta entries as multi object
    std::string affiliated_sta_path = sta_path + "AffiliatedSta.";
    auto affiliated_sta_objects =
        m_ambiorix_cl.get_object_multi<AmbiorixVariantMapSmartPtr>(affiliated_sta_path);

    if (!affiliated_sta_objects || affiliated_sta_objects->empty()) {
        LOG(ERROR) << "No AffiliatedSta entries found for " << sta_mac;
        return false;
    }

    LOG(INFO) << "Found " << affiliated_sta_objects->size() << " AffiliatedSta entries for "
              << sta_mac;

    // Read MLOMode from AssociatedDevice
    std::string mlo_mode;
    if (!m_ambiorix_cl.get_param(mlo_mode, sta_path, "MLOMode")) {
        LOG(ERROR) << "Failed to get MLOMode for " << sta_path;
    }
    LOG(INFO) << "MLOMode read from data model: " << mlo_mode;

    std::transform(mlo_mode.begin(), mlo_mode.end(), mlo_mode.begin(), ::toupper);
    if (mlo_mode.find("NSTR") != std::string::npos) {
        mlo_info.mlo_modes |= beerocks::message::MLO_MODE_NSTR;
    } else if (mlo_mode.find("STR") != std::string::npos) {
        mlo_info.mlo_modes |= beerocks::message::MLO_MODE_STR;
    } else if (mlo_mode.find("EMLSR") != std::string::npos) {
        mlo_info.mlo_modes |= beerocks::message::MLO_MODE_EMLSR;
    } else if (mlo_mode.find("EMLMR") != std::string::npos) {
        mlo_info.mlo_modes |= beerocks::message::MLO_MODE_EMLMR;
    } else {
        LOG(ERROR) << "MLO read failed";
    }

    mlo_info.client_mld_mac = tlvf::mac_from_string(sta_mac);
    LOG(DEBUG) << "Client MAC: " << mlo_info.client_mld_mac << ", APMLD: " << mlo_info.ap_mld_bssid;

    // Iterate over all AffiliatedSta entries
    for (auto &affiliated_sta : *affiliated_sta_objects) {
        if (mlo_info.affiliated_links.size() >= beerocks::message::DEV_MAX_RADIOS) {
            LOG(INFO) << "AffiliatedSta list truncated for " << sta_mac
                      << " (max: " << int(beerocks::message::DEV_MAX_RADIOS) << ")";
            break;
        }

        std::string affiliated_mac;
        if (!affiliated_sta.second.read_child(affiliated_mac, "MACAddress")) {
            LOG(ERROR) << "Failed reading AffiliatedSta MACAddress for " << sta_mac;
            continue;
        }

        bool is_active = false;
        if (!affiliated_sta.second.read_child(is_active, "Active") || !is_active) {
            LOG(DEBUG) << "Skipping inactive AffiliatedSta link - Affiliated MAC: "
                       << affiliated_mac << ", STA MAC: " << sta_mac;
            continue;
        }

        std::string affiliated_bssid_str;
        sMacAddr affiliated_bssid = mlo_info.ap_mld_bssid;
        if (!affiliated_sta.second.read_child(affiliated_bssid_str, "BSSID")) {
            LOG(ERROR) << "Failed reading AffiliatedSta BSSID, using AP MLD BSSID: "
                       << affiliated_bssid;
        } else {
            affiliated_bssid = tlvf::mac_from_string(affiliated_bssid_str);
        }

        sAffiliatedStaInfo affiliated_info{};
        affiliated_info.affiliated_sta_mac = tlvf::mac_from_string(affiliated_mac);
        affiliated_info.bssid              = affiliated_bssid;

        LOG(DEBUG) << "Added affiliated link [" << int(mlo_info.affiliated_links.size())
                   << "]: MAC=" << affiliated_info.affiliated_sta_mac
                   << ", BSSID=" << affiliated_info.bssid;
        mlo_info.affiliated_links.push_back(affiliated_info);
    }

    // Return false if we couldn't collect any valid affiliated links
    if (mlo_info.affiliated_links.empty()) {
        LOG(ERROR) << "No valid AffiliatedSta entries collected for " << sta_mac;
        return false;
    }

    LOG(DEBUG) << "MLO params - mlo_modes: " << std::hex << int(mlo_info.mlo_modes) << std::dec
               << " (str=" << ((mlo_info.mlo_modes & beerocks::message::MLO_MODE_STR) ? 1 : 0)
               << ", nstr=" << ((mlo_info.mlo_modes & beerocks::message::MLO_MODE_NSTR) ? 1 : 0)
               << ", emlsr=" << ((mlo_info.mlo_modes & beerocks::message::MLO_MODE_EMLSR) ? 1 : 0)
               << ", emlmr=" << ((mlo_info.mlo_modes & beerocks::message::MLO_MODE_EMLMR) ? 1 : 0)
               << ")";

    return true;
}

bool ap_wlan_hal_whm::send_wds_iface_notification(const std::string &sta_mac, const sMacAddr &bssid,
                                                  int8_t vap_id, const std::string &wds_iface_name)
{
    auto msg_buff = ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_WDS_IFACE_NOTIFICATION));
    auto msg      = reinterpret_cast<sACTION_APMANAGER_WDS_IFACE_NOTIFICATION *>(msg_buff.get());
    LOG_IF(!msg, FATAL) << "Memory allocation failed!";

    memset(msg_buff.get(), 0, sizeof(sACTION_APMANAGER_WDS_IFACE_NOTIFICATION));

    msg->params.mac    = tlvf::mac_from_string(sta_mac);
    msg->params.bssid  = bssid;
    msg->params.vap_id = vap_id;
    beerocks::string_utils::copy_string(msg->params.wds_iface_name, wds_iface_name.c_str(),
                                        beerocks::message::IFACE_NAME_LENGTH);

    LOG(DEBUG) << "Pushing WDS iface notification for MAC: " << msg->params.mac
               << ", BSSID: " << msg->params.bssid << ", iface: " << msg->params.wds_iface_name;
    event_queue_push(Event::STA_WDS_Iface_Ready, msg_buff);

    return true;
}

bool ap_wlan_hal_whm::process_sta_connected_event(
    const std::string &interface, const std::string &sta_mac, const std::string &key,
    const AmbiorixVariant *value, const std::string &sta_path, const std::string &vap_path)
{
    auto vap_id = get_vap_id_with_bss(interface);
    LOG(DEBUG) << "Processing STA connected event - interface: " << interface << ", STA MAC: "
               << sta_mac << ", key: " << key << ", vap_path: " << vap_path
               << ", sta_path: " << sta_path;

    if (!check_vap_id(vap_id)) {
        LOG(ERROR) << "Invalid vap_id for interface " << interface;
        return true;
    }

    if (key == "AuthenticationState") {
        bool connected = value->get<bool>();
        if (connected) {
            auto msg_buff =
                ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_CLIENT_ASSOCIATED_NOTIFICATION));
            auto msg = reinterpret_cast<sACTION_APMANAGER_CLIENT_ASSOCIATED_NOTIFICATION *>(
                msg_buff.get());
            LOG_IF(!msg, FATAL) << "Memory allocation failed!";

            // Check if this is an MLO client by reading APMLDMacAddress first
            // Legacy clients have APMLDMacAddress="00:00:00:00:00:00" (ZERO_MAC)
            // MLO clients have APMLDMacAddress with a valid MAC address
            std::string ap_mld_mac_str;
            bool is_mlo_client = false;

            if (!m_ambiorix_cl.get_param(ap_mld_mac_str, sta_path, "APMLDMacAddress")) {
                LOG(DEBUG) << "Failed reading APMLDMacAddress for " << sta_mac
                           << ", treating as legacy client";
            }
            sMacAddr ap_mld_mac = tlvf::mac_from_string(ap_mld_mac_str);
            if (ap_mld_mac != net::network_utils::ZERO_MAC) {
                is_mlo_client = true;
            } else {
                LOG(DEBUG) << "APMLDMacAddress is ZERO_MAC for " << sta_mac;
            }

            sMloClientInfo mlo_info;
            if (is_mlo_client) {
                if (!collect_mlo_client_association_info(sta_mac, sta_path, ap_mld_mac, mlo_info)) {
                    LOG(ERROR) << " Failed to collect MLO client information for " << sta_mac;
                }
            }

            memset(msg_buff.get(), 0, sizeof(sACTION_APMANAGER_CLIENT_ASSOCIATED_NOTIFICATION));

            auto answer = get_last_assoc_frame(interface, sta_mac);
            if (!answer) {
                LOG(ERROR) << "fail to get last frame";
                return true;
            }

            msg->params.vap_id = vap_id;
            // msg->bssid will reflect AP MLD Mac for MLO, BSSID for legacy stations
            if (!is_mlo_client) {
                msg->params.bssid = tlvf::mac_from_string(m_radio_info.available_vaps[vap_id].mac);
            } else {
                msg->params.bssid = mlo_info.ap_mld_bssid;
            }
            msg->params.mac          = tlvf::mac_from_string(sta_mac);
            msg->params.capabilities = {};
            //init the freq band cap with the target radio freq band info
            msg->params.capabilities.band_5g_capable = m_radio_info.is_5ghz;
            msg->params.capabilities.band_2g_capable =
                (son::wireless_utils::which_freq_type(m_radio_info.vht_center_freq) ==
                 beerocks::eFreqType::FREQ_24G);
            msg->params.association_frame_length = 0;
            msg->params.is_mlo                   = is_mlo_client;

            LOG(INFO) << "Connected station " << sta_mac << " over vap " << interface;

            msg->params.num_affiliated_sta = static_cast<uint8_t>(std::min<size_t>(
                mlo_info.affiliated_links.size(), beerocks::message::DEV_MAX_RADIOS));
            msg->params.mlo_modes          = mlo_info.mlo_modes;

            for (size_t i = 0; i < msg->params.num_affiliated_sta; ++i) {
                msg->params.affiliated_sta[i] = mlo_info.affiliated_links[i];
            }

            std::string frame_body_str;
            if (!answer->read_child(frame_body_str, "frame") || frame_body_str.empty()) {
                LOG(WARNING) << "STA connected without previously receiving a "
                                "(re-)association frame!";
            } else {
                auto assoc_frame_type = assoc_frame::AssocReqFrame::UNKNOWN;
                // Tunnel the Management request to the controller
                auto management_frame = create_mgmt_frame_notification(frame_body_str.c_str());
                if (management_frame) {

                    // create_mgmt_frame_notification will fill mac with 802.11 source address
                    // sta_mac may hold MLD Station MAC if present
                    management_frame->mac = tlvf::mac_from_string(sta_mac);

                    event_queue_push(Event::MGMT_Frame, management_frame);
                    // For MLO, preserve the MLD BSSID - don't overwrite with link-specific BSSID
                    if (!msg->params.is_mlo) {
                        msg->params.bssid = management_frame->bssid;
                    }
                    auto mac = tlvf::mac_to_string(management_frame->bssid);
                    vap_id   = get_vap_id_with_mac(mac);
                    if (check_vap_id(vap_id)) {
                        msg->params.vap_id = vap_id;
                    }
                    auto &frame_body = management_frame->data;
                    // Add the latest association frame
                    std::copy(frame_body.begin(), frame_body.end(), msg->params.association_frame);
                    msg->params.association_frame_length = frame_body.size();
                    assoc_frame_type = assoc_frame::AssocReqFrame::ASSOCIATION_REQUEST;
                    if (management_frame->type == eManagementFrameType::REASSOCIATION_REQUEST) {
                        assoc_frame_type = assoc_frame::AssocReqFrame::REASSOCIATION_REQUEST;
                    }

                    auto assoc_frame = assoc_frame::AssocReqFrame::parse(
                        msg->params.association_frame, msg->params.association_frame_length,
                        assoc_frame_type);

                    auto res = son::assoc_frame_utils::get_station_capabilities_from_assoc_frame(
                        assoc_frame, msg->params.capabilities);
                    if (!res) {
                        LOG(ERROR) << "Failed to get station capabilities.";
                    } else {
                        son::wireless_utils::print_station_capabilities(msg->params.capabilities);
                    }
                }
            }

            LOG(DEBUG) << "Pushing STA_Connected event for MAC: " << msg->params.mac
                       << ", BSSID: " << msg->params.bssid
                       << ", is_mlo: " << int(msg->params.is_mlo)
                       << ", num_affiliated: " << int(msg->params.num_affiliated_sta);
            event_queue_push(Event::STA_Connected, msg_buff);
        }
    } else if (key == "WdsInterfaceName") {
        const auto bssid          = tlvf::mac_from_string(m_radio_info.available_vaps[vap_id].mac);
        const auto wds_iface_name = value->get<std::string>();

        if (wds_iface_name.empty()) {
            return true;
        }

        if (!send_wds_iface_notification(sta_mac, bssid, vap_id, wds_iface_name)) {
            LOG(ERROR) << "Failed sending WDS iface notification for " << sta_mac;
        }
    }
    return true;
}

bool ap_wlan_hal_whm::process_sta_disassoc_event(const std::string &interface,
                                                 const beerocks::wbapi::AmbiorixVariant *event_data)
{
    if (event_data == nullptr) {
        LOG(ERROR) << "event_data null";
        return false;
    }

    std::string name_notification;
    event_data->read_child(name_notification, "notification");

    auto msg_buff = ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_CLIENT_DISCONNECTED_NOTIFICATION));
    auto msg =
        reinterpret_cast<sACTION_APMANAGER_CLIENT_DISCONNECTED_NOTIFICATION *>(msg_buff.get());
    LOG_IF(!msg, FATAL) << "Memory allocation failed!";

    // Initialize the message
    memset(msg_buff.get(), 0, sizeof(sACTION_APMANAGER_CLIENT_DISCONNECTED_NOTIFICATION));

    auto vap_id = get_vap_id_with_bss(interface);
    if (vap_id == beerocks::IFACE_ID_INVALID) {
        LOG(ERROR) << "Invalid vap_id";
        return false;
    }
    msg->params.vap_id = vap_id;

    auto data = event_data->find_child("Data");
    if (!data || data->empty()) {
        LOG(WARNING) << "Missing or empty Data field in event_data";
        return false;
    }
    auto data_map = data->read_children<AmbiorixVariantMapSmartPtr>();
    if (!data_map) {
        LOG(WARNING) << "Data field could not be parsed into data_map";
        return false;
    }

    std::string sta_mac;
    if (data_map->find("MACAddress") != data_map->end()) {
        (*data_map)["MACAddress"].get(sta_mac);
        msg->params.mac = tlvf::mac_from_string(sta_mac);
    }

    if (data_map->find("DeauthReason") != data_map->end()) {
        (*data_map)["DeauthReason"].get(msg->params.reason);
    }

    LOG(INFO) << "disconnected station " << sta_mac << " from vap "
              << interface << " reason: " << msg->params.reason;

    event_queue_push(Event::STA_Disconnected, msg_buff);

    return true;
}

bool ap_wlan_hal_whm::process_ap_bss_event(const std::string &interface,
                                           const beerocks::wbapi::AmbiorixVariant *event_data)
{
    if (event_data == nullptr) {
        LOG(WARNING) << "event_data null";
        return false;
    }
    std::string name_notification;
    event_data->read_child(name_notification, "notification");
    if (name_notification == AMX_CL_BSS_TM_RESPONSE_EVT) {
        auto msg_buff = ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_CLIENT_BSS_STEER_RESPONSE));
        auto msg = reinterpret_cast<sACTION_APMANAGER_CLIENT_BSS_STEER_RESPONSE *>(msg_buff.get());
        LOG_IF(!msg, FATAL) << "Memory allocation failed!";

        // Initialize the message
        memset(msg_buff.get(), 0, sizeof(sACTION_APMANAGER_CLIENT_BSS_STEER_RESPONSE));

        // Client params
        std::string data;
        event_data->read_child(data, "PeerMacAddress");
        msg->params.mac = tlvf::mac_from_string(data);
        int32_t status_code(UINT32_MAX);
        event_data->read_child(status_code, "StatusCode");

        auto vap_id = get_vap_id_with_bss(interface);
        if (vap_id == beerocks::IFACE_ID_INVALID) {
            LOG(ERROR) << "Invalid vap_id";
            return false;
        }
        msg->params.source_bssid = tlvf::mac_from_string(m_radio_info.available_vaps[vap_id].mac);

        msg->params.status_code = status_code;
        if (msg->params.status_code == 0) {
            event_data->read_child(data, "TargetBssid");
            msg->params.target_bssid = tlvf::mac_from_string(data);
        } else {
            LOG(ERROR) << "BSS Transition Management Query for station " << msg->params.mac
                       << " has been rejected with Status code = " << msg->params.status_code;
        }

        LOG(DEBUG) << "BTM Response with mac= " << msg->params.mac
                   << " status_code= " << msg->params.status_code
                   << " source_bssid= " << msg->params.source_bssid
                   << " target_bssid= " << msg->params.target_bssid;

        // Add the message to the queue
        event_queue_push(Event::BSS_TM_Response, msg_buff);
    } else if (name_notification == AMX_CL_MGMT_ACT_FRAME_EVT) {

        std::string frame_body_str;
        if (!event_data->read_child<>(frame_body_str, "frame") || frame_body_str.empty()) {
            LOG(WARNING) << "Unable to retrieve MGMT Frame from pwhm notification";
        }

        auto management_frame = create_mgmt_frame_notification(frame_body_str.c_str());
        if (management_frame) {
            event_queue_push(Event::MGMT_Frame, management_frame);
        } else {
            LOG(ERROR) << "creage_mgmt_frame_notification failed";
        }
    }
    return true;
}

bool ap_wlan_hal_whm::process_wpa_ctrl_event(const beerocks::wbapi::AmbiorixVariant &event_data)
{
    std::string event_str;
    if (!event_data.read_child<>(event_str, "eventData") || event_str.empty()) {
        LOG(WARNING) << "Unable to retrieve wpaCtrl event data from pwhm notification";
        return false;
    }
    LOG(DEBUG) << "wpaCtrl event: " << event_str;

    std::string interface;
    if (!event_data.read_child<>(interface, "ifName") || interface.empty()) {
        LOG(WARNING) << "Unable to retrieve ifName from pwhm notification";
        return false;
    }
    LOG(DEBUG) << "interface: " << interface;

    bwl::parsed_line_t parsed_obj;
    parse_event(event_str, parsed_obj);

    std::string opcode;
    if (!(parsed_obj.find(bwl::EVENT_KEYLESS_PARAM_OPCODE) != parsed_obj.end() &&
          !(opcode = parsed_obj[bwl::EVENT_KEYLESS_PARAM_OPCODE]).empty())) {
        return false;
    }

    auto event = wpaCtrl_to_bwl_event(opcode);

    switch (event) {

    case Event::DFS_CAC_Started: {
        auto msg_buff =
            ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_HOSTAP_DFS_CAC_STARTED_NOTIFICATION));
        auto msg = reinterpret_cast<sACTION_APMANAGER_HOSTAP_DFS_CAC_STARTED_NOTIFICATION *>(
            msg_buff.get());
        LOG_IF(!msg, FATAL) << "Memory allocation failed!";

        // Initialize the message
        memset(msg_buff.get(), 0, sizeof(sACTION_APMANAGER_HOSTAP_DFS_CAC_STARTED_NOTIFICATION));

        // Channel
        msg->params.channel = beerocks::string_utils::stoi(parsed_obj["chan"]);

        // Secondary Channel
        std::string tmp_string = parsed_obj["sec_chan"];
        beerocks::string_utils::rtrim(tmp_string, ",");
        msg->params.secondary_channel = beerocks::string_utils::stoi(tmp_string);

        // Bandwidth
        tmp_string = parsed_obj["width"];
        beerocks::string_utils::rtrim(tmp_string, ",");
        msg->params.bandwidth = beerocks::eWiFiBandwidth(
            wpaCtrl_bw_to_beerocks_bw(beerocks::string_utils::stoi(tmp_string)));

        // CAC Duration
        tmp_string = parsed_obj["cac_time"];
        beerocks::string_utils::rtrim(tmp_string, "s");
        msg->params.cac_duration_sec = beerocks::string_utils::stoi(tmp_string);

        // Reset the flag at CAC_Started. It will be updated again on CAC_Completed.
        m_accept_dfs_channel_change_after_cac_failure = false;

        // Add the message to the queue
        event_queue_push(Event::DFS_CAC_Started, msg_buff);
        break;
    }
    case Event::DFS_CAC_Completed: {
        if (!get_radio_info().is_5ghz) {
            LOG(WARNING) << "interface: " << interface << " not 5GHz radio!";
            return true;
        }

        auto msg_buff =
            ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_HOSTAP_DFS_CAC_COMPLETED_NOTIFICATION));
        auto msg = reinterpret_cast<sACTION_APMANAGER_HOSTAP_DFS_CAC_COMPLETED_NOTIFICATION *>(
            msg_buff.get());
        LOG_IF(!msg, FATAL) << "Memory allocation failed!";

        // Initialize the message
        memset(msg_buff.get(), 0, sizeof(sACTION_APMANAGER_HOSTAP_DFS_CAC_COMPLETED_NOTIFICATION));

        // CAC Status
        std::string success = parsed_obj["cac_status"];
        if (success.empty()) {
            // Some wpaCtrl_events still received with "success" parameter and we should support it as well
            success = parsed_obj["success"];
            if (success.empty()) {
                LOG(ERROR) << "Failed reading cac finished success parameter!";
                return false;
            }
        }
        msg->params.success = beerocks::string_utils::stoi(success);

        /**
         * CAC failed (success == 0), meaning the radio will switch to a non-DFS channel.
         * In this case we expect a DFS-triggered channel change event, and we must
         * accept it rather than ignore it.
         */
        m_accept_dfs_channel_change_after_cac_failure = (msg->params.success == 0);

        // Frequency
        msg->params.frequency = beerocks::string_utils::stoi(parsed_obj["freq"]);

        // Center frequency 1
        msg->params.center_frequency1 = beerocks::string_utils::stoi(parsed_obj["cf1"]);

        // Center frequency 2
        msg->params.center_frequency2 = beerocks::string_utils::stoi(parsed_obj["cf2"]);

        // Channel
        msg->params.channel = son::wireless_utils::freq_to_channel(msg->params.frequency);

        // Timeout
        std::string timeout = parsed_obj["timeout"];
        if (!timeout.empty()) {
            msg->params.timeout = beerocks::string_utils::stoi(timeout);
        }

        // Bandwidth
        msg->params.bandwidth =
            wpaCtrl_bw_to_beerocks_bw(beerocks::string_utils::stoi(parsed_obj["chan_width"]));

        // Add the message to the queue
        event_queue_push(Event::DFS_CAC_Completed, msg_buff);
        break;
    }
    case Event::DFS_NOP_Finished: {
        auto msg_buff =
            ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_HOSTAP_DFS_CHANNEL_AVAILABLE_NOTIFICATION));
        auto msg = reinterpret_cast<sACTION_APMANAGER_HOSTAP_DFS_CHANNEL_AVAILABLE_NOTIFICATION *>(
            msg_buff.get());
        LOG_IF(!msg, FATAL) << "Memory allocation failed!";

        // Initialize the message
        memset(msg_buff.get(), 0,
               sizeof(sACTION_APMANAGER_HOSTAP_DFS_CHANNEL_AVAILABLE_NOTIFICATION));

        // Frequency
        msg->params.frequency = beerocks::string_utils::stoi(parsed_obj["freq"]);

        // Channel
        msg->params.channel = son::wireless_utils::freq_to_channel(msg->params.frequency);

        // Bandwidth
        msg->params.bandwidth =
            wpaCtrl_bw_to_beerocks_bw(beerocks::string_utils::stoi(parsed_obj["chan_width"]));

        // Center frequency
        msg->params.vht_center_frequency = beerocks::string_utils::stoi(parsed_obj["cf1"]);

        // Add the message to the queue
        event_queue_push(Event::DFS_NOP_Finished, msg_buff);
        break;
    }
    case Event::CSA_Finished: {
        const std::string reason  = parsed_obj["reason"];
        ChanSwReason chanSwReason = ChanSwReason::Unknown;
        if (reason == "RADAR") {
            chanSwReason = ChanSwReason::Radar;
        }
        m_radio_info.last_csa_sw_reason = chanSwReason;
        event_queue_push(event);
        break;
    }
    case Event::WPA_Event_EAP_Failure:
    case Event::WPA_Event_EAP_Failure2:
    case Event::WPA_Event_EAP_Timeout_Failure:
    case Event::WPA_Event_EAP_Timeout_Failure2:
    case Event::WPA_Event_SAE_Unknown_Password_Identifier:
    case Event::AP_Sta_Possible_Psk_Mismatch:
        /* example PSK Mismatch notification
            eobject = "WiFi.AccessPoint.[vap5g0priv].",
            eventData = "<3>AP-STA-POSSIBLE-PSK-MISMATCH 6c:f7:84:d8:32:af",
            // or with status/reason code
            // eventData = "<3>AP-STA-POSSIBLE-PSK-MISMATCH 6c:f7:84:d8:32:af status=14/reason=15",
            ifName = "wlan2.1",
            notification = "wpaCtrlEvents",
            object = "WiFi.AccessPoint.vap5g0priv.",
            path = "WiFi.AccessPoint.1."
        */
    case Event::ACL_DENY: {
        // eventData = "<3>ACL-DENY AA:BB:CC:DD:EE:FF"
        auto vap_id    = get_vap_id_with_bss(interface);
        auto iface_ids = beerocks::utils::get_ids_from_iface_string(interface);
        if ((vap_id < 0) && (iface_ids.vap_id != beerocks::IFACE_RADIO_ID)) {
            LOG(DEBUG) << "Unknown vap_id " << vap_id;
        }

        LOG(DEBUG) << "STA Connection Failure";
        auto msg_buff = ALLOC_SMART_BUFFER(sizeof(sStaConnectionFail));
        auto msg      = reinterpret_cast<sStaConnectionFail *>(msg_buff.get());
        LOG_IF(!msg, FATAL) << "Memory allocation failed!";

        // Initialize the message
        memset(msg_buff.get(), 0, sizeof(sStaConnectionFail));

        // STA Mac Address
        msg->sta_mac = tlvf::mac_from_string(parsed_obj[bwl::EVENT_KEYLESS_PARAM_MAC]);
        LOG(DEBUG) << "STA connection failure: offending Sta MAC: " << msg->sta_mac;

        // BSSID
        msg->bssid = tlvf::mac_from_string(m_radio_info.available_vaps[vap_id].mac);
        LOG(DEBUG) << "STA connection failure: interface BSSID: " << msg->bssid;

        std::string status_str;
        std::string reason_str;
        if (event == Event::ACL_DENY) {

            // As specified in Wi-Fi EasyMesh® Specification,
            // Section 11.6, "Client Association Control Mechanism"
            status_str = CODE_INSUFFICIENT_BANDWIDTH;
            reason_str = CODE_OK;
        } else {
            status_str = parsed_obj["status"];
            reason_str = parsed_obj["reason"];
            if (status_str.empty()) {
                status_str = reason_str.empty() ? CODE_UNSPECIFIED : CODE_OK;
            }
            if (reason_str.empty()) {
                reason_str = CODE_OK;
            }
        }

        msg->status = beerocks::string_utils::stoi(status_str);
        LOG(DEBUG) << "STA connection failure: status: " << msg->status;

        msg->reason = beerocks::string_utils::stoi(reason_str);
        LOG(DEBUG) << "STA connection failure: reason: " << msg->reason;

        // Add the message to the queue
        event_queue_push(event, msg_buff);
        break;
    }
    // Unhandled events
    default:
        LOG(ERROR) << "Unhandled event received: " << int(event);
        break;
    }

    return true;
}

bool ap_wlan_hal_whm::process_afc_update_event(const AmbiorixVariant *value)
{
    auto parameters = value->find_child("Updates");
    if (!parameters || parameters->empty()) {
        LOG(ERROR) << "Received AFC Update event without Updates parameter";
        return false;
    }

    std::string status;
    if (!parameters->read_child(status, "InquiryStatus")) {
        LOG(ERROR) << "Received AFC Update event without InquiryStatus parameter";
        return false;
    }

    if (status != "UPDATE") {
        LOG(ERROR) << "AFC Update status other than UPDATE: " << status;
        return false;
    }

    // Emit AFCUpdate event when status is UPDATE
    event_queue_push(Event::AFCUpdate);
    return true;
}

bool ap_wlan_hal_whm::set(const std::string &param, const std::string &value, int vap_id)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

int ap_wlan_hal_whm::add_bss(std::string &ifname, son::wireless_utils::sBssInfoConf &bss_conf,
                             std::string &bridge, bool vbss)
{
    // Virtual bss will not be covered by the pwhm, for now!
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED!";
    return false;
}

bool ap_wlan_hal_whm::remove_bss(std::string &ifname)
{
    // Virtual bss will not be covered by the pwhm, for now!
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED!";
    return false;
}

bool ap_wlan_hal_whm::add_key(const std::string &ifname, const sKeyInfo &key_info)
{
    // Virtual bss will not be covered by the pwhm, for now!
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED!";
    return false;
}

bool ap_wlan_hal_whm::add_station(const std::string &ifname, const sMacAddr &mac,
                                  std::vector<uint8_t> &raw_assoc_req)
{
    // Virtual bss will not be covered by the pwhm, for now!
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED!";
    return false;
}

bool ap_wlan_hal_whm::get_key(const std::string &ifname, sKeyInfo &key_info)
{
    // Virtual bss will not be covered by the pwhm, for now!
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED!";
    return false;
}

bool ap_wlan_hal_whm::send_delba(const std::string &ifname, const sMacAddr &dst,
                                 const sMacAddr &src, const sMacAddr &bssid)
{
    // Virtual bss will not be covered by the pwhm, for now!
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED!";
    return false;
}

void ap_wlan_hal_whm::send_unassoc_sta_link_metric_query(
    std::shared_ptr<wfa_map::tlvUnassociatedStaLinkMetricsQuery> &query)
{
}

bool ap_wlan_hal_whm::prepare_unassoc_sta_link_metrics_response(
    std::shared_ptr<wfa_map::tlvUnassociatedStaLinkMetricsResponse> &response)
{
    //LOG(TRACE) << __func__ << " - NOT IMPLEMENTED!";
    return false;
}

bool ap_wlan_hal_whm::set_beacon_da(const std::string &ifname, const sMacAddr &mac)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::update_beacon(const std::string &ifname)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::set_no_deauth_unknown_sta(const std::string &ifname, bool value)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}

bool ap_wlan_hal_whm::configure_service_priority(const uint8_t *dscp)
{
    unsigned char i = 0, j = 0, k = 0;

    struct range_t {
        uint8_t pcp;
        int8_t start;
        int8_t end;
    } range[8] = {};

    struct map_t {
        uint8_t dscp;
        uint8_t pcp;
    } exception[64] = {};

    std::stringstream ss;

    for (i = 0; i < 8; i++) {
        range[i].start = -1;
        range[i].end   = -1;
        range[i].pcp   = i;
    }
    for (i = 0; i < 64; i++) {
        exception[i].dscp = -1;
        exception[i].pcp  = -1;
    }
    for (i = 0; i < 64; i++) {
        if ((i != 63) && dscp[i] == dscp[i + 1]) {
            for (j = i + 1; j < 64; j++) {
                if (j == 63 || dscp[j] != dscp[j + 1]) {
                    if ((j - i) >= (range[dscp[j]].end - range[dscp[j]].start)) {
                        range[dscp[j]].start = i;
                        range[dscp[j]].end   = j;
                        i                    = j;
                        break;
                    }
                } else {
                    continue;
                }
            }
        }
    }
    for (i = 0; i < 8; i++) {
        LOG(DEBUG) << "range[" << +i << "] : start = " << +range[i].start
                   << ", end = " << +range[i].end;
    }
    for (i = 0, j = 0; i < 64; i++) {
        for (k = 0; k < 8; k++) {
            if ((i >= range[k].start) && (i <= range[k].end)) {
                break;
            }
        }
        if (k == 8) {
            exception[j].pcp    = dscp[i];
            exception[j++].dscp = i;
        }
    }
    for (i = 0; i < 64; i++) {
        if (exception[i].dscp == 255) {
            break;
        }
    }
    for (i = 0; i < 21; i++) {
        if (exception[i].dscp == 255) {
            break;
        }
        ss << +exception[i].dscp << "," << +exception[i].pcp << ",";
    }
    for (i = 0; i < 8; i++) {
        ss << +range[i].start << "," << +range[i].end << ",";
    }

    std::string qos_map = ss.str();
    if (!qos_map.empty() && qos_map.back() == ',')
        qos_map.pop_back();

    LOG(DEBUG) << "Setting QOS_MAP_SET " << qos_map;

    // Getting APs paths by radRef (getting all APs for specific radio)
    // pWHM: Radio obj -> Alias -> "WiFi.Radio." + Alias == RadioReference (e.g. "WiFi.Radio.radio0")
    LOG(INFO) << "Getting APs paths by RadioReference for radio=" << get_iface_name();
    const auto radio_obj =
        m_ambiorix_cl.get_object(wbapi_utils::search_path_radio_by_iface(get_iface_name()));
    if (!radio_obj) {
        LOG(ERROR) << "Could not resolve get radio object for iface=" << get_iface_name();
        return false;
    }

    std::string alias;
    if (!radio_obj->read_child(alias, "Alias")) {
        LOG(ERROR) << "Alias was not found for iface=" << get_iface_name();
        return false;
    }
    auto radRef = wbapi_utils::search_path_radRef_by_alias(alias);

    // TODO: Handle pWHM DM Radio/SSID/ProfileReferences (PPM-3533)
    constexpr const char *device_prefix = "Device.";
    if (radRef.rfind(device_prefix, 0) == 0) {
        radRef.erase(0, strlen(device_prefix));
        LOG(DEBUG) << "Stripped Device prefix, new radRef: " << radRef;
    }

    const auto search_path_ap_by_radRef = wbapi_utils::search_path_ap_by_radRef(radRef);
    LOG(TRACE) << "search_path=" << search_path_ap_by_radRef;

    std::vector<std::string> paths;
    if (!m_ambiorix_cl.resolve_path_multi(search_path_ap_by_radRef, paths)) {
        LOG(ERROR) << "Could not resolve " << search_path_ap_by_radRef;
        return false;
    }

    for (const auto &path : paths) {
        AmbiorixVariant new_map(AMXC_VAR_ID_HTABLE);
        new_map.add_child("QoSMapSet", qos_map);

        if (!m_ambiorix_cl.update_object(path + "IEEE80211u.", new_map)) {
            LOG(ERROR) << "Could not set QoSMapSet for " << path;
            return false;
        }
    }

    return true;
}

bool ap_wlan_hal_whm::set_spatial_reuse_config(
    son::wireless_utils::sSpatialReuseParams &spatial_reuse_params)
{
    std::string path_to_80211ax = m_radio_path + "IEEE80211ax.";
    AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);

    new_obj.add_child("BssColor", spatial_reuse_params.bss_color);
    new_obj.add_child("BssColorPartial", spatial_reuse_params.partial_bss_color);
    new_obj.add_child("HESIGASpatialReuseValue15Allowed",
                      spatial_reuse_params.hesiga_spatial_reuse_value15_allowed);
    new_obj.add_child("SRGInformationValid", spatial_reuse_params.srg_information_valid);
    new_obj.add_child("NonSRGOffsetValid", spatial_reuse_params.non_srg_offset_valid);
    new_obj.add_child("PSRDisallowed", spatial_reuse_params.psr_disallowed);

    if (spatial_reuse_params.non_srg_offset_valid) {
        new_obj.add_child("NonSRGOBSSPDMaxOffset", spatial_reuse_params.non_srg_obsspd_max_offset);
    }

    if (spatial_reuse_params.srg_information_valid) {
        new_obj.add_child("SRGOBSSPDMinOffset", spatial_reuse_params.srg_obsspd_min_offset);
        new_obj.add_child("SRGOBSSPDMaxOffset", spatial_reuse_params.srg_obsspd_max_offset);
        new_obj.add_child("SRGBSSColorBitmap",
                          get_bss_color_bitmap(spatial_reuse_params.srg_bss_color_bitmap));
        new_obj.add_child("SRGPartialBSSIDBitmap",
                          get_bss_color_bitmap(spatial_reuse_params.srg_partial_bssid_bitmap));
    }
    if (!m_ambiorix_cl.update_object(path_to_80211ax, new_obj)) {
        LOG(ERROR) << "Could not set spatial reuse parameters for " << path_to_80211ax;
        return false;
    }

    return true;
}

bool ap_wlan_hal_whm::get_spatial_reuse_config(
    son::wireless_utils::sSpatialReuseParams &spatial_reuse_params)
{
    std::string path_to_80211ax = m_radio_path + "IEEE80211ax.";
    std::string string_bss_color_bitmap;
    std::string string_partial_bssid_bitmap;

    LOG(WARNING) << "get_spatial_reuse_config. path_to_80211ax" << path_to_80211ax;
    m_ambiorix_cl.get_param<>(spatial_reuse_params.bss_color, path_to_80211ax, "BssColor");
    m_ambiorix_cl.get_param<>(spatial_reuse_params.partial_bss_color, path_to_80211ax,
                              "BssColorPartial");
    m_ambiorix_cl.get_param<>(spatial_reuse_params.hesiga_spatial_reuse_value15_allowed,
                              path_to_80211ax, "HESIGASpatialReuseValue15Allowed");
    m_ambiorix_cl.get_param<>(spatial_reuse_params.srg_information_valid, path_to_80211ax,
                              "SRGInformationValid");
    m_ambiorix_cl.get_param<>(spatial_reuse_params.non_srg_offset_valid, path_to_80211ax,
                              "NonSRGOffsetValid");
    m_ambiorix_cl.get_param<>(spatial_reuse_params.psr_disallowed, path_to_80211ax,
                              "PSRDisallowed");
    m_ambiorix_cl.get_param<>(spatial_reuse_params.non_srg_obsspd_max_offset, path_to_80211ax,
                              "NonSRGOBSSPDMaxOffset");
    m_ambiorix_cl.get_param<>(spatial_reuse_params.srg_obsspd_min_offset, path_to_80211ax,
                              "SRGOBSSPDMinOffset");
    m_ambiorix_cl.get_param<>(spatial_reuse_params.srg_obsspd_max_offset, path_to_80211ax,
                              "SRGOBSSPDMaxOffset");
    m_ambiorix_cl.get_param<>(string_bss_color_bitmap, path_to_80211ax, "SRGBSSColorBitmap");
    m_ambiorix_cl.get_param<>(string_partial_bssid_bitmap, path_to_80211ax,
                              "SRGPartialBSSIDBitmap");
    spatial_reuse_params.srg_bss_color_bitmap = get_uint64_from_bss_string(string_bss_color_bitmap);
    spatial_reuse_params.srg_partial_bssid_bitmap =
        get_uint64_from_bss_string(string_partial_bssid_bitmap);

    LOG(INFO) << "Get spatial reuse parameters. bss_color: " << spatial_reuse_params.bss_color
              << " partial_bss_color: " << spatial_reuse_params.partial_bss_color
              << " string_bss_color_bitmap: " << string_bss_color_bitmap
              << " string_partial_bssid_bitmap: " << string_partial_bssid_bitmap;
    return true;
}

bool ap_wlan_hal_whm::update_mld_mode(std::string ssid, uint8_t mld_mode)
{
    LOG(DEBUG) << "Not implemented yet";

    return true;
}

bool ap_wlan_hal_whm::update_mld_unit(std::string ssid, int8_t mld_unit)
{
    std::string radio_path_no_dot = m_radio_path;
    if (radio_path_no_dot.back() == '.') {
        radio_path_no_dot.pop_back();
    }

    std::string search_path =
        wbapi_utils::search_path_ssid_by_ssid_and_radio(ssid, radio_path_no_dot);
    LOG(DEBUG) << "Search SSID: " << ssid << " for Radio: " << radio_path_no_dot
               << " returned paths:\n"
               << search_path;

    auto ssids = m_ambiorix_cl.get_object_multi<AmbiorixVariantMapSmartPtr>(search_path);
    if (!ssids || ssids->empty()) {
        LOG(ERROR) << "No SSID instances found with name: " << ssid
                   << " for radio: " << m_radio_path;
        return false;
    }

    LOG(DEBUG) << "Found " << ssids->size() << " SSID instance(s) matching SSID and radio";

    AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
    new_obj.add_child("MLDUnit", mld_unit);

    auto it                      = ssids->begin();
    const std::string &ssid_path = it->first;

    if (!m_ambiorix_cl.update_object(ssid_path, new_obj)) {
        LOG(ERROR) << "Failed to update MLDUnit for SSID: " << ssid << ", ssid_path: " << ssid_path
                   << ", MLDUnit value: " << static_cast<int>(mld_unit);
        return false;
    }

    LOG(INFO) << " Successfully updated MLDUnit. SSID: " << ssid
              << ", MLDUnit: " << static_cast<int>(mld_unit) << ", Path: " << ssid_path;

    LOG_IF(ssids->size() > 1, WARNING)
        << "Found " << ssids->size()
        << " SSID instances with same name on same radio (misconfiguration) "
        << "Only first SSID updated";
    return true;
}

void ap_wlan_hal_whm::process_rssi_eventing_event(const std::string &interface,
                                                  beerocks::wbapi::AmbiorixVariant *updates)
{
    auto vap_id = get_vap_id_with_bss(interface);

    if (updates == nullptr || updates->empty()) {
        return;
    }
    auto updates_list = updates->read_children<AmbiorixVariantListSmartPtr>();
    if (!updates_list) {
        return;
    }

    // list of hash_tables
    for (auto &update : *updates_list) { //update is a map
        auto station_map = update.read_children<AmbiorixVariantMapSmartPtr>();
        if (!station_map) {
            continue;
        }

        auto real_map = *station_map;

        std::string mac_address = real_map["MACAddress"];

        if (m_unassociated_stations.find(mac_address) != m_unassociated_stations.end()) {

            auto msg_buff =
                ALLOC_SMART_BUFFER(sizeof(sACTION_APMANAGER_CLIENT_RX_RSSI_MEASUREMENT_RESPONSE));
            auto msg = reinterpret_cast<sACTION_APMANAGER_CLIENT_RX_RSSI_MEASUREMENT_RESPONSE *>(
                msg_buff.get());
            LOG_IF(!msg, FATAL) << "Memory allocation failed!";

            // Initialize the message
            memset(msg_buff.get(), 0,
                   sizeof(sACTION_APMANAGER_CLIENT_RX_RSSI_MEASUREMENT_RESPONSE));

            msg->params.rx_rssi = real_map["SignalStrength"];

            msg->params.rx_snr     = beerocks::SNR_INVALID;
            msg->params.result.mac = tlvf::mac_from_string(mac_address);
            msg->params.vap_id     = vap_id;

            event_queue_push(Event::STA_Unassoc_RSSI, msg_buff);

            //Rssi consumed --> lets remove the unassociated station
            m_unassociated_stations.erase(mac_address);
        }
    }
    if (m_unassociated_stations.empty()) {
        m_ambiorix_cl.unsubscribe_from_object_event(m_rssi_event_handler);
    }
}

bool ap_wlan_hal_whm::start_platform_acs(const std::shared_ptr<airties::cACSChannelList> &acs_list)
{
    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    AmbiorixVariant args_list(AMXC_VAR_ID_LIST);

    for (size_t i = 0; i < acs_list->acs_list_length(); ++i) {
        auto acs_list_tuple_entry = acs_list->acs_list(i);
        if (!std::get<0>(acs_list_tuple_entry)) {
            LOG(ERROR) << "Failed to get ACS list";
            return false;
        }
        auto &acs_list_entry = std::get<1>(acs_list_tuple_entry);

        if (!args_list.add(acs_list_entry.opclass(), acs_list_entry.exclude_channels_length(),
                           acs_list_entry.exclude_channels(0))) {
            return false;
        }
    }

    args.add_child<AmbiorixVariant &>("acs_list", args_list);

    std::string wifi_ap_path = wbapi_utils::search_path_radio_by_iface(m_radio_info.iface_name);
    bool ret                 = m_ambiorix_cl.call(wifi_ap_path, "startPlatformACS", args, result);
    if (!ret) {
        LOG(ERROR) << "startPlatformACS() failed!";
        return false;
    }

    return true;
}

bool ap_wlan_hal_whm::change_radio_mode_config(
    const airties::tlvAirtiesRadioCapability::sStandards &operating_standards)
{
    if (m_radio_path.empty()) {
        m_ambiorix_cl.resolve_path(wbapi_utils::search_path_radio_by_iface(m_radio_info.iface_name),
                                   m_radio_path);
    }

    std::string op_std_format;
    if (!m_ambiorix_cl.get_param(op_std_format, m_radio_path, "OperatingStandardsFormat")) {
        LOG(ERROR) << "Cannot read OperatingStandardsFormat for " << m_radio_path;
        return false;
    }

    auto op_std_str =
        [&](const airties::tlvAirtiesRadioCapability::sStandards &op_std) -> std::string {
        std::vector<std::string> standards;

        if (op_std.s_80211be)
            standards.push_back("be");
        if (op_std.s_80211ax)
            standards.push_back("ax");
        if (op_std.s_80211ac)
            standards.push_back("ac");
        if (op_std.s_80211n)
            standards.push_back("n");
        if (op_std.s_80211g)
            standards.push_back("g");
        if (op_std.s_80211b)
            standards.push_back("b");
        if (op_std.s_80211a)
            standards.push_back("a");

        if (op_std_format == "Legacy" && !standards.empty()) {
            return standards.front();
        }

        return std::accumulate(standards.rbegin(), standards.rend(), std::string(),
                               [](const std::string &lhs, const std::string &rhs) {
                                   return lhs.empty() ? rhs : lhs + "," + rhs;
                               });
    }(operating_standards);

    if (op_std_str.empty()) {
        return false;
    }

    std::string current_standards;
    if (!m_ambiorix_cl.get_param(current_standards, m_radio_path, "OperatingStandards")) {
        LOG(ERROR) << "Cannot read OperatingStandards for " << m_radio_path;
        return false;
    }

    if (current_standards != op_std_str) {
        AmbiorixVariant new_obj(AMXC_VAR_ID_HTABLE);
        new_obj.add_child("OperatingStandards", op_std_str);
        if (!m_ambiorix_cl.update_object(m_radio_path, new_obj)) {
            LOG(ERROR) << "Could not set OperatingStandards for " << m_radio_path;
            return false;
        }
        refresh_radio_info();
        event_queue_push(Event::AP_Attached);
    }

    return true;
}

} // namespace whm

std::shared_ptr<ap_wlan_hal> ap_wlan_hal_create(std::string iface_name, bwl::hal_conf_t hal_conf,
                                                base_wlan_hal::hal_event_cb_t callback)
{
    return std::make_shared<whm::ap_wlan_hal_whm>(iface_name, callback, hal_conf);
}

} // namespace bwl
