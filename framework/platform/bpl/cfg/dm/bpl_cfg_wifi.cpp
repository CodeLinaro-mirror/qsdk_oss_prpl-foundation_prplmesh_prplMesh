/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../common/utils/utils.h"
#include "../common/utils/utils_net.h"

#include <bcl/beerocks_string_utils.h>
#include <bpl/bpl_cfg.h>

#include <mapf/common/logger.h>
#include <mapf/common/utils.h>

#include <tlvf/WSC/eWscAuth.h>
#include <tlvf/WSC/eWscEncr.h>

#include <tlvf/common/eVapType.h>

#include "wbapi_utils.h"

#include "bpl_cfg_service_helper.h"
#include "bpl_cfg_status.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

using namespace mapf;
using namespace beerocks;
using namespace wbapi;

namespace {
eFreqType string_to_freq_type(const std::string &freq_str)
{
    static const std::unordered_map<std::string, eFreqType> freq_map = {
        {"2.4GHz", FREQ_24G}, {"5GHz", FREQ_5G}, {"6GHz", FREQ_6G}};

    auto it = freq_map.find(freq_str);
    return it != freq_map.end() ? it->second : FREQ_UNKNOWN;
}
} // namespace

namespace beerocks {
namespace bpl {

static bool bpl_cfg_get_radio_reference_path(const AmbiorixVariant &object,
                                             std::string &reference_path)
{
    reference_path.clear();

    static const std::string radio_reference_param("RadioReference");
    std::string value;
    if (!object.read_child(value, radio_reference_param) || value.empty()) {
        return false;
    }

    // prplMesh uses paths without the "Device." prefix for both common and direct socket
    // connections, so strip it from the returned reference.
    value = normalize_path(std::move(value));
    if (value.empty()) {
        return false;
    }
    if (value.back() != '.') {
        value.push_back('.');
    }

    reference_path = std::move(value);
    return true;
}

bool bpl_cfg_get_wifi_radio_temperature(const std::string &iface_name, uint8_t &radio_temperature)
{
    return read_param_via_common_socket(wbapi_utils::search_path_radio_by_iface(iface_name) +
                                            "Stats.",
                                        "Temperature", radio_temperature);
}

static bool bpl_cfg_read_wifi_credentials(const AmbiorixVariant &ssid_obj,
                                          const AmbiorixVariant &ap_sec_obj,
                                          son::wireless_utils::sBssInfoConf &configuration)
{
    std::string bssid;
    ssid_obj.read_child(bssid, "MACAddress");
    std::transform(bssid.begin(), bssid.end(), bssid.begin(), ::tolower);
    configuration.bssid = tlvf::mac_from_string(bssid);

    ssid_obj.read_child(configuration.ssid, "SSID");

    std::string mode_enabled;
    if (ap_sec_obj.read_child(mode_enabled, "ModeEnabled")) {
        configuration.authentication_type = wbapi_utils::security_mode_from_string(mode_enabled);
        configuration.additional_auth = wbapi_utils::security_rsn_mode_from_string(mode_enabled);
    }

    std::string encryption_mode;
    if (ap_sec_obj.read_child(encryption_mode, "EncryptionMode")) {
        if (encryption_mode == "Default") {
            configuration.encryption_type =
                wbapi_utils::encryption_type_from_auth(configuration.authentication_type);
        } else {
            configuration.encryption_type =
                wbapi_utils::encryption_type_from_string(encryption_mode);
        }
    }

    std::string key_pass_phrase;
    if (ap_sec_obj.read_child(key_pass_phrase, "KeyPassPhrase")) {
        configuration.network_key = std::move(key_pass_phrase);
    }

    return true;
}

int cfg_get_all_prplmesh_wifi_interfaces(BPL_WLAN_IFACE *interfaces, int *num_of_interfaces)
{
    if (!interfaces) {
        MAPF_ERR("cfg_get_all_prplmesh_wifi_interfaces: invalid input: interfaces is nullptr");
        return RETURN_ERR;
    }
    if (!num_of_interfaces) {
        MAPF_ERR(
            "cfg_get_all_prplmesh_wifi_interfaces: invalid input: num_of_interfaces is nullptr");
        return RETURN_ERR;
    }
    if (*num_of_interfaces < 1) {
        MAPF_ERR(
            "cfg_get_all_prplmesh_wifi_interfaces: invalid input: max num_of_interfaces value < 1");
        return RETURN_ERR;
    }

    int interfaces_count = 0;

    // pwhm dm path: WiFi.Radio.*
    auto radios = get_object_multi_via_common_socket(wbapi_utils::search_path_radio());
    if (radios) {
        for (auto const &it : *radios) {
            if (interfaces_count >= *num_of_interfaces) {
                MAPF_WARN("cfg_get_all_prplmesh_wifi_interfaces: interface buffer is full");
                break;
            }
            auto &radio = it.second;

            // Getting ifname
            std::string ifname;
            if (!radio.read_child(ifname, "Name") || ifname.empty()) {
                std::string radio_status;
                if (radio.read_child(radio_status, "Status") && radio_status == "NotPresent") {
                    LOG(DEBUG) << "cfg_get_all_prplmesh_wifi_interfaces: skip not-present radio "
                               << it.first;
                    continue;
                }
                MAPF_ERR(
                    "cfg_get_all_prplmesh_wifi_interfaces: failed to get radio iface for radio " +
                    std::to_string(interfaces_count));
                continue;
            }
            mapf::utils::copy_string(interfaces[interfaces_count].ifname, ifname.c_str(), IFNAMSIZ);
            interfaces[interfaces_count].radio_num = interfaces_count;

            // Getting freq band
            std::string freq_band_str;
            if (!radio.read_child(freq_band_str, "OperatingFrequencyBand") ||
                freq_band_str.empty()) {
                MAPF_ERR(
                    "cfg_get_all_prplmesh_wifi_interfaces: failed to get freq band for radio " +
                    std::to_string(interfaces_count));
                continue;
            }
            interfaces[interfaces_count].freq_type = string_to_freq_type(freq_band_str);

            interfaces_count++;
        }
    }

    *num_of_interfaces = interfaces_count;

    return RETURN_OK;
}

int cfg_get_wifi_params(const std::string &iface, struct BPL_WLAN_PARAMS *wlan_params)
{
    if (iface.empty() || !wlan_params) {
        MAPF_ERR("cfg_get_wifi_params: invalid input: iface = " << iface << " wlan_params = "
                                                                << intptr_t(wlan_params));
        return RETURN_ERR;
    }

    auto radio_obj = get_object_via_common_socket(wbapi_utils::search_path_radio_by_iface(iface));
    if (!radio_obj) {
        return RETURN_ERR;
    }

    radio_obj->read_child(wlan_params->enabled, "Enable");
    radio_obj->read_child(wlan_params->channel, "Channel");

    bool ieee80211h_supported = false;
    radio_obj->read_child(ieee80211h_supported, "IEEE80211hSupported");
    bool ieee80211h_enabled = false;
    radio_obj->read_child(ieee80211h_enabled, "IEEE80211hEnabled");
    wlan_params->sub_band_dfs = ieee80211h_supported && ieee80211h_enabled;

    std::string country_code;
    if (radio_obj->read_child(country_code, "RegulatoryDomain")) {
        wlan_params->country_code[0] = country_code.at(0);
        wlan_params->country_code[1] = country_code.at(1);
        wlan_params->country_code[2] = '\0';
    }

    return RETURN_OK;
}

int cfg_get_wifi_universal_index(const std::string &iface, int &index)
{
    index = -1;

    std::string radio_path;
    if (!resolve_path_via_common_socket(wbapi_utils::search_path_radio_by_iface(iface),
                                        radio_path)) {
        return RETURN_ERR;
    }

    const size_t pos = radio_path.find_last_of('.');
    if (pos == std::string::npos || pos + 1 >= radio_path.size())
        return RETURN_ERR;

    const std::string suffix = radio_path.substr(pos + 1);

    int value = 0;
    for (char c : suffix) {
        if (c < '0' || c > '9')
            return RETURN_ERR;

        value = value * 10 + (c - '0');
    }

    index = value - 1;

    return RETURN_OK;
}

bool bpl_cfg_get_wireless_settings(std::list<son::wireless_utils::sBssInfoConf> &wireless_settings)
{
    auto aps = get_object_multi_via_common_socket(wbapi_utils::search_path_ap());
    if (!aps) {
        return false;
    }

    // TODO: centralize bss_index generation and propagation across different TLVs (PPM-3625)
    uint8_t bss_index_generator = 1;
    for (auto const &it : *aps) {
        const auto &ap = it.second;
        std::string iface;
        if (!ap.read_child(iface, "Alias") || iface.empty()) {
            continue;
        }

        son::wireless_utils::sBssInfoConf configuration;
        std::string radio_path;
        const bool has_radio_path = bpl_cfg_get_radio_reference_path(ap, radio_path);
        auto radio_obj =
            has_radio_path ? get_object_via_common_socket(radio_path) : AmbiorixVariantSmartPtr{};
        if (has_radio_path) {
            std::string band_str;
            if (radio_obj && radio_obj->read_child(band_str, "OperatingFrequencyBand")) {
                band_str = wbapi_utils::band_short_name(band_str);
            }
            configuration.operating_class = son::wireless_utils::string_to_wsc_oper_class(band_str);
        }

        std::string multi_ap_type_str;
        if (ap.read_child(multi_ap_type_str, "MultiAPType")) {
            if (multi_ap_type_str.find("FronthaulBSS") != std::string::npos) {
                configuration.fronthaul = true;
            }
            if (multi_ap_type_str.find("BackhaulBSS") != std::string::npos) {
                configuration.backhaul = true;
            }
        }

        int8_t mld_id = DISABLED_MLDUNIT;
        auto ssid_obj = get_object_via_common_socket(wbapi_utils::search_path_ssid_by_iface(iface));
        if (!ssid_obj || !ssid_obj->read_child(mld_id, "MLDUnit")) {
            LOG(ERROR) << "failed to read MLDUnit from SSID object of iface " << iface;
        }

        configuration.mld_id = std::to_string(mld_id);

        // Read CustomAlias separately because the AP multi-object snapshot can be incomplete
        // during startup and omit this parameter even when it is available on the common bus.
        std::string custom_alias;
        if (!read_param_via_common_socket(it.first, "CustomAlias", custom_alias)) {
            LOG(ERROR) << "bpl_cfg_get_wireless_settings: Failed to read CustomAlias";
        } else if (custom_alias.empty()) {
            LOG(WARNING) << "bpl_cfg_get_wireless_settings: CustomAlias is empty";
        }
        configuration.vap_type = wbapi_utils::vap_type_from_custom_alias(custom_alias);
        LOG(DEBUG) << "bpl_cfg_get_wireless_settings: vap_type is "
                   << eVapType_str(configuration.vap_type) << " for SSID=" << configuration.ssid;

        // Reading Enable of the Radio associated with AP
        bool radio_enable;
        if (!has_radio_path || !radio_obj || !radio_obj->read_child(radio_enable, "Enable")) {
            radio_enable = true;
        }

        bool ap_enable = false;
        ap.read_child(ap_enable, "Enable");
        bool credentials_ok = false;
        if (ap_enable && radio_enable) {
            if (!ssid_obj) {
                LOG(ERROR) << "Failed to get ssid obj of iface " << iface;
            } else {
                auto ap_sec_obj = get_object_via_common_socket(
                    wbapi_utils::search_path_ap_by_iface(iface) + "Security.");
                credentials_ok = ap_sec_obj && bpl_cfg_read_wifi_credentials(*ssid_obj, *ap_sec_obj,
                                                                             configuration);
            }
        }

        if (credentials_ok) {
            LOG(DEBUG) << "add " << configuration.ssid << " to wireless settings size "
                       << wireless_settings.size() << " path " << it.first;
            configuration.bss_index = bss_index_generator++;

            bool ssid_advertisement_enabled = true;
            ap.read_child(ssid_advertisement_enabled, "SSIDAdvertisementEnabled");
            configuration.hidden_ssid = ssid_advertisement_enabled
                                            ? WSC::eWscVendorExtHiddenSsid::DISABLED
                                            : WSC::eWscVendorExtHiddenSsid ::ENABLED;

            // Add AKM24 in case of MLD for SAE
            if (mld_id != DISABLED_MLDUNIT) {
                if (configuration.authentication_type & WSC::eWscAuth::WSC_AUTH_SAE) {
                    configuration.authentication_type = WSC::eWscAuth(
                        configuration.authentication_type | WSC::eWscAuth::WSC_AUTH_SAE_AKM24);
                }
            }

            wireless_settings.push_back(configuration);
        } else {
            LOG(DEBUG) << " ap " << it.first << " is disabled";
        }
    }

    return true;
}

bool bpl_cfg_get_wifi_credentials(const std::string &iface,
                                  son::wireless_utils::sBssInfoConf &configuration)
{
    auto ssid_obj = get_object_via_common_socket(wbapi_utils::search_path_ssid_by_iface(iface));
    if (!ssid_obj) {
        LOG(ERROR) << "Failed to get ssid obj of iface " << iface;
        return false;
    }

    auto ap_sec_obj =
        get_object_via_common_socket(wbapi_utils::search_path_ap_by_iface(iface) + "Security.");
    if (!ap_sec_obj) {
        return false;
    }

    return bpl_cfg_read_wifi_credentials(*ssid_obj, *ap_sec_obj, configuration);
}

bool bpl_cfg_get_mld_info_config(const std::string &ssid, int8_t mld_id,
                                 son::wireless_utils::sMldInfoConf &mld_info_config)
{
    if (mld_id == DISABLED_MLDUNIT) {
        LOG(ERROR) << "Trying to read MLD configuration with mld_id=DISABLED_MLDUNIT";
        return false;
    }

    mld_info_config.ssid  = ssid;
    mld_info_config.str   = true;
    mld_info_config.nstr  = true;
    mld_info_config.emlsr = true;
    mld_info_config.emlmr = true;

    std::string apmld_path;
    resolve_path_via_common_socket(wbapi_utils::search_path_apmld_by_mldid(mld_id), apmld_path);
    if (apmld_path.empty()) {
        LOG(ERROR) << "Failed to resolve path of APMLD with MLDID of " << (int)mld_id
                   << ", using default values instead";
        return true;
    }

    const auto apmld_config_path = apmld_path + "APMLDConfig.";
    auto apmld_config_obj        = get_object_via_common_socket(apmld_config_path);
    if (!apmld_config_obj) {
        LOG(ERROR) << "Failed to read " << apmld_config_path;
        return true;
    }

    if (!apmld_config_obj->read_child(mld_info_config.str, "STREnabled")) {
        LOG(ERROR) << "Failed to read " << apmld_path << "APMLDConfig.STREnabled";
    }

    if (!apmld_config_obj->read_child(mld_info_config.nstr, "NSTREnabled")) {
        LOG(ERROR) << "Failed to read " << apmld_path << "APMLDConfig.NSTREnabled";
    }

    if (!apmld_config_obj->read_child(mld_info_config.emlsr, "EMLSREnabled")) {
        LOG(ERROR) << "Failed to read " << apmld_path << "APMLDConfig.EMLSREnabled";
    }

    if (!apmld_config_obj->read_child(mld_info_config.emlmr, "EMLMREnabled")) {
        LOG(ERROR) << "Failed to read " << apmld_path << "APMLDConfig.EMLMREnabled";
    }

    return true;
}

int cfg_get_sta_iface(const std::string &iface, std::string &sta_iface)
{
    // Get the current radio reference for the given iface
    std::string radio_path;
    if (!resolve_path_via_common_socket(wbapi_utils::search_path_radio_by_iface(iface),
                                        radio_path)) {
        return RETURN_ERR;
    }

    // Find the endpoint that its radioreference is the current one
    auto result = get_object_multi_via_common_socket(wbapi_utils::search_path_ep());
    if (!result) {
        return RETURN_ERR;
    }
    std::string ep_radio_path;
    for (auto &it : *result) {
        auto &ep = it.second;
        std::string ep_radio_reference_path;
        if ((ep.empty()) || !bpl_cfg_get_radio_reference_path(ep, ep_radio_reference_path) ||
            !resolve_path_via_common_socket(ep_radio_reference_path, ep_radio_path) ||
            (ep_radio_path != radio_path) || !ep.read_child(sta_iface, "IntfName")) {
            continue;
        }

        // The sta iface is the value of the endpoint IntfName field
        // Ex: WiFi.EndPoint.ep5g0.IntfName="wlan1"
        return RETURN_OK;
    }

    return RETURN_ERR;
}

int cfg_get_hostap_iface(int32_t radio_num, std::string &hostap_iface)
{
    if (radio_num < 0) {
        MAPF_ERR("cfg_get_hostap_iface: invalid input: radio_num < 0");
        return RETURN_ERR;
    }

    beerocks::bpl::BPL_WLAN_IFACE interfaces[beerocks::MAX_RADIOS_PER_AGENT] = {0};
    int num_of_interfaces = beerocks::MAX_RADIOS_PER_AGENT;
    if (cfg_get_all_prplmesh_wifi_interfaces(interfaces, &num_of_interfaces)) {
        MAPF_ERR("ERROR: Failed to read interfaces map");
        return RETURN_ERR;
    }
    for (int i = 0; i < num_of_interfaces; i++) {
        if (interfaces[i].radio_num == radio_num) {
            hostap_iface = std::string(interfaces[i].ifname);
            return RETURN_OK;
        }
    }
    return RETURN_ERR;
}

} // namespace bpl
} // namespace beerocks
