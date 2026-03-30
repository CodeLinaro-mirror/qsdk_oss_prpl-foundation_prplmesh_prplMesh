/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "nl80211_client_whm.h"

#include <bcl/beerocks_utils.h>
#include <bcl/network/network_utils.h>

// Ambiorix
#include "ambiorix_connection_manager.h"
#include "wbapi_utils.h"

using namespace beerocks;
using namespace wbapi;

namespace bwl {

nl80211_client_whm::nl80211_client_whm()
    : m_connection(AmbiorixConnectionManager::get_instance()->get_connection(
          AMBIORIX_USP_BACKEND_PATH, AMBIORIX_PWHM_USP_BACKEND_URI))
{
}

bool nl80211_client_whm::get_interfaces(std::vector<std::string> &interfaces)
{
    interfaces.clear();
    if (!m_connection) {
        return false;
    }
    // pwhm dm path: WiFi.SSID.*.Name?
    auto ssids = m_connection->get_object(wbapi_utils::search_path_ssid_iface(), 0, false);
    if (!ssids) {
        return false;
    }
    auto ssids_map = ssids->read_children<AmbiorixVariantMapSmartPtr>();
    if (!ssids_map) {
        return false;
    }
    for (auto const &it : *ssids_map) {
        auto &ssid  = it.second;
        auto ifname = wbapi_utils::get_ssid_iface(ssid);
        if (ifname.empty()) {
            continue;
        }
        interfaces.push_back(ifname);
    }
    return true;
}

bool nl80211_client_whm::get_interface_info(const std::string &interface_name,
                                            interface_info &interface_info)
{
    return false;
}

bool nl80211_client_whm::get_radio_info(const std::string &interface_name, radio_info &radio_info)
{
    return false;
}

bool nl80211_client_whm::get_sta_info(const std::string &interface_name,
                                      const sMacAddr &sta_mac_address, sta_info &sta_info)
{
    if (!m_connection) {
        return false;
    }

    std::string sta_mac_str = tlvf::mac_to_string(sta_mac_address);

    std::string assoc_device_search_path =
        wbapi_utils::search_path_assocDev_by_mac(interface_name, sta_mac_str);
    std::vector<std::string> assoc_device_path_list;
    m_connection->resolve_path(assoc_device_search_path, assoc_device_path_list);
    if (assoc_device_path_list.size() == 1) {
        AmbiorixVariantSmartPtr assoc_device_obj =
            m_connection->get_object(assoc_device_path_list.front(), 0, true);
        if (assoc_device_obj) {
            get_sta_info_from_assoc_device(assoc_device_obj, sta_info);
            return true;
        }
    } else if (!assoc_device_path_list.empty()) {
        LOG(ERROR) << "Search path resolved to several objects: " << assoc_device_search_path;
        return false;
    }

    std::string end_point_search_path = wbapi_utils::search_path_ep_by_iface(interface_name);
    std::vector<std::string> end_point_path_list;
    m_connection->resolve_path(end_point_search_path, end_point_path_list);
    if (end_point_path_list.size() == 1) {
        AmbiorixVariantSmartPtr end_point_obj =
            m_connection->get_object(end_point_path_list.front(), 0, true);
        if (end_point_obj) {
            get_sta_info_from_end_point(end_point_obj, sta_info);
            return true;
        }
    } else if (!end_point_path_list.empty()) {
        LOG(ERROR) << "Search path resolved to several objects: " << end_point_search_path;
        return false;
    }

    LOG(ERROR) << "Failed to get both AssociatedDevice and EndPoint objects for iface="
               << interface_name << " mac=" << sta_mac_address;
    return false;
}

bool nl80211_client_whm::get_survey_info(const std::string &interface_name, SurveyInfo &survey_info)
{
    return false;
}

bool nl80211_client_whm::set_tx_power_limit(const std::string &interface_name, uint32_t limit)
{
    return false;
}

bool nl80211_client_whm::get_tx_power_dbm(const std::string &interface_name, uint32_t &power)
{
    return false;
}

bool nl80211_client_whm::channel_scan_abort(const std::string &interface_name) { return false; }

bool nl80211_client_whm::add_key(const std::string &interface_name, const sKeyInfo &key_info)
{
    return false;
}

bool nl80211_client_whm::add_station(const std::string &interface_name, const sMacAddr &mac,
                                     assoc_frame::AssocReqFrame &assoc_req, uint16_t aid)
{
    return false;
}

bool nl80211_client_whm::get_key(const std::string &interface_name, sKeyInfo &key_info)
{
    return false;
}

bool nl80211_client_whm::send_delba(const std::string &interface_name, const sMacAddr &dst,
                                    const sMacAddr &src, const sMacAddr &bssid)
{
    return false;
}

void nl80211_client_whm::get_sta_info_from_assoc_device(
    beerocks::wbapi::AmbiorixVariantSmartPtr &assoc_device, sta_info &sta_info)
{
    assoc_device->read_child(sta_info.tx_bytes, "TxBytes");
    assoc_device->read_child(sta_info.rx_bytes, "RxBytes");
    assoc_device->read_child(sta_info.tx_packets, "TxPacketCount");
    assoc_device->read_child(sta_info.rx_packets, "RxPacketCount");
    assoc_device->read_child(sta_info.tx_retries, "Tx_Retransmissions");
    assoc_device->read_child(sta_info.tx_failed, "TxErrors");
    assoc_device->read_child(sta_info.signal_dbm, "SignalStrength");
    assoc_device->read_child(sta_info.signal_avg_dbm, "AvgSignalStrength");

    uint32_t u32Val;
    if (assoc_device->read_child(u32Val, "LastDataDownlinkRate")) {
        sta_info.rx_bitrate_100kbps = u32Val / 100;
    }
    if (assoc_device->read_child(u32Val, "LastDataUplinkRate")) {
        sta_info.tx_bitrate_100kbps = u32Val / 100;
    }

    uint32_t dl_bandwidth;
    assoc_device->read_child(dl_bandwidth, "DownlinkBandwidth");
    sta_info.dl_bandwidth = wbapi_utils::bandwith_from_string(std::to_string(dl_bandwidth) + "MHz");
}

void nl80211_client_whm::get_sta_info_from_end_point(
    beerocks::wbapi::AmbiorixVariantSmartPtr &end_point, sta_info &sta_info)
{
    end_point->read_child(sta_info.tx_bytes, "TxBytes");
    end_point->read_child(sta_info.rx_bytes, "RxBytes");
    end_point->read_child(sta_info.tx_packets, "TxPacketCount");
    end_point->read_child(sta_info.rx_packets, "RxPacketCount");
    end_point->read_child(sta_info.tx_retries, "Tx_Retransmissions");
    sta_info.tx_failed = 0;
    end_point->read_child(sta_info.signal_dbm, "SignalStrength");
    sta_info.signal_avg_dbm = sta_info.signal_dbm;

    uint32_t u32Val;
    if (end_point->read_child(u32Val, "LastDataDownlinkRate")) {
        sta_info.tx_bitrate_100kbps = u32Val / 100;
    }
    if (end_point->read_child(u32Val, "LastDataUplinkRate")) {
        sta_info.rx_bitrate_100kbps = u32Val / 100;
    }

    std::string link_bandwidth;
    end_point->read_child(link_bandwidth, "LinkBandwidth");
    sta_info.dl_bandwidth = wbapi_utils::bandwith_from_string(link_bandwidth);
}

} // namespace bwl
