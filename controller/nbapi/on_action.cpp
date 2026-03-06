/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "on_action.h"

#include <bcl/beerocks_defines.h>
#include <beerocks/tlvf/beerocks_message_bml.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <locale.h>
#include <sstream>
#include <time.h>
#include <unordered_map>

using namespace beerocks;
using namespace net;
using namespace son;
namespace prplmesh {
namespace controller {
namespace actions {

// Actions

son::db *g_database = nullptr;

/*
** Set the number of seconds since this Associated Device was last attempted to be steered.
*/
static amxd_status_t action_last_steer_time(amxd_object_t *object, amxd_param_t *param,
                                            amxd_action_t reason, const amxc_var_t *const args,
                                            amxc_var_t *const retval, void *priv)
{
    /*
        This action retrieves timestamp of last steering attempt of Associated Device
        from LastSteerTime parameter.
        Then, we get сurrent time and subtract retrieved timestamp for getting time passed
        from last steering attempt in seconds.
    */
    if (reason != action_param_read) {
        return amxd_status_function_not_implemented;
    }
    if (!param) {
        return amxd_status_parameter_not_found;
    }

    auto status = amxd_action_param_read(object, param, reason, args, retval, priv);
    if (status != amxd_status_ok) {
        return status;
    }

    auto last_steer_timestamp = amxc_var_dyncast(uint32_t, retval);
    // If LastSteerTime has not been set yet
    if (!last_steer_timestamp) {
        return amxd_status_ok;
    }

    auto current_time =
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count());

    uint32_t last_steer_time = current_time - last_steer_timestamp;

    amxc_var_set(uint32_t, retval, last_steer_time);

    return amxd_status_ok;
}

/*
** Set (LastConnectTime) the number of seconds since station is associated. It is created from assoc. time of station object
*/
static amxd_status_t action_read_assoc_time(amxd_object_t *object, amxd_param_t *param,
                                            amxd_action_t reason, const amxc_var_t *const args,
                                            amxc_var_t *const retval, void *priv)
{

    if (reason != action_param_read) {
        return amxd_status_function_not_implemented;
    }
    if (!param) {
        return amxd_status_parameter_not_found;
    }

    auto status = amxd_action_param_read(object, param, reason, args, retval, priv);
    if (status != amxd_status_ok) {
        return status;
    }

    // Initialization of ambiorix triggers read action and it leads un-initalized 1db object
    if (!g_database) {
        return amxd_status_unknown_error;
    }

    amxd_param_t *mac_param = amxd_object_get_param_def(object, "MACAddress");
    if (mac_param == nullptr) {
        LOG(ERROR) << "MACAddress can not be read in STA datamodel";
        return amxd_status_parameter_not_found;
    }
    auto sta_mac = tlvf::mac_from_string(amxc_var_constcast(cstring_t, &mac_param->value));

    // Initialization of ambiorix objects triggers read action and it leads un-initalized values
    if (sta_mac == beerocks::net::network_utils::ZERO_MAC) {
        return amxd_status_object_not_found;
    }

    auto station = g_database->get_station(sta_mac);
    if (!station) {
        LOG(ERROR) << "Station is not found on db with mac: " << sta_mac;
        return amxd_status_object_not_found;
    }

    if (station->assoc_timestamp.empty()) {
        amxc_var_set(uint64_t, retval, 0);
        return amxd_status_ok;
    }

    amxc_ts_t ts_assoc, ts_now;
    amxc_ts_parse(&ts_assoc, station->assoc_timestamp.c_str(), station->assoc_timestamp.length());
    amxc_ts_now(&ts_now);

    amxc_var_set(uint64_t, retval, (ts_now.sec - ts_assoc.sec));
    return amxd_status_ok;
}

static amxd_status_t action_read_last_change(amxd_object_t *object, amxd_param_t *param,
                                             amxd_action_t reason, const amxc_var_t *const args,
                                             amxc_var_t *const retval, void *priv)
{
    /*
        This action retrieves CreationTime of BSS instance from BSS.LastChange parameter,
        since BSS.Enabled changes just once when we create BSS instance BSS.LastChange is constant.
        Than we retrieve CurrentTime and subtract CreationTime for getting time passed
        from creation in seconds.
    */
    if (reason != action_param_read) {
        return amxd_status_function_not_implemented;
    }
    if (!param) {
        return amxd_status_parameter_not_found;
    }

    auto status = amxd_action_param_read(object, param, reason, args, retval, priv);
    if (status != amxd_status_ok) {
        return status;
    }
    auto creation_time = amxc_var_dyncast(uint32_t, retval);

    auto current_time =
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count());

    uint32_t last_change = current_time - creation_time;

    amxc_var_set(uint32_t, retval, last_change);

    return amxd_status_ok;
}

static std::string get_param_string(amxd_object_t *object, const char *param_name)
{
    amxc_var_t param;
    std::string param_val;

    amxc_var_init(&param);
    if (amxd_object_get_param(object, param_name, &param) == amxd_status_ok) {
        auto param_val_cstring = amxc_var_dyncast(cstring_t, &param);
        if (param_val_cstring) {
            param_val.assign(param_val_cstring);
        }
    }
    amxc_var_clean(&param);
    return param_val;
}

static bool get_param_bool(amxd_object_t *object, const char *param_name)
{
    amxc_var_t param;
    bool param_val = false;

    amxc_var_init(&param);
    if (amxd_object_get_param(object, param_name, &param) == amxd_status_ok) {
        param_val = amxc_var_constcast(bool, &param);
    } else {
        LOG(ERROR) << "Fail to get param: " << param_name;
    }
    amxc_var_clean(&param);
    return param_val;
}

static bool get_param_uint32(amxd_object_t *object, const char *param_name)
{
    amxc_var_t param;
    uint32_t param_val = false;

    amxc_var_init(&param);
    if (amxd_object_get_param(object, param_name, &param) == amxd_status_ok) {
        param_val = amxc_var_dyncast(uint32_t, &param);
    } else {
        LOG(ERROR) << "Fail to get param: " << param_name;
    }
    amxc_var_clean(&param);
    return param_val;
}

/**
* @brief Converting string of decimal values(ex. "1 3 5 63") to uint64.
* Set corresponding bits at result variable to 1.
* Ex. if we have string "0 1 5 63" then 1st, 2d, 6th and 64th bit will be set to 1.
* Decimal values range [0-63].
*/
static uint64_t get_uint64_from_bss_color_bitmap(const std::string &decimal_str)
{
    std::istringstream iss(decimal_str);
    uint64_t result = 0;

    for (int decimal = 0; iss >> decimal;) {
        // Ensure that the decimal value is within the valid range (0-63)
        if (decimal <= 63) {
            // Set corresponding bits at result variable to 1.
            result |= 1ULL << decimal;
        } else {
            LOG(WARNING) << "Invalid decimal value: " << decimal;
        }
    }
    return result;
}

/**
* @brief Overwrite an action 'get' aka 'read' for Device.WiFi.DataElements.Network.AccessPointCommit
* data element, that when this element is triggered the bss information from
* Device.WiFi.DataElements.Network.AccessPoint and Device.WiFi.DataElements.Network.AccessPoint.n.Security,
* where n = element's index, objects will be stored in the sAccessPoint structure.
*/
amxd_status_t access_point_commit(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
                                  amxc_var_t *ret)
{
    if (!g_database) {
        LOG(ERROR) << "Can't read use_dataelements_vap_configs, g_database is nullptr";
        return amxd_status_ok;
    }

    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(WARNING) << "Device.WiFi.DataElements.Network.AccessPointCommit ignored when "
                        "use_dataelements_vap_configs = false";
        return amxd_status_ok;
    }

    amxc_var_clean(ret);
    amxd_object_t *access_point = amxd_object_get_child(object, "AccessPoint");

    if (!access_point) {
        LOG(ERROR) << "AccessPoint Object is not found!";
        return amxd_status_object_not_found;
    }

    bool network_enable         = get_param_bool(object, "Enable");
    amxd_object_t *group_object = amxd_object_get_child(object, "X-PRPL_ORG_Group");
    if (!group_object) {
        LOG(WARNING) << "Fail to get Group object from Network object!";
        return amxd_status_object_not_found;
    }

    // Lets build a map of key=Name-->Enable
    std::unordered_map<std::string, bool> group_status;
    amxd_object_for_each(instance, it, group_object)
    {
        amxd_object_t *group_inst = amxc_llist_it_get_data(it, amxd_object_t, it);
        std::string group_name    = get_param_string(group_inst, "Name");
        bool group_enable         = get_param_bool(group_inst, "Enable");
        if (!group_name.empty()) {
            group_status[group_name] = group_enable;
        } else {
            LOG(WARNING) << "Name param  inside Group object is empty!";
        }
    }

    g_database->clear_bss_info_configuration();
    g_database->clear_mld_info_configuration();

    if (network_enable) {

        // TODO: centralize bss_index generation and propagation across different TLVs (PPM-3625)
        uint8_t bss_index_generator = 1;

        amxd_object_for_each(instance, it, access_point)
        {
            amxd_object_t *access_point_inst = amxc_llist_it_get_data(it, amxd_object_t, it);
            son::wireless_utils::sBssInfoConf bss_info;
            bss_info.ssid = get_param_string(access_point_inst, "SSID");

            bss_info.vap_type = wireless_utils::string_to_vap_type(
                get_param_string(access_point_inst, "X_PRPLWARE_VapType"));

            bool access_point_enable = amxd_object_get_bool(access_point_inst, "Enable", NULL);
            std::string group_name   = get_param_string(access_point_inst, "X-PRPL_ORG_GroupName");
            if (!group_name.empty()) {
                bool new_enable_value =
                    network_enable && group_status[group_name] && access_point_enable;

                //the Accesspoint shall not be enabled/existing--> lets skip it then
                if (!new_enable_value) {
                    continue;
                }
                LOG(DEBUG) << "Enabling AP with ssid:" << bss_info.ssid
                           << " under GroupName: " << group_name;

            } else {
                // We keep the accesspoint with a warning, maybe re-consider this in the future ?
                LOG(WARNING) << "AccessPoint " << bss_info.ssid << " has en Empty GroupName param!";
                if (!access_point_enable) {
                    continue;
                }
            }

            amxd_object_t *security_inst = amxd_object_get_child(access_point_inst, "Security");

            auto multi_ap_mode = get_param_string(access_point_inst, "MultiApMode");
            bss_info.backhaul  = (multi_ap_mode.find("Backhaul") != std::string::npos);
            bss_info.fronthaul = (multi_ap_mode.find("Fronthaul") != std::string::npos);

            if (!bss_info.backhaul && !bss_info.fronthaul) {
                LOG(DEBUG) << "MultiApMode for AccessPoint: " << bss_info.ssid << " is not set.";
                continue;
            }
            if (get_param_bool(access_point_inst, "Band2_4G")) {
                bss_info.operating_class.splice(
                    bss_info.operating_class.end(),
                    son::wireless_utils::string_to_wsc_oper_class("24g"));
            }
            if (get_param_bool(access_point_inst, "Band5GH")) {
                bss_info.operating_class.splice(
                    bss_info.operating_class.end(),
                    son::wireless_utils::string_to_wsc_oper_class("5gh"));
            }
            if (get_param_bool(access_point_inst, "Band5GL")) {
                bss_info.operating_class.splice(
                    bss_info.operating_class.end(),
                    son::wireless_utils::string_to_wsc_oper_class("5gl"));
            }
            if (get_param_bool(access_point_inst, "Band6G")) {
                bss_info.operating_class.splice(
                    bss_info.operating_class.end(),
                    son::wireless_utils::string_to_wsc_oper_class("6g"));
            }
            if (bss_info.operating_class.empty()) {
                LOG(WARNING) << "Band for Access Point: " << bss_info.ssid << " is not set.";
                continue;
            }

            // MLDUnit is used also for AUTH Type
            int8_t mld_unit = amxd_object_get_int8_t(access_point_inst, "MLDUnit", nullptr);

            std::string mode_enabled = get_param_string(security_inst, "ModeEnabled");
            if (mode_enabled == "WPA3-Personal" || mode_enabled == "WPA3-Personal-Transition") {
                bss_info.network_key = get_param_string(security_inst, "SAEPassphrase");
                if (bss_info.network_key.empty()) { //try KeyPassphrase instead
                    bss_info.network_key = get_param_string(security_inst, "KeyPassphrase");
                }
                if (mode_enabled == "WPA3-Personal-Transition") {
                    bss_info.authentication_type = WSC::eWscAuth(WSC::eWscAuth::WSC_AUTH_WPA2PSK |
                                                                 WSC::eWscAuth::WSC_AUTH_SAE);
                } else {
                    bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_SAE;
                }

                // Add AKM24 in case of MLD for SAE
                if (mld_unit != DISABLED_MLDUNIT) {
                    bss_info.authentication_type = WSC::eWscAuth(bss_info.authentication_type |
                                                                 WSC::eWscAuth::WSC_AUTH_SAE_AKM24);
                }

                bss_info.encryption_type = WSC::eWscEncr::WSC_ENCR_AES;
            } else if (mode_enabled == "WPA2-Personal") {
                bss_info.network_key = get_param_string(security_inst, "PreSharedKey");
                if (bss_info.network_key.empty()) {
                    bss_info.network_key = get_param_string(security_inst, "KeyPassphrase");
                }
                bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
                bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
            } else if (mode_enabled == "WPA3-Personal-Compatibility") {
                bss_info.network_key = get_param_string(security_inst, "KeyPassphrase");
                if (bss_info.network_key.empty()) {
                    bss_info.network_key = get_param_string(security_inst, "SAEPassphrase");
                }
                bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_RSN;
                bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
                bss_info.additional_auth =
                    son::wireless_utils::eAdditionalAuth::WPA3_PERSONAL_COMPATIBILITY;
            } else {
                bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_OPEN;
                bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_NONE;
            }

            if (bss_info.authentication_type != WSC::eWscAuth::WSC_AUTH_OPEN &&
                bss_info.network_key.empty()) {
                LOG(WARNING) << "BSS: " << bss_info.ssid << " with mode: " << mode_enabled
                             << " missing value for network key.";
                continue;
            }
            LOG(DEBUG) << "Add bss info configration for AP with ssid: " << bss_info.ssid
                       << " and operating classes: " << bss_info.operating_class;

            if (mld_unit != DISABLED_MLDUNIT) {
                son::wireless_utils::sMldInfoConf mld_info;
                mld_info.ssid = bss_info.ssid;

                // TODO: Read Modes from Configuration (PPM-3588)
                mld_info.str   = true;
                mld_info.nstr  = false;
                mld_info.emlsr = true;
                mld_info.emlmr = false;

                g_database->add_mld_info_configuration(mld_info, std::to_string(mld_unit));
                bss_info.mld_id = std::to_string(mld_unit);
                LOG(DEBUG) << " Set bss_info.mld_id='" << bss_info.mld_id << "'";
            }
            bss_info.bss_index = bss_index_generator++;
            g_database->add_bss_info_configuration(bss_info);
        }
    }

    // Update wifi credentials
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];
    ieee1905_1::CmduMessageTx cmdu_tx(m_tx_buffer, sizeof(m_tx_buffer));
    auto connected_agents = g_database->get_all_connected_agents();

    if (!connected_agents.empty()) {
        if (!son_actions::send_ap_config_renew_msg(cmdu_tx, *g_database)) {
            LOG(ERROR) << "Failed son_actions::send_ap_config_renew_msg ! ";
        }
    }

    return amxd_status_ok;
}

amxd_status_t client_steering(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
                              amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();

    auto sta_mac      = GET_CHAR(args, "station_mac");
    auto target_bssid = GET_CHAR(args, "target_bssid");

    if (!sta_mac || !target_bssid) {
        LOG(ERROR) << "Failed to get proper arguments.";
        return amxd_status_parameter_not_found;
    }

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    if (!g_database->can_start_client_steering(sta_mac, target_bssid)) {
        LOG(WARNING) << "Failed to initiate steering on the client: " << sta_mac
                     << " with attempt connecting to an AP with BSSID: " << target_bssid;
    } else {
        controller_ctx->start_client_steering(sta_mac, target_bssid);
    }
    return amxd_status_ok;
}

/**
 * @brief Initiate channel scan from NBAPI for given radio and channels.
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.1.Radio.1 ScanTrigger
 * '{"channels_list": "36, 44", channels_num: "2"}'
 *
 * When channel list does not contain any channels
 * scan triggering for all supported channels of specified radio.
 */
amxd_status_t trigger_scan(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
                           amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    amxc_var_t value;

    amxc_var_init(&value);
    amxd_object_get_param(object, "ID", &value);
    std::string radio_mac = amxc_var_constcast(cstring_t, &value);

    if (radio_mac.empty()) {
        LOG(ERROR) << "radio_mac is empty";
        return amxd_status_parameter_not_found;
    }

    std::string channels_list = GET_CHAR(args, "channels_list");
    int pool_size             = amxc_var_dyncast(uint32_t, GET_ARG(args, "channels_num"));
    std::array<uint8_t, beerocks::message::SUPPORTED_CHANNELS_LENGTH> channel_pool;
    std::vector<std::string> channels_vec = beerocks::string_utils::str_split(channels_list, ',');
    int i                                 = 0;

    for (auto channel_str : channels_vec) {
        if (pool_size == 0) {
            break;
        }
        int channel_num = atoi(channel_str.c_str());
        if (channel_num < std::numeric_limits<uint8_t>::min() ||
            std::numeric_limits<uint8_t>::max() < channel_num) {
            LOG(ERROR) << "Channel #" << channel_num << " is out of range.";
            return amxd_status_unknown_error;
        }
        channel_pool[i] = channel_num;
        i++;
    }

    if (i != pool_size) {
        LOG(ERROR) << "Wrong number of channels: " << pool_size
                   << " or data entered in wrong format: " << channels_list;
        return amxd_status_unknown_error;
    }

    if (!controller_ctx->trigger_scan(tlvf::mac_from_string(radio_mac), channel_pool, pool_size,
                                      PREFERRED_DWELLTIME_MS)) {
        LOG(ERROR) << "Failed to trigger scan from NBAPI for radio: " << radio_mac
                   << " with channels: " << channels_list;
        return amxd_status_unknown_error;
    }
    return amxd_status_ok;
}

/**
 * @brief Send BTMRequest from NBAPI for the current STA and given parameters
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.Radio.BSS.STA.MultiAPSTA.BTMRequest
 * '{"DisassociationImminent": "true", "DisassociationTimer": "60", "TargetBSS": "aa:bb:cc:dd:ee:ff"}'
 *
 * STA MAC address is implicit (using the MACAddress of the object on which this function
 * is called)
 * Optional parameters defined by TR-181, BSSTerminationDuration, ValidityInterval and SteeringTimer
 * are currently ignored
 */
amxd_status_t btm_request(amxd_object_t *mapsta_object, amxd_function_t *func, amxc_var_t *args,
                          amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    amxd_object_t *station_object = NULL;
    station_object                = amxd_object_get_parent(mapsta_object);

    if (station_object == NULL) {
        LOG(ERROR) << "Failed retrieving the parent of the MultiAPSTA object";
        return amxd_status_object_not_found;
    }

    amxc_var_t value;
    amxc_var_init(&value);
    amxd_object_get_param(station_object, "MACAddress", &value);
    std::string station_mac = amxc_var_constcast(cstring_t, &value);

    bool disassociation_imminent = GET_BOOL(args, "DisassociationImminent");

    uint32_t disassociation_timer = 0, bss_termination_duration = 0, validity_interval = 0,
             steering_timer  = 0;
    disassociation_timer     = GET_UINT32(args, "DisassociationTimer");
    bss_termination_duration = GET_UINT32(args, "BSSTerminationDuration");
    //optional
    validity_interval = GET_UINT32(args, "ValidityInterval");
    steering_timer    = GET_UINT32(args, "SteeringTimer");

    std::string target_bssid = GET_CHAR(args, "TargetBSS");

    if (disassociation_timer != 0) {
        disassociation_imminent = true;
        //force true if a disassoc timer is provided
    } else if (disassociation_imminent == true) {
        disassociation_timer = 60;
        // force a non-zero value for disassoc timer if disassoc imminent flag is set
    }

    if (station_mac.empty()) {
        LOG(ERROR) << "Failed reading Station MAC from DM";
        return amxd_status_invalid_attr;
    }

    if (target_bssid.empty()) {
        LOG(ERROR) << "Failed to get proper arguments.";
        return amxd_status_parameter_not_found;
    }

    if (bss_termination_duration == 0) {
        LOG(INFO) << "bss termination duration not provided";
    }

    if (validity_interval == 0) {
        LOG(INFO) << "validity interval not provided";
    }

    if (steering_timer == 0) {
        LOG(INFO) << "steering timer not provided";
    }

    if (!g_database->can_start_client_steering(station_mac, target_bssid)) {
        LOG(WARNING) << "Cannot initiate steering of the client: " << station_mac
                     << " to the AP with BSSID: " << target_bssid;
        return amxd_status_invalid_arg;
    } else {
        controller_ctx->send_btm_request(disassociation_imminent, disassociation_timer,
                                         bss_termination_duration, validity_interval,
                                         steering_timer, station_mac, target_bssid);
    }
    return amxd_status_ok;
}

/**
 * @brief Initiates setting spatial reuse parameters and bss color with the Creation TLV at the current Radio and the given parameters
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.1.Radio.1 SetSpatialReuse
 * '{"bss_color": "1", "hesiga_spr_value15_allowed": "true", "srg_information_valid": "true",
 * "non_srg_offset_valid": "true", "psr_disallowed": "true", "non_srg_obsspd_max_offset": "0",
 * "srg_obsspd_min_offset": "0", "srg_obsspd_max_offset": "0", "srg_bss_color_bitmap": "1 2 10 63",
 * "srg_partial_bssid_bitmap": "0 1 3 63"}'
 *
 * If some input parameter(s) are not provided, then the corresponding existing parameter in SpatialReuse applies.
 *
 */
amxd_status_t trigger_set_spatial_reuse(amxd_object_t *object, amxd_function_t *func,
                                        amxc_var_t *args, amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    amxc_var_t value;
    amxc_var_init(&value);
    amxd_object_get_param(object, "ID", &value);
    std::string radio_mac_str = amxc_var_constcast(cstring_t, &value);

    if (radio_mac_str.empty()) {
        LOG(ERROR) << "radio_mac is empty";
        return amxd_status_parameter_not_found;
    }

    sMacAddr radio_uid = tlvf::mac_from_string(radio_mac_str);

    amxc_var_clean(ret);
    amxd_object_t *spatial_reuse = amxd_object_get_child(object, "SpatialReuse");

    if (!spatial_reuse) {
        LOG(WARNING) << "Fail to get SpatialReuse object from data model";
        return amxd_status_unknown_error;
    }

    // Input parameters are not mandatory.
    // If some parameter(s) are not provided then the corresponding existing parameter in SpatialReuse applies.
    uint32_t bss_color;
    bool hesiga_spr_value15_allowed;
    bool srg_information_valid;
    bool non_srg_offset_valid;
    bool psr_disallowed;
    uint32_t non_srg_obsspd_max_offset     = 0;
    uint32_t srg_obsspd_min_offset         = 0;
    uint32_t srg_obsspd_max_offset         = 0;
    uint64_t srg_bss_color_bitmap_uint     = 0;
    uint64_t srg_partial_bssid_bitmap_uint = 0;

    bss_color = GET_UINT32(args, "bss_color") ?: get_param_uint32(spatial_reuse, "BSSColor");
    hesiga_spr_value15_allowed = GET_BOOL(args, "hesiga_spr_value15_allowed") ||
                                 get_param_bool(spatial_reuse, "HESIGASpatialReuseValue15Allowed");
    srg_information_valid = GET_BOOL(args, "srg_information_valid") ||
                            get_param_bool(spatial_reuse, "SRGInformationValid");
    non_srg_offset_valid = GET_BOOL(args, "non_srg_offset_valid") ||
                           get_param_bool(spatial_reuse, "NonSRGOffsetValid");
    psr_disallowed =
        GET_BOOL(args, "psr_disallowed") || get_param_bool(spatial_reuse, "PSRDisallowed");

    if (non_srg_offset_valid) {
        non_srg_obsspd_max_offset = GET_UINT32(args, "non_srg_obsspd_max_offset")
                                        ?: get_param_uint32(spatial_reuse, "NonSRGOBSSPDMaxOffset");
    }
    if (srg_information_valid) {
        std::string srg_bss_color_bitmap     = "";
        std::string srg_partial_bssid_bitmap = "";

        srg_obsspd_min_offset = GET_UINT32(args, "srg_obsspd_min_offset")
                                    ?: get_param_uint32(spatial_reuse, "SRGOBSSPDMinOffset");
        srg_obsspd_max_offset = GET_UINT32(args, "srg_obsspd_max_offset")
                                    ?: get_param_uint32(spatial_reuse, "SRGOBSSPDMaxOffset");
        srg_bss_color_bitmap = GET_CHAR(args, "srg_bss_color_bitmap")
                                   ?: get_param_string(spatial_reuse, "SRGBSSColorBitmap");
        srg_partial_bssid_bitmap = GET_CHAR(args, "srg_partial_bssid_bitmap")
                                       ?: get_param_string(spatial_reuse, "SRGPartialBSSIDBitmap");
        srg_bss_color_bitmap_uint     = get_uint64_from_bss_color_bitmap(srg_bss_color_bitmap);
        srg_partial_bssid_bitmap_uint = get_uint64_from_bss_color_bitmap(srg_partial_bssid_bitmap);
    }

    if (!controller_ctx->trigger_set_spatial_reuse(
            radio_uid, bss_color, hesiga_spr_value15_allowed, srg_information_valid,
            non_srg_offset_valid, psr_disallowed, non_srg_obsspd_max_offset, srg_obsspd_min_offset,
            srg_obsspd_max_offset, srg_bss_color_bitmap_uint, srg_partial_bssid_bitmap_uint)) {
        LOG(ERROR) << "Failed to set spatial reuse parametrs";
        return amxd_status_unknown_error;
    }

    return amxd_status_ok;
}

// VBSS Functions

/**
 * @brief Initiates a Virtual BSS Capabilities Request
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.1 UpdateVBSSCapabilities
 *
 */
amxd_status_t update_vbss_capabilities(amxd_object_t *object, amxd_function_t *func,
                                       amxc_var_t *args, amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    amxc_var_t value;

    amxc_var_init(&value);
    amxd_object_get_param(object, "ID", &value);
    std::string agent_mac_str = amxc_var_constcast(cstring_t, &value);

    if (agent_mac_str.empty()) {
        LOG(ERROR) << "agent_mac_str is empty";
        return amxd_status_parameter_not_found;
    }

    if (!controller_ctx->update_agent_vbss_capabilities(tlvf::mac_from_string(agent_mac_str))) {
        LOG(ERROR) << "Failed to send VBSS capabilites request from NBAPI for agent "
                   << agent_mac_str;
        return amxd_status_unknown_error;
    }

    return amxd_status_ok;
}

/**
 * @brief Initiates a VBSS Request with the Creation TLV at the current Radio and the given parameters
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.1.Radio.1 TriggerVBSSCreation
 * '{"vbssid": "aa:bb:cc:dd:ee:ff", "client_mac": "aa:bb:cc:dd:ee:ff", "ssid": "prplMeshNetwork", "pass": "prplmeshpass"}'
 *
 */
amxd_status_t trigger_vbss_creation(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
                                    amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    amxc_var_t value;

    amxc_var_init(&value);
    amxd_object_get_param(object, "ID", &value);
    std::string radio_mac_str = amxc_var_constcast(cstring_t, &value);

    if (radio_mac_str.empty()) {
        LOG(ERROR) << "agent_mac_str is empty";
        return amxd_status_parameter_not_found;
    }

    std::string vbssid_str     = GET_CHAR(args, "vbssid");
    std::string client_mac_str = GET_CHAR(args, "client_mac");
    std::string ssid           = GET_CHAR(args, "ssid");
    std::string password       = GET_CHAR(args, "pass");

    if (password.size() < 8) {
        LOG(ERROR)
            << "Failed to create VBSS via NB API! Password provided is less than 8 characters!";
        return amxd_status_invalid_value;
    }

    sMacAddr vbssid, client_mac = {};
    sMacAddr radio_uid = tlvf::mac_from_string(radio_mac_str);
    if (!tlvf::mac_from_string(vbssid.oct, vbssid_str)) {
        LOG(ERROR) << "Failed to create VBSS via NB API! Given VBSSID (" << vbssid_str
                   << ") is not a valid MAC address";
        return amxd_status_invalid_value;
    }
    if (!tlvf::mac_from_string(client_mac.oct, client_mac_str)) {
        LOG(ERROR) << "Failed to create VBSS via NB API! Given Client MAC (" << client_mac_str
                   << ") is not a valid MAC address";
        return amxd_status_invalid_value;
    }

    if (!controller_ctx->trigger_vbss_creation(radio_uid, vbssid, client_mac, ssid, password)) {
        LOG(ERROR) << "Failed to send VBSS Creation Request for client: " << client_mac_str
                   << " and VBSSID " << vbssid_str << " on radio " << radio_mac_str;
        return amxd_status_unknown_error;
    }

    return amxd_status_ok;
}

/**
 * @brief Initiates a Virtual BSS Destruction Request for the current Radio and BSS,
 *          along with the option to disassociate the client from the network
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.1.Radio.1.BSS.1 TriggerVBSSDestruction
 * '{"client_mac" : "aa:bb:cc:dd:ee:ff", "should_disassociate" : false}'
 *
 */
amxd_status_t trigger_vbss_destruction(amxd_object_t *object, amxd_function_t *func,
                                       amxc_var_t *args, amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    bool should_disassociate   = GET_BOOL(args, "should_disassociate");
    std::string client_mac_str = GET_CHAR(args, "client_mac");

    sMacAddr client_mac = {};
    if (!tlvf::mac_from_string(client_mac.oct, client_mac_str)) {
        LOG(ERROR) << "Failed to destroy VBSS via NB API! Given Client MAC address ("
                   << client_mac_str << ") is not a valid MAC address";
        return amxd_status_invalid_value;
    }

    amxc_var_t value;
    amxc_var_init(&value);

    // Read BSS object

    amxd_object_get_param(object, "BSSID", &value);
    std::string vbssid_str = amxc_var_constcast(cstring_t, &value);

    if (vbssid_str.empty()) {
        LOG(ERROR) << "vbssid_str is empty";
        return amxd_status_parameter_not_found;
    }
    // Read Radio object

    amxd_object_t *radio_object = NULL;
    radio_object                = amxd_object_get_parent(amxd_object_get_parent(object));

    if (radio_object == NULL) {
        LOG(ERROR) << "Failed retrieving the Radio grandparent of the BSS object";
        return amxd_status_object_not_found;
    }

    amxd_object_get_param(radio_object, "ID", &value);
    std::string connected_ruid_str = amxc_var_constcast(cstring_t, &value);

    if (connected_ruid_str.empty()) {
        LOG(ERROR) << "connected_ruid_str is empty";
        return amxd_status_parameter_not_found;
    }

    // Send Request
    sMacAddr connected_ruid = tlvf::mac_from_string(connected_ruid_str);
    sMacAddr vbssid         = tlvf::mac_from_string(vbssid_str);

    if (!controller_ctx->trigger_vbss_destruction(connected_ruid, vbssid, client_mac,
                                                  should_disassociate)) {
        LOG(ERROR) << "Failed to send VBSS Destruction request from NBAPI for VBSSID: "
                   << vbssid_str << ", on Radio: " << connected_ruid_str
                   << ", for client: " << client_mac_str;
        return amxd_status_unknown_error;
    }

    return amxd_status_ok;
}

/**
 * @brief Initiates the process of moving a Virtual BSS between radios for the current Radio and BSS,
 *          along with the provided parameters
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.1.Radio.1.BSS.1.VBSSClient.1 TriggerVBSSMove
 * '{"client_mac" : "aa:bb:cc:dd:ee:ff", "dest_ruid" : "aa:bb:cc:dd:ee:ff", "ssid": "prplMeshNetwork", "pass": "prplmeshpass"}'
 *
 */
amxd_status_t trigger_vbss_move(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
                                amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    std::string dest_ruid_str  = GET_CHAR(args, "dest_ruid");
    std::string client_mac_str = GET_CHAR(args, "client_mac");
    std::string ssid           = GET_CHAR(args, "ssid");
    std::string password       = GET_CHAR(args, "pass");

    if (password.size() < 8) {
        LOG(ERROR)
            << "Failed to move VBSS via NB API! Password provided is less than 8 characters!";
        return amxd_status_invalid_value;
    }

    sMacAddr dest_ruid = {};
    if (!tlvf::mac_from_string(dest_ruid.oct, dest_ruid_str)) {
        LOG(ERROR) << "Failed to move VBSS via NB API! Given Radio UID (" << dest_ruid_str
                   << ") is not a valid MAC address";
        return amxd_status_invalid_value;
    }
    sMacAddr client_mac = {};
    if (!tlvf::mac_from_string(client_mac.oct, client_mac_str)) {
        LOG(ERROR) << "Failed to move VBSS via NB API! Given Client MAC address (" << client_mac_str
                   << ") is not a valid MAC address";
        return amxd_status_invalid_value;
    }

    if (!g_database->get_radio_by_uid(dest_ruid)) {
        LOG(ERROR) << "Failed to move VBSS via NB API! Given Radio UID (" << dest_ruid_str
                   << ") does not correspond to an existing radio!";
        return amxd_status_invalid_value;
    }

    auto station = g_database->get_station(client_mac);
    if (!station) {
        LOG(ERROR) << "Station not found in the database!";
        return amxd_status_invalid_value;
    }

    auto bss = station->get_bss();
    if (!bss) {
        LOG(ERROR) << "Failed to move VBSS via NB API! The station is not currently connected! "
                      "Station MAC: "
                   << station->mac;
        return amxd_status_invalid_value;
    }

    // Send Request

    sMacAddr connected_ruid = bss->radio.radio_uid;
    sMacAddr vbssid         = bss->bssid;

    if (!controller_ctx->trigger_vbss_move(connected_ruid, dest_ruid, vbssid, client_mac, ssid,
                                           password)) {
        LOG(ERROR) << "Failed to trigger VBSS Move from NBAPI for VBSSID: " << vbssid
                   << ", on current Radio: " << connected_ruid << ", for client: " << client_mac_str
                   << ", moving to Radio: " << dest_ruid_str;
        return amxd_status_unknown_error;
    }

    return amxd_status_ok;
}

/**
 * @brief add EHT Operation TLV with disabled subchannel bitmap as an argument
 *
 * @param[in] DisabledSubchannelBitmap decimal or hex representation of the disabled subchannel bitmap,
 *            where LSB is lowest 20MHz channel of the currently in use Operating Class
 *            same syntax as hostapd.conf punct_bitmap parameter
 * Example of usage:
 * Device.WiFi.DataElements.Network.Device.1.Radio.1.BSS.1.SetEHTOperations(DisabledSubchannelBitmap="0x02")
 *
 */
amxd_status_t set_eht_operations(amxd_object_t *bss_instance, amxd_function_t *func,
                                 amxc_var_t *args, amxc_var_t *ret)
{
    auto controller_ctx = g_database->get_controller_ctx();
    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    amxc_var_t value;
    amxc_var_init(&value);

    amxd_object_t *radio = amxd_object_get_parent(amxd_object_get_parent(bss_instance));
    amxd_object_get_param(radio, "ID", &value);
    const std::string radio_mac = amxc_var_constcast(cstring_t, &value);

    if (radio_mac.empty()) {
        LOG(ERROR) << "radio_mac is empty";
        amxc_var_clean(&value);
        return amxd_status_parameter_not_found;
    }

    amxd_object_t *agent = amxd_object_get_parent(amxd_object_get_parent(radio));
    amxd_object_get_param(agent, "ID", &value);
    const std::string agent_mac = amxc_var_constcast(cstring_t, &value);

    if (agent_mac.empty()) {
        LOG(ERROR) << "agent_mac is empty";
        amxc_var_clean(&value);
        return amxd_status_parameter_not_found;
    }

    amxd_object_get_param(bss_instance, "BSSID", &value);
    const std::string bssid_str = amxc_var_constcast(cstring_t, &value);

    if (bssid_str.empty()) {
        LOG(ERROR) << "bssid_str is empty";
        amxc_var_clean(&value);
        return amxd_status_parameter_not_found;
    }

    amxc_var_clean(&value);

    uint16_t bitmap = amxc_var_dyncast(uint16_t, GET_ARG(args, "DisabledSubchannelBitmap"));

    if (!controller_ctx->set_eht_operations(tlvf::mac_from_string(agent_mac),
                                            tlvf::mac_from_string(radio_mac),
                                            tlvf::mac_from_string(bssid_str), bitmap)) {
        LOG(ERROR) << "Failed to set disabled subchannel from NBAPI for BSSID: " << bssid_str;
        return amxd_status_unknown_error;
    }

    return amxd_status_ok;
}

amxd_status_t trigger_prioritization(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
                                     amxc_var_t *ret)
{
    if (!g_database) {
        LOG(ERROR) << "Invalid database access";
        return amxd_status_unknown_error;
    }

    auto controller = g_database->get_controller_ctx();
    if (!controller) {
        LOG(ERROR) << "Failed to get controller context";
        return amxd_status_unknown_error;
    }

    g_database->dm_configure_service_prioritization();
    controller->trigger_prioritization_config();
    return amxd_status_ok;
}
/**
 * @brief add an unassociated station using the channel given in the arguments
 * 
 * Example of usage:
 * Device.WiFi.DataElements.Network.Device.1.Radio.1.AddUnassociatedStation(un_station_mac="AA:BB:CC:DD:12:04",operating_class=81,channel=4,agent_mac="b2:83:c4:14:93:08")
 *
 */
amxd_status_t add_unassociated_station(amxd_object_t *object, amxd_function_t *func,
                                       amxc_var_t *args, amxc_var_t *ret)
{
    amxd_status_t result(amxd_status_ok);

    amxc_var_t value;
    amxc_var_init(&value);
    amxd_object_get_param(object, "ID", &value);
    const char *str = amxc_var_constcast(cstring_t, &value);
    if (str == nullptr) {
        LOG(ERROR) << "Failed fetching ID";
        amxc_var_clean(&value);
        return amxd_status_object_not_found;
    };
    std::string radio_mac(str);
    if (radio_mac.empty()) {
        LOG(ERROR) << "radio_mac is empty";
        amxc_var_clean(&value);
        return amxd_status_parameter_not_found;
    }

    //get agent mac
    amxd_object_t *parent = amxd_object_get_parent(object);
    amxd_object_get_param(parent, "ID", &value);
    str = amxc_var_constcast(cstring_t, &value);
    if (str == nullptr) {
        LOG(ERROR) << "Failed fetching ID";
        amxc_var_clean(&value);
        return amxd_status_object_not_found;
    };
    std::string agent_mac(str);
    if (agent_mac.empty()) {
        LOG(ERROR) << "agent_mac is empty";
        amxc_var_clean(&value);
        return amxd_status_parameter_not_found;
    }
    amxc_var_clean(&value);

    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    std::string station_mac_addr = GET_CHAR(args, "un_station_mac");
    if (!network_utils::is_valid_mac(station_mac_addr)) {
        LOG(ERROR) << station_mac_addr << " is not avalid mac_address!";
        return amxd_status_invalid_value;
    }

    uint8_t channel = amxc_var_dyncast(uint8_t, GET_ARG(args, "channel"));
    if (channel < 1) {
        LOG(ERROR) << "entered channel is not valid! ";
        return amxd_status_invalid_value;
    }

    uint8_t operating_class = amxc_var_dyncast(uint8_t, GET_ARG(args, "operating_class"));
    if (operating_class < 1) {
        LOG(ERROR) << "entered operating_class is not valid! ";
        return amxd_status_invalid_value;
    }

    if (!controller_ctx->add_unassociated_station(tlvf::mac_from_string(station_mac_addr), channel,
                                                  operating_class, tlvf::mac_from_string(agent_mac),
                                                  tlvf::mac_from_string(radio_mac))) {
        result = amxd_status_unknown_error;
    }
    return result;
}

/**
 * @brief remove the unassociated station being monitored
 * 
 * Example of usage:
 * Device.WiFi.DataElements.Network.Device.1.Radio.1.RemoveUnassociatedStation(un_station_mac="AA:BB:CC:DD:12:04")
 *
 */
amxd_status_t remove_unassociated_station(amxd_object_t *object, amxd_function_t *func,
                                          amxc_var_t *args, amxc_var_t *ret)
{
    amxd_status_t result(amxd_status_ok);

    amxc_var_t value;

    amxc_var_init(&value);
    amxd_object_get_param(object, "ID", &value);
    const char *str = amxc_var_constcast(cstring_t, &value);
    if (str == nullptr) {
        LOG(ERROR) << "Failed fetching ID";
        amxc_var_clean(&value);
        return amxd_status_object_not_found;
    };
    std::string radio_mac(str);
    if (radio_mac.empty()) {
        LOG(ERROR) << "radio_mac is empty";
        amxc_var_clean(&value);
        return amxd_status_parameter_not_found;
    }

    //get agent mac
    amxd_object_t *parent = amxd_object_get_parent(object);
    amxd_object_get_param(parent, "ID", &value);
    str = amxc_var_constcast(cstring_t, &value);
    amxc_var_clean(&value);

    if (str == nullptr) {
        LOG(ERROR) << "Failed fetching ID";
        return amxd_status_object_not_found;
    };
    std::string agent_mac(str);
    if (agent_mac.empty()) {
        LOG(ERROR) << "agent_mac is empty";
        return amxd_status_parameter_not_found;
    }

    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }

    std::string station_mac_addr = GET_CHAR(args, "un_station_mac");
    if (!network_utils::is_valid_mac(station_mac_addr)) {
        LOG(ERROR) << station_mac_addr << " is not avalid mac_address!";
        return amxd_status_invalid_value;
    }

    if (!controller_ctx->remove_unassociated_station(tlvf::mac_from_string(station_mac_addr),
                                                     tlvf::mac_from_string(agent_mac),
                                                     tlvf::mac_from_string(radio_mac))) {
        result = amxd_status_unknown_error;
    }
    return result;
}

/**
 * @brief update the datamodel with new stats from all connected agents
 * 
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network UpdateUnassociatedStationsStats
 *
 */
amxd_status_t update_unassociatedStations_stats(amxd_object_t *object, amxd_function_t *func,
                                                amxc_var_t *args, amxc_var_t *ret)
{
    amxd_status_t result(amxd_status_ok);
    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(ERROR) << "Failed to get controller context.";
        return amxd_status_unknown_error;
    }
    if (!controller_ctx->get_unassociated_stations_stats()) {
        result = amxd_status_unknown_error;
    }
    return result;
}

/**
 * @brief Build a parameter filter targeting an object instance by a linked TemplateID.
 *
 * Example:
 *   If BSSTemplate has LinkedSSCTemplateID="PRIV" (Device.WiFi.Templates.BSSTemplate.1.LinkedSSCTemplateID="PRIV"),
 *   this builds a filter to read HaulType from the SSCTemplate with ID "PRIV":
 *   Device.WiFi.Templates.SSCTemplate.[SSCTemplateID=='PRIV'].HaulType?
 *
 * @param linked_template_id_name  Name of the TemplateID attribute (e.g., "SSCTemplateID").
 * @param linked_template_id_value Value of the TemplateID to match (e.g., "PRIV").
 * @param param_name               Target parameter name (e.g., "HaulType").
 * @return Filter string
 */
std::string filter_param_name_with_linked_template_id(const char *linked_template_id_name,
                                                      const std::string &linked_template_id_value,
                                                      const char *param_name)
{
    return "[" + std::string(linked_template_id_name) + "== '" + linked_template_id_value + "']." +
           param_name;
}

beerocks::BandFlag str_to_band_flag(const std::string &band_str)
{
    if (band_str == "2.4")
        return beerocks::BandFlag::BAND_2_4;
    if (band_str == "5")
        return beerocks::BandFlag::BAND_5;
    if (band_str == "6")
        return beerocks::BandFlag::BAND_6;
    if (band_str == "5_UNII_1")
        return beerocks::BandFlag::BAND_5_UNII_1;
    if (band_str == "5_UNII_2")
        return beerocks::BandFlag::BAND_5_UNII_2;
    if (band_str == "5_UNII_3")
        return beerocks::BandFlag::BAND_5_UNII_3;
    if (band_str == "5_UNII_4")
        return beerocks::BandFlag::BAND_5_UNII_4;
    if (band_str == "6_UNII_5")
        return beerocks::BandFlag::BAND_6_UNII_5;
    if (band_str == "6_UNII_6")
        return beerocks::BandFlag::BAND_6_UNII_6;
    if (band_str == "6_UNII_7")
        return beerocks::BandFlag::BAND_6_UNII_7;
    if (band_str == "6_UNII_8")
        return beerocks::BandFlag::BAND_6_UNII_8;
    if (band_str == "Sub_1GHz")
        return beerocks::BandFlag::BAND_SUB_1GHZ;
    return beerocks::BandFlag::UNKNOWN;
}

std::string band_flag_to_str(beerocks::BandFlag band)
{
    switch (band) {
    case beerocks::BandFlag::BAND_2_4:
        return std::string("24g");
    case beerocks::BandFlag::BAND_5:
        return std::string("5g");
    case beerocks::BandFlag::BAND_6:
        return std::string("6g");
    case beerocks::BandFlag::BAND_5_UNII_1:
        return std::string("5_UNII_1");
    case beerocks::BandFlag::BAND_5_UNII_2:
        return std::string("5_UNII_2");
    case beerocks::BandFlag::BAND_5_UNII_3:
        return std::string("5_UNII_3");
    case beerocks::BandFlag::BAND_5_UNII_4:
        return std::string("5_UNII_4");
    case beerocks::BandFlag::BAND_6_UNII_5:
        return std::string("6_UNII_5");
    case beerocks::BandFlag::BAND_6_UNII_6:
        return std::string("6_UNII_6");
    case beerocks::BandFlag::BAND_6_UNII_7:
        return std::string("6_UNII_7");
    case beerocks::BandFlag::BAND_6_UNII_8:
        return std::string("6_UNII_8");
    case beerocks::BandFlag::BAND_SUB_1GHZ:
        return std::string("Sub_1GHz");
    default:
        return std::string("UNKNOWN");
    }
}

// Simple trim helpers (ASCII whitespace) - Left Trim
static inline void ltrim(std::string &s)
{
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

// Simple trim helpers (ASCII whitespace) - right Trim
static inline void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            s.end());
}

static inline void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

/**
 * @brief Parse a comma-separated band list and validate composition rules.
 *
 * Rules:
 *  - Either zero items (returns empty vector).
 *  - OR exactly one frequency band: Sub_1GHz, 2.4, 5, or 6.
 *  - OR one or more UNII bands (any of 5_UNII_* or 6_UNII_*), exclusively.
 *
 * @param band_flag Comma-separated bands (e.g., "5_UNII_1, 5_UNII_3", or "2.4").
 * @return Validated band list; empty if composition invalid or tokens unknown.
 */
std::vector<beerocks::BandFlag> parse_and_validate_band_flag(const std::string &band_flag)
{
    std::vector<beerocks::BandFlag> bands;
    std::stringstream ss(band_flag);
    std::string token;

    while (std::getline(ss, token, ',')) {
        trim(token);
        if (!token.empty()) {
            beerocks::BandFlag band = str_to_band_flag(token);
            if (band != beerocks::BandFlag::UNKNOWN) {
                bands.push_back(band);
            } else {
                LOG(WARNING) << "Skipping unknown frequency band: " << token;
            }
        }
    }

    if (bands.empty()) {
        LOG(DEBUG) << "No valid bands parsed from input: '" << band_flag << "'";
        return {};
    }

    auto is_frequency_band = [](const beerocks::BandFlag band) {
        return band == beerocks::BandFlag::BAND_SUB_1GHZ || band == beerocks::BandFlag::BAND_2_4 ||
               band == beerocks::BandFlag::BAND_5 || band == beerocks::BandFlag::BAND_6;
    };
    auto is_unii_band = [](const beerocks::BandFlag band) {
        return band == beerocks::BandFlag::BAND_5_UNII_1 ||
               band == beerocks::BandFlag::BAND_5_UNII_2 ||
               band == beerocks::BandFlag::BAND_5_UNII_3 ||
               band == beerocks::BandFlag::BAND_5_UNII_4 ||
               band == beerocks::BandFlag::BAND_6_UNII_5 ||
               band == beerocks::BandFlag::BAND_6_UNII_6 ||
               band == beerocks::BandFlag::BAND_6_UNII_7 ||
               band == beerocks::BandFlag::BAND_6_UNII_8;
    };

    bool all_frequency = std::all_of(bands.begin(), bands.end(), is_frequency_band);
    bool all_unii      = std::all_of(bands.begin(), bands.end(), is_unii_band);

    if ((bands.size() == 1 && all_frequency) || all_unii) {
        LOG(DEBUG) << "Validated bands: count=" << bands.size()
                   << ", composition=" << (all_unii ? "UNII-only" : "single-frequency");
        return bands;
    }

    LOG(WARNING)
        << "Invalid band composition. Must be a single frequency band OR only UNII bands. Input: '"
        << band_flag << "'";
    return {};
}

std::vector<std::string>
parse_security_template_id_list(const std::string &linked_security_group_id)
{
    std::vector<std::string> security_template_id_list;
    std::stringstream ss(linked_security_group_id);
    std::string token;

    while (std::getline(ss, token, ',')) {
        trim(token);

        if (!token.empty()) {
            security_template_id_list.push_back(token);
        }
    }

    if (security_template_id_list.empty()) {
        LOG(DEBUG) << "No SecurityTemplateIDs parsed from: '" << linked_security_group_id << "'";
    } else {
        LOG(DEBUG) << "Parsed SecurityTemplateIDs (" << security_template_id_list.size()
                   << "): " << linked_security_group_id;
    }

    return security_template_id_list;
}

/**
 * @brief Among the given SecurityTemplateIDs, find the enabled one with the highest Priority.
 *
 * @param security_template_object Object holding security template parameters (Priority, Enable).
 * @param linked_security_group_id Comma-separated IDs to consider.
 * @return The ID with the highest priority (if enabled). Empty string if none found/enabled.
 */
std::string
find_security_template_id_with_the_highest_priority(amxd_object_t *security_template_object,
                                                    const std::string &linked_security_group_id)
{
    auto security_template_id_list = parse_security_template_id_list(linked_security_group_id);
    std::string highest_security_template_id        = "";
    unsigned int highest_security_template_priority = 0;

    if (security_template_id_list.empty()) {
        LOG(WARNING) << "No SecurityTemplateIDs to evaluate.";
        return "";
    }

    for (const auto &security_template_id : security_template_id_list) {
        uint32_t security_template_priority = get_param_uint32(
            security_template_object, filter_param_name_with_linked_template_id(
                                          "SecurityTemplateID", security_template_id, "Priority")
                                          .c_str());
        bool security_template_enable = get_param_bool(
            security_template_object, filter_param_name_with_linked_template_id(
                                          "SecurityTemplateID", security_template_id, "Enable")
                                          .c_str());

        LOG(DEBUG) << "TemplateID='" << security_template_id
                   << "' priority=" << security_template_priority
                   << ", enable=" << (security_template_enable ? "true" : "false");

        if (!security_template_enable) {
            continue; // Skip disabled security templates
        }

        if (security_template_priority > highest_security_template_priority) {
            highest_security_template_priority = security_template_priority;
            highest_security_template_id       = security_template_id;
        }
    }

    if (highest_security_template_id.empty()) {
        LOG(WARNING) << "No enabled SecurityTemplate found with a positive priority.";
    } else {
        LOG(INFO) << "Selected SecurityTemplateID with highest priority: '"
                  << highest_security_template_id
                  << "' (priority=" << highest_security_template_priority << ")";
    }

    return highest_security_template_id;
}

beerocks::AKMType str_to_akm_suite(const std::string &akm_suite)
{
    if (akm_suite == "psk")
        return beerocks::AKMType::PSK;
    if (akm_suite == "dpp")
        return beerocks::AKMType::DPP;
    if (akm_suite == "sae")
        return beerocks::AKMType::SAE;
    if (akm_suite == "sae-ext-key")
        return beerocks::AKMType::SAE_EXT_KEY;
    if (akm_suite == "SuiteSelector")
        return beerocks::AKMType::SUITE_SELECTOR;
    return beerocks::AKMType::UNKNOWN;
}

std::vector<beerocks::AKMType> parse_and_validate_akm_suite(const std::string &akm_suite)
{
    std::vector<beerocks::AKMType> akm_suite_list;
    std::stringstream ss(akm_suite);
    std::string token;

    while (std::getline(ss, token, ',')) {
        trim(token);
        if (!token.empty()) {
            beerocks::AKMType akm_type = str_to_akm_suite(token);
            if (akm_type != beerocks::AKMType::UNKNOWN) {
                akm_suite_list.push_back(akm_type);
            } else {
                LOG(WARNING) << "Skipping unknown Authentication type: " << token;
            }
        }
    }

    LOG(DEBUG) << "Parsed AKM suite list size=" << akm_suite_list.size() << " from input: '"
               << akm_suite << "'";
    return akm_suite_list;
}

std::string security_mode_to_string(beerocks::SecurityMode mode)
{
    switch (mode) {
    case beerocks::SecurityMode::WPA2_PERSONAL:
        return "WPA2-Personal";
    case beerocks::SecurityMode::WPA3_PERSONAL:
        return "WPA3-Personal";
    case beerocks::SecurityMode::WPA3_PERSONAL_TRANSITION:
        return "WPA3-Personal-Transition";
    case beerocks::SecurityMode::DPP:
        return "DPP";
    case beerocks::SecurityMode::OPEN:
        return "OPEN";
    default:
        return "UNKNOWN";
    }
}

// Utility function to check if a vector contains a specific AKMType
bool contains_akm(const std::vector<AKMType> &akm_list, AKMType akm_type)
{
    return std::find(akm_list.begin(), akm_list.end(), akm_type) != akm_list.end();
}

beerocks::SecurityMode
configure_security_parameters_in_bss_info(son::wireless_utils::sBssInfoConf &bss_info,
                                          const std::vector<beerocks::AKMType> &akm_suite_list,
                                          const std::string &key_pass_phrase)
{
    bool has_psk = contains_akm(akm_suite_list, beerocks::AKMType::PSK);
    bool has_sae = contains_akm(akm_suite_list, beerocks::AKMType::SAE);
    bool has_dpp = contains_akm(akm_suite_list, beerocks::AKMType::DPP);

    beerocks::SecurityMode security_mode;
    // Set common defaults
    bss_info.encryption_type = WSC::eWscEncr::WSC_ENCR_AES;
    bss_info.network_key     = key_pass_phrase;

    // Only DPP
    if (has_dpp && akm_suite_list.size() == 1) {
        security_mode                = beerocks::SecurityMode::DPP;
        bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_DPP;
        // DPP uses public key cryptography and bootstrapping rather than network key
        bss_info.network_key.clear();
        LOG(DEBUG) << "Selected DPP security";
    }
    // Only PSK
    else if (has_psk && akm_suite_list.size() == 1) {
        security_mode                = beerocks::SecurityMode::WPA2_PERSONAL;
        bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
        LOG(DEBUG) << "Selected WPA2-Personal (PSK)";
    }
    // SAE (with or without SAE_EXT_KEY), and no PSK
    else if (has_sae && !has_psk) {
        security_mode                = beerocks::SecurityMode::WPA3_PERSONAL;
        bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_SAE;
        LOG(DEBUG) << "Selected WPA3-Personal (SAE-only)";
    }
    // PSK + SAE => Transition mode (with or without SAE_EXT_KEY)
    else if (has_psk && has_sae) {
        security_mode = beerocks::SecurityMode::WPA3_PERSONAL_TRANSITION;
        bss_info.authentication_type =
            WSC::eWscAuth(WSC::eWscAuth::WSC_AUTH_WPA2PSK | WSC::eWscAuth::WSC_AUTH_SAE);
        LOG(DEBUG) << "Selected WPA3-Personal-Transition (PSK + SAE)";
    } else {
        // Default to OPEN
        security_mode                = beerocks::SecurityMode::OPEN;
        bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_OPEN;
        bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_NONE;
        bss_info.network_key.clear();
        LOG(DEBUG) << "Selected OPEN network (no valid AKMs)";
    }

    LOG(INFO) << "Authentication Mode: " << security_mode_to_string(security_mode);
    return security_mode;
}

/**
 * @brief Parse WiFi operating generations from input and optionally include future generations.
 *
 * Input examples:
 *   "4,5,6"  -> ["4","5","6"]
 *   "6+"     -> ["6","7"]  (future generations after 6, up to the highest known)
 *   "5, 6+"  -> ["5","6","7"] (the '+' only applies to last token)
 */
std::vector<std::string> get_wifi_generations(const std::string &operating_generation)
{
    // Known generations and their ordering reference
    static const std::unordered_map<std::string, int> gen_order = {
        {"1", 1}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}, {"6", 6}, {"7", 7}};

    if (operating_generation.empty()) {
        LOG(WARNING)
            << "Empty input provided. Please specify WiFi generations (e.g., '4,5,6' or '6+')";
        return {};
    }

    std::stringstream ss(operating_generation);
    std::string token;
    std::vector<std::string> generation_tokens;
    while (std::getline(ss, token, ',')) {
        trim(token);
        if (!token.empty()) {
            generation_tokens.push_back(token);
        }
    }

    bool include_future_generations = false;
    std::vector<std::string> parsed_generations;

    for (size_t i = 0; i < generation_tokens.size(); ++i) {
        std::string raw_token    = generation_tokens[i];
        const bool is_last_token = (i == generation_tokens.size() - 1);

        // only treat a trailing '+' on the last token as a future-generation marker.
        if (!raw_token.empty() && raw_token.back() == '+') {
            size_t len = raw_token.length();
            // base is token without trailing '+'
            std::string base_token = raw_token.substr(0, len - 1);

            if (len >= 2 && std::isspace(static_cast<unsigned char>(raw_token[len - 2]))) {
                rtrim(base_token);
                if (base_token.empty()) {
                    LOG(WARNING) << "'+' cannot be standalone; ignoring.";
                    continue;
                }
                LOG(WARNING)
                    << "Space is not allowed between the generation and '+'. Ignoring '+' in '"
                    << raw_token << "'.";
                parsed_generations.push_back(base_token);
                continue;
            }
            trim(base_token);
            if (base_token.empty()) {
                LOG(WARNING) << "'+' cannot be standalone; ignoring.";
                continue;
            }

            if (!is_last_token) {
                LOG(WARNING) << "'+' is only allowed on the last list item; ignoring '+' in '"
                             << raw_token << "'.";
                parsed_generations.push_back(base_token);
                continue;
            }

            if (gen_order.find(base_token) != gen_order.end()) {
                include_future_generations = true;
                parsed_generations.push_back(base_token);
            } else {
                LOG(DEBUG)
                    << "Warning: '+' is only accepted when the last item is a valid generation. '"
                    << base_token << "' is not a known generation.";
                parsed_generations.push_back(base_token);
            }
        } else {
            parsed_generations.push_back(raw_token);
        }
    }

    if (parsed_generations.empty()) {
        LOG(WARNING) << "No valid WiFi generations found in input: '" << operating_generation
                     << "'";
        return {};
    }

    // Collect valid generations (preserve order, avoid duplicates)
    std::vector<std::string> valid_generations;
    for (const auto &gen_token : parsed_generations) {
        if (gen_order.find(gen_token) != gen_order.end()) {
            if (std::find(valid_generations.begin(), valid_generations.end(), gen_token) ==
                valid_generations.end()) {
                valid_generations.push_back(gen_token);
            }
        } else {
            LOG(WARNING) << "Unknown WiFi generation: " << gen_token;
        }
    }

    if (include_future_generations && !valid_generations.empty()) {
        int max_generation_ord = 0;
        for (const auto &gen_token : valid_generations) {
            max_generation_ord = std::max(max_generation_ord, gen_order.at(gen_token));
        }

        // Build ordered list and append future gens beyond max (up to the highest known; 7 is top now)
        std::vector<std::pair<std::string, int>> ordered_gen_pairs;
        ordered_gen_pairs.reserve(gen_order.size());
        for (const auto &kv : gen_order)
            ordered_gen_pairs.emplace_back(kv.first, kv.second);
        std::sort(ordered_gen_pairs.begin(), ordered_gen_pairs.end(),
                  [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) {
                      return a.second < b.second || (a.second == b.second && a.first < b.first);
                  });

        for (const auto &gen_pair : ordered_gen_pairs) {
            if (gen_pair.second > max_generation_ord) {
                if (std::find(valid_generations.begin(), valid_generations.end(), gen_pair.first) ==
                    valid_generations.end()) {
                    valid_generations.push_back(gen_pair.first);
                }
            }
        }
    }

    LOG(DEBUG) << "Final WiFi generations (" << valid_generations.size()
               << "): " << operating_generation;
    return valid_generations;
}

// Events

/**
* @brief Overwrite an action 'get' aka 'read' to fetch data about Access Point from ubus and store it in the controller database.
*  When this element is triggered the bss information from "Device.WiFi.Templates.BSSTemplate.", "Device.WiFi.Templates.RadioTemplate.",
* "Device.WiFi.Templates.SSCTemplate.", "Device.WiFi.Templates.SecurityGroup", and "Device.WiFi.Templates.SecurityTemplate" objects will be stored in sBssInfoConf structure.
*/
amxd_status_t templates_commit(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
                               amxc_var_t *ret)
{
    amxc_var_clean(ret);
    amxd_object_t *bss_template_object = amxd_object_get_child(object, "BSSTemplate");

    if (!bss_template_object) {
        LOG(ERROR) << "Failed to get BSSTemplate object from Templates object!";
        return amxd_status_object_not_found;
    }

    amxd_object_t *network_object = amxd_object_get_child(object, "Network");
    if (!network_object) {
        LOG(ERROR) << "Failed to get Network object from Templates object!";
        return amxd_status_object_not_found;
    }
    bool network_enable = get_param_bool(network_object, "Enable");

    amxd_object_t *ssc_template_object = amxd_object_get_child(object, "SSCTemplate");
    if (!ssc_template_object) {
        LOG(ERROR) << "Failed to get SSCTemplate object from Templates object!";
        return amxd_status_object_not_found;
    }

    amxd_object_t *radio_template_object = amxd_object_get_child(object, "RadioTemplate");
    if (!radio_template_object) {
        LOG(ERROR) << "Failed to get RadioTemplate object from Templates object!";
        return amxd_status_object_not_found;
    }

    amxd_object_t *apmld_template_object = amxd_object_get_child(object, "APMLDTemplate");
    if (!apmld_template_object) {
        LOG(ERROR) << "Failed to get APMLDTemplate object from Templates object!";
        return amxd_status_object_not_found;
    }

    // Build a map of key=Name-->Enable for SSCTemplate
    std::unordered_map<std::string, bool> ssc_template_status;
    amxd_object_for_each(instance, it, ssc_template_object)
    {
        amxd_object_t *ssc_template_inst = amxc_llist_it_get_data(it, amxd_object_t, it);
        std::string ssc_template_id      = get_param_string(ssc_template_inst, "SSCTemplateID");
        bool ssc_template_enable         = get_param_bool(ssc_template_inst, "SSCEnable");

        if (!ssc_template_id.empty()) {
            ssc_template_status[ssc_template_id] = ssc_template_enable;
            LOG(DEBUG) << "SSC map: id=" << ssc_template_id
                       << " enable=" << (ssc_template_enable ? "true" : "false");
        } else {
            LOG(WARNING) << "SSCTemplateID param in SSCTemplate object is empty!";
        }
    }
    LOG(DEBUG) << "SSC map built. size=" << ssc_template_status.size();

    // Build a map of key=Name-->Enable for RadioTemplate
    std::unordered_map<std::string, bool> radio_template_status;
    amxd_object_for_each(instance, it, radio_template_object)
    {
        amxd_object_t *radio_template_inst = amxc_llist_it_get_data(it, amxd_object_t, it);
        std::string radio_template_id = get_param_string(radio_template_inst, "RadioTemplateID");
        bool radio_template_enable    = get_param_bool(radio_template_inst, "Enable");

        if (!radio_template_id.empty()) {
            radio_template_status[radio_template_id] = radio_template_enable;
            LOG(DEBUG) << "Radio map: id=" << radio_template_id
                       << " enable=" << (radio_template_enable ? "true" : "false");
        } else {
            LOG(WARNING) << "RadioTemplateID param in RadioTemplate object is empty!";
        }
    }
    LOG(DEBUG) << "Radio map built. size=" << radio_template_status.size();

    // Build a map of key=Name-->Enable for APMLDTemplate
    std::unordered_map<std::string, bool> apmld_template_status;
    amxd_object_for_each(instance, it, apmld_template_object)
    {
        amxd_object_t *apmld_template_inst = amxc_llist_it_get_data(it, amxd_object_t, it);
        std::string apmld_template_id = get_param_string(apmld_template_inst, "APMLDTemplateID");
        bool apmld_template_enable    = get_param_bool(apmld_template_inst, "MLOEnable");

        if (!apmld_template_id.empty()) {
            apmld_template_status[apmld_template_id] = apmld_template_enable;
            LOG(DEBUG) << "APMLD map: id=" << apmld_template_id
                       << " MLOEnable=" << (apmld_template_enable ? "true" : "false");
        } else {
            LOG(WARNING) << "APMLDTemplateID param in APMLDTemplate object is empty!";
        }
    }
    LOG(DEBUG) << "APMLD map built. size=" << apmld_template_status.size();

    // --------- Clear previously staged configs ---------
    g_database->clear_bss_info_configuration();
    g_database->clear_mld_info_configuration();
    LOG(DEBUG) << " Cleared previous BSS/MLD info configuration";

    // Coarse upper bound on how many BSS templates will be added to the BSS info configuration.
    // This uses the MAX across connected agents because a later stage
    // enforces per-agent/per-radio constraints on effective deployment
    size_t max_supported_bss = 0;
    {
        auto agents = g_database->get_all_connected_agents();
        LOG(DEBUG) << "Connected agents: " << agents.size();

        if (!agents.empty()) {
            for (const auto &agent : agents) {
                LOG(DEBUG) << "Agent " << agent->al_mac;
                size_t max_bss_on_any_radio = 0;
                size_t radios_count         = 0;

                for (const auto &radio_kv : agent->radios) {
                    const auto &radio_uid = radio_kv.first;
                    const auto &radio_sp  = radio_kv.second;

                    if (!radio_sp) {
                        LOG(WARNING) << "Agent " << agent->al_mac << " radio " << radio_uid
                                     << " is null, skipping.";
                        continue;
                    }

                    ++radios_count;
                    const size_t bss_count = radio_sp->bsses.size();
                    max_bss_on_any_radio   = std::max(max_bss_on_any_radio, bss_count);
                    LOG(DEBUG) << "Radio " << radio_uid << " has " << bss_count << " BSS(es)";
                }

                const size_t agent_supported_bss = max_bss_on_any_radio * radios_count;
                LOG(DEBUG) << "Agent " << agent->al_mac << " => radios=" << radios_count
                           << ", max_bss_on_any_radio=" << max_bss_on_any_radio
                           << ", agent_supported_bss=" << agent_supported_bss;

                max_supported_bss = std::max(max_supported_bss, agent_supported_bss);
            }
            LOG(DEBUG) << "Max supported BSS across connected agents: " << max_supported_bss;
        } else {
            LOG(INFO) << "No connected agents found, so no BSS templates will be selected";
        }
    }

    // --------- Select & stage BSS configs ---------

    if (network_enable) {
        // Collect all BSSTemplate instances with their Priority (as pair<priority, object>)
        std::vector<std::pair<uint32_t, amxd_object_t *>> bss_ordered;
        {
            size_t bss_inst_count = 0;
            amxd_object_for_each(instance, it, bss_template_object)
            {
                amxd_object_t *inst = amxc_llist_it_get_data(it, amxd_object_t, it);
                uint32_t prio       = get_param_uint32(inst, "Priority");
                bss_ordered.emplace_back(prio, inst);
                bss_inst_count++;
            }
            LOG(DEBUG) << "Collected " << bss_inst_count
                       << " BSSTemplate instance(s) prior to sorting";
        }

        // Sort by Priority descending so higher Priority BSS templates get selected first
        std::sort(bss_ordered.begin(), bss_ordered.end(),
                  [](const std::pair<uint32_t, amxd_object_t *> &a,
                     const std::pair<uint32_t, amxd_object_t *> &b) { return a.first > b.first; });

        size_t selected_bss_count = 0;
        for (const auto &entry : bss_ordered) {
            if (selected_bss_count >= max_supported_bss) {
                LOG(INFO) << "Reached maximum number of BSS capacity (" << max_supported_bss
                          << "). Remaining BSSTemplates will be ignored.";
                break;
            }

            uint32_t priority                = entry.first;
            amxd_object_t *bss_template_inst = entry.second;

            son::wireless_utils::sBssInfoConf bss_info;
            bss_info.ssid = get_param_string(bss_template_inst, "SSID");

            bool bss_template_enable = amxd_object_get_bool(bss_template_inst, "Enable", NULL);
            std::string linked_ssc_template_id =
                get_param_string(bss_template_inst, "LinkedSSCTemplateID");
            std::string linked_radio_template_id =
                get_param_string(bss_template_inst, "LinkedRadioTemplateID");

            if (!linked_ssc_template_id.empty() && !linked_radio_template_id.empty()) {
                // The BSS should be enabled for operation, and both related RadioTemplate & SSCTemplate should be set to true.
                bool new_enable_value = network_enable && bss_template_enable &&
                                        ssc_template_status[linked_ssc_template_id] &&
                                        radio_template_status[linked_radio_template_id];

                if (!new_enable_value) {
                    LOG(DEBUG) << "Skipping BSS \"" << bss_info.ssid
                               << "\" because effective enable=false"
                               << " (network=" << (network_enable ? "true" : "false")
                               << ", bss_enable=" << (bss_template_enable ? "true" : "false")
                               << ", ssc_enable="
                               << (ssc_template_status[linked_ssc_template_id] ? "true" : "false")
                               << ", radio_enable="
                               << (radio_template_status[linked_radio_template_id] ? "true"
                                                                                   : "false")
                               << ")";
                    continue;
                }
                LOG(DEBUG) << "Enabling AP with ssid: " << bss_info.ssid
                           << ", linked SSC ID: " << linked_ssc_template_id
                           << ", linked Radio ID: " << linked_radio_template_id
                           << ", Priority: " << priority;

            } else {
                LOG(WARNING)
                    << "AccessPoint:" << bss_info.ssid
                    << " has an empty LinkedSSCTemplateID or LinkedRadioTemplateID parameters!";
                if (!bss_template_enable) {
                    LOG(DEBUG) << "Skipping BSS \"" << bss_info.ssid
                               << "\" because BSS is disabled and linkage missing";
                    continue;
                }
            }

            // Multi-AP mode configuration
            auto haul_type = get_param_string(
                ssc_template_object, filter_param_name_with_linked_template_id(
                                         "SSCTemplateID", linked_ssc_template_id, "HaulType")
                                         .c_str());

            bss_info.backhaul  = (haul_type.find("Backhaul") != std::string::npos);
            bss_info.fronthaul = (haul_type.find("Fronthaul") != std::string::npos);
            LOG(DEBUG) << "HaulType=\"" << (haul_type.empty() ? "[empty]" : haul_type)
                       << "\" => backhaul=" << (bss_info.backhaul ? "true" : "false")
                       << ", fronthaul=" << (bss_info.fronthaul ? "true" : "false");

            if (!bss_info.backhaul && !bss_info.fronthaul) {
                LOG(DEBUG) << "MultiAp Mode for AccessPoint: \"" << bss_info.ssid
                           << "\" is not set. Skipping.";
                continue;
            }

            // Operating classes configuration
            std::string band_flag = get_param_string(
                radio_template_object, filter_param_name_with_linked_template_id(
                                           "RadioTemplateID", linked_radio_template_id, "BandFlag")
                                           .c_str());

            if (band_flag.empty()) {
                std::string op_class_flag =
                    get_param_string(radio_template_object,
                                     filter_param_name_with_linked_template_id(
                                         "RadioTemplateID", linked_radio_template_id, "OpclassFlag")
                                         .c_str());
                LOG(DEBUG) << "Using OpclassFlag=\""
                           << (op_class_flag.empty() ? "[empty]" : op_class_flag)
                           << "\" for ssid=\"" << bss_info.ssid << "\"";
                bss_info.operating_class.splice(
                    bss_info.operating_class.end(),
                    son::wireless_utils::parse_op_class_flag_to_wsc_oper_class(op_class_flag));

            } else {
                LOG(DEBUG) << "Using BandFlag=\"" << band_flag << "\" for ssid=\"" << bss_info.ssid
                           << "\"";
                auto bands = parse_and_validate_band_flag(band_flag);
                if (bands.empty()) {
                    LOG(WARNING) << "No valid frequency band was provided for ssid=\""
                                 << bss_info.ssid << "\". Skipping.";
                    continue;
                }

                for (const auto &band : bands) {
                    bss_info.operating_class.splice(
                        bss_info.operating_class.end(),
                        son::wireless_utils::string_to_wsc_oper_class(band_flag_to_str(band)));
                }
            }

            if (bss_info.operating_class.empty()) {
                LOG(WARNING) << "Operating classes for Access Point: \"" << bss_info.ssid
                             << "\" is not set. Skipping.";
                continue;
            }

            // Security Configuration
            amxd_object_t *security_group_object = amxd_object_get_child(object, "SecurityGroup");
            if (!security_group_object) {
                LOG(WARNING) << "Failed to get SecurityGroup object from Templates object!";
                return amxd_status_object_not_found;
            }

            std::string linked_security_Group_id_in_bss_template =
                get_param_string(bss_template_inst, "LinkedSecurityGroupID");
            LOG(DEBUG) << "SecurityGroup link in BSS \"" << bss_info.ssid << "\": id="
                       << (linked_security_Group_id_in_bss_template.empty()
                               ? "[empty]"
                               : linked_security_Group_id_in_bss_template);

            std::string linked_security_Group_id_in_security_group =
                get_param_string(security_group_object,
                                 filter_param_name_with_linked_template_id(
                                     "SecurityGroupID", linked_security_Group_id_in_bss_template,
                                     "LinkedSecurityGroupID")
                                     .c_str());

            if (!linked_security_Group_id_in_security_group.empty()) {

                amxd_object_t *security_template_object =
                    amxd_object_get_child(object, "SecurityTemplate");
                if (!security_template_object) {
                    LOG(WARNING) << "Failed to get SecurityTemplate object from Templates object!";
                    return amxd_status_object_not_found;
                }

                std::string sec_tmpl_id = find_security_template_id_with_the_highest_priority(
                    security_template_object, linked_security_Group_id_in_security_group);

                LOG(DEBUG) << "Chosen SecurityTemplateID for ssid=\"" << bss_info.ssid
                           << "\": " << (sec_tmpl_id.empty() ? "[none]" : sec_tmpl_id);

                if (!sec_tmpl_id.empty()) {
                    std::string akm_suite_in_rsne =
                        get_param_string(security_template_object,
                                         filter_param_name_with_linked_template_id(
                                             "SecurityTemplateID", sec_tmpl_id, "RSNE.AKMSuite")
                                             .c_str());
                    LOG(DEBUG) << "AKMSuite for ssid=\"" << bss_info.ssid << "\": \""
                               << (akm_suite_in_rsne.empty() ? "[empty]" : akm_suite_in_rsne)
                               << "\"";

                    // Determine authentication mode based on Device.WiFi.Templates.SecurityTemplate.{i}.RSNE.AKMSuite
                    std::vector<AKMType> akm_suite_list =
                        parse_and_validate_akm_suite(akm_suite_in_rsne);

                    std::string key_pass_phrase =
                        get_param_string(bss_template_inst, "KeyPassphrase");
                    LOG(DEBUG) << "Passphrase present="
                               << (!key_pass_phrase.empty() ? "yes" : "no");

                    beerocks::SecurityMode security_mode =
                        configure_security_parameters_in_bss_info(bss_info, akm_suite_list,
                                                                  key_pass_phrase);

                    if (bss_info.authentication_type != WSC::eWscAuth::WSC_AUTH_OPEN &&
                        bss_info.network_key.empty()) {
                        LOG(WARNING) << "BSS: " << bss_info.ssid
                                     << " with mode: " << security_mode_to_string(security_mode)
                                     << " missing value for network key. Skipping.";
                        continue;
                    }

                } else {
                    LOG(WARNING) << "Failed to find a valid Security Template to apply for ssid=\""
                                 << bss_info.ssid << "\". Skipping.";
                    continue;
                }

            } else {
                //For each selected BSS Template, the Configuration Template Manager shall select a Security Template
                LOG(WARNING)
                    << "Linked Security Template to SecurityGroup object is empty for ssid=\""
                    << bss_info.ssid << "\". Skipping.";
                continue;
            }

            LOG(DEBUG) << "Add bss info configuration for ssid: " << bss_info.ssid
                       << " operating classes: " << bss_info.operating_class;

            std::string operating_generation = get_param_string(
                radio_template_object,
                filter_param_name_with_linked_template_id(
                    "RadioTemplateID", linked_radio_template_id, "OperatingGeneration")
                    .c_str());
            LOG(DEBUG) << "Operating Generation for ssid=\"" << bss_info.ssid << "\": "
                       << (operating_generation.empty() ? "[empty]" : operating_generation);

            auto gens = get_wifi_generations(operating_generation);
            if (gens.empty()) {
                LOG(DEBUG) << "No valid Wi-Fi generations parsed for ssid=\"" << bss_info.ssid
                           << "\"";
            } else {
                bool wifi7_deployed = std::find(gens.begin(), gens.end(), "7") != gens.end();

                if (wifi7_deployed) {
                    LOG(INFO) << "[MLO] Wi-Fi 7 detected -> Will activate MLO if enabled in APMLD "
                                 "Template (ssid=\""
                              << bss_info.ssid << "\")";
                } else {
                    LOG(INFO) << "[MLO] Wi-Fi 7 not deployed -> MLO remains disabled (ssid=\""
                              << bss_info.ssid << "\")";
                }

                // Configure MLD if applicable
                std::string linked_apmld_template_id =
                    get_param_string(bss_template_inst, "LinkedAPMLDTemplateID");
                LOG(DEBUG) << "Linked APMLDTemplateID for ssid=\"" << bss_info.ssid << "\": "
                           << (linked_apmld_template_id.empty() ? "[empty]"
                                                                : linked_apmld_template_id);

                if (!linked_apmld_template_id.empty()) {
                    bool apmld_template_enable = apmld_template_status[linked_apmld_template_id];
                    if (apmld_template_enable && wifi7_deployed) {
                        son::wireless_utils::sMldInfoConf mld_info;
                        mld_info.ssid = bss_info.ssid;
                        LOG(DEBUG) << "[MLO] APMLD Template enabled -> MLO activated (ssid=\""
                                   << bss_info.ssid << "\")";
                        // Read Modes from Configuration
                        mld_info.str = get_param_bool(
                            apmld_template_object,
                            filter_param_name_with_linked_template_id(
                                "APMLDTemplateID", linked_apmld_template_id, "STREnable")
                                .c_str());
                        LOG(DEBUG) << "MLD STR=" << (mld_info.str ? "enabled" : "disabled")
                                   << " (ssid=\"" << mld_info.ssid << "\")";
                        mld_info.nstr = get_param_bool(
                            apmld_template_object,
                            filter_param_name_with_linked_template_id(
                                "APMLDTemplateID", linked_apmld_template_id, "NSTREnable")
                                .c_str());
                        LOG(DEBUG) << "MLD NSTR=" << (mld_info.nstr ? "enabled" : "disabled")
                                   << " (ssid=\"" << mld_info.ssid << "\")";
                        mld_info.emlsr = get_param_bool(
                            apmld_template_object,
                            filter_param_name_with_linked_template_id(
                                "APMLDTemplateID", linked_apmld_template_id, "EMLSREnable")
                                .c_str());
                        LOG(DEBUG) << "MLD EMLSR=" << (mld_info.emlsr ? "enabled" : "disabled")
                                   << " (ssid=\"" << mld_info.ssid << "\")";
                        mld_info.emlmr = get_param_bool(
                            apmld_template_object,
                            filter_param_name_with_linked_template_id(
                                "APMLDTemplateID", linked_apmld_template_id, "EMLMREnable")
                                .c_str());
                        LOG(DEBUG) << "MLD EMLMR=" << (mld_info.emlmr ? "enabled" : "disabled")
                                   << " (ssid=\"" << mld_info.ssid << "\")";

                        g_database->add_mld_info_configuration(mld_info, linked_apmld_template_id);
                        // Update mld_id in bss_info to ensure each BSS entry is correctly linked to its corresponding MLD unit.
                        bss_info.mld_id = linked_apmld_template_id;
                        LOG(DEBUG) << "Set MLD ID: " << bss_info.mld_id
                                   << " for BSS with SSID: " << bss_info.ssid;
                    } else {
                        LOG(INFO) << "[MLO] APMLD Template disabled or Wi-Fi 7 not deployed -> MLO "
                                     "disabled (ssid=\""
                                  << bss_info.ssid << "\")";
                    }
                }
            }
            // Add the configured BSS info to the database
            g_database->add_bss_info_configuration(bss_info);
            selected_bss_count++;
            LOG(DEBUG) << " BSS staged: ssid=\"" << bss_info.ssid
                       << "\" (selected=" << selected_bss_count << "/" << max_supported_bss << ")";
        }
    } else {
        LOG(INFO) << " Network is disabled. Skipping BSS selection/staging.";
    }

    // --------- Push configuration to agents ---------
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];
    ieee1905_1::CmduMessageTx cmdu_tx(m_tx_buffer, sizeof(m_tx_buffer));
    auto connected_agents = g_database->get_all_connected_agents();

    LOG(DEBUG) << "Connected agents eligible for AP Config Renew: " << connected_agents.size();

    if (!connected_agents.empty()) {
        if (!son_actions::send_ap_config_renew_msg(cmdu_tx, *g_database)) {
            LOG(ERROR) << "Failed son_actions::send_ap_config_renew_msg ! ";
        } else {
            LOG(INFO) << "AP Config Renew message sent to " << connected_agents.size()
                      << " agent(s)";
        }
    } else {
        LOG(INFO) << "No connected agents -> Skipping AP Config Renew message";
    }

    return amxd_status_ok;
}

/**
 * @brief Renew configurations on agents.
 *
 * send_ap_config_renew is invoked when new configurations need to be propagated to agents.
 */
bool send_ap_config_renew()
{
    uint8_t tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];
    ieee1905_1::CmduMessageTx cmdu_tx(tx_buffer, sizeof(tx_buffer));
    auto connected_agents = g_database->get_all_connected_agents();

    if (!connected_agents.empty()) {
        if (!son_actions::send_ap_config_renew_msg(cmdu_tx, *g_database)) {
            LOG(ERROR) << "Failed son_actions::send_ap_config_renew_msg ! ";
            return false;
        }
    }
    return true;
}

/**
 * @brief Event handler for controller configuration change.
 *
 * event_configuration_changed is invoked when value of parameter
 * in CONTROLLER_ROOT_DM.Configuration object changes with set command.
 */
static void event_configuration_changed(const char *const sig_name, const amxc_var_t *const data,
                                        void *const priv)
{
    amxd_object_t *configuration =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!configuration) {
        LOG(WARNING) << "Failed to get object " CONTROLLER_ROOT_DM ".Configuration";
        return;
    }

    son::db::sDbNbapiConfig nbapi_config;
    nbapi_config.client_band_steering =
        amxd_object_get_bool(configuration, "BandSteeringEnabled", nullptr);
    nbapi_config.client_11k_roaming =
        amxd_object_get_bool(configuration, "Client11kRoamingEnabled", nullptr);
    nbapi_config.client_optimal_path_roaming =
        amxd_object_get_bool(configuration, "ClientRoamingEnabled", nullptr);
    nbapi_config.roaming_hysteresis_percent_bonus =
        amxd_object_get_int32_t(configuration, "SteeringCurrentBonus", nullptr);
    nbapi_config.steering_disassoc_timer_msec = std::chrono::milliseconds{
        amxd_object_get_int32_t(configuration, "SteeringDisassociationTimerMSec", nullptr)};
    nbapi_config.link_metrics_request_interval_seconds = std::chrono::seconds{
        amxd_object_get_int32_t(configuration, "LinkMetricsRequestIntervalSec", nullptr)};

    nbapi_config.channel_select_task =
        amxd_object_get_bool(configuration, "ChannelSelectionTaskEnabled", nullptr);

    nbapi_config.ire_roaming =
        amxd_object_get_bool(configuration, "BackhaulOptimizationEnabled", nullptr);

    nbapi_config.dynamic_channel_select_task =
        amxd_object_get_bool(configuration, "DynamicChannelSelectionTaskEnabled", nullptr);

    nbapi_config.load_balancing =
        amxd_object_get_bool(configuration, "LoadBalancingTaskEnabled", nullptr);

    nbapi_config.optimal_path_prefer_signal_strength =
        amxd_object_get_bool(configuration, "OptimalPathPreferSignalStrength", nullptr);

    nbapi_config.health_check =
        amxd_object_get_bool(configuration, "HealthCheckTaskEnabled", nullptr);

    nbapi_config.diagnostics_measurements =
        amxd_object_get_bool(configuration, "StatisticsPollingTaskEnabled", nullptr);

    nbapi_config.diagnostics_measurements_polling_rate_sec =
        amxd_object_get_int32_t(configuration, "StatisticsPollingRateSec", nullptr);

    nbapi_config.enable_dfs_reentry =
        amxd_object_get_bool(configuration, "DFSReentryEnabled", nullptr);

    nbapi_config.daisy_chaining_disabled =
        amxd_object_get_bool(configuration, "DaisyChainingDisabled", nullptr);

    // Send config renew if setting is changed
    if (nbapi_config.daisy_chaining_disabled != g_database->settings_daisy_chaining_disabled()) {
        send_ap_config_renew();
    }

    if (!g_database->update_master_configuration(nbapi_config)) {
        LOG(ERROR) << "Failed update master configuration from NBAPI.";
    }

    if (!g_database->dm_update_collection_intervals(
            nbapi_config.link_metrics_request_interval_seconds)) {
        LOG(ERROR) << "Failed update collection intervals of all agents.";
    }

    auto controller_ctx = g_database->get_controller_ctx();

    if (!controller_ctx) {
        LOG(WARNING) << "Failed to get controller context.";
    } else {
        LOG(DEBUG) << "Start/Stop reconfigured optional tasks";
        controller_ctx->start_optional_tasks();
    }

    // TODO Save persistent settings with amxo_parser_save() (PPM-1419)
}

/**
 * @brief Event handler for controller Group change.
 *
 * event_group_enable_changed is invoked when value of parameter Device.WiFi.DataElements.Network.Group.X.Enable is changed
 * 
 */

static void event_network_group_changed(const char *const sig_name, const amxc_var_t *const data,
                                        void *const priv)
{
    amxd_object_t *group = amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!group) {
        LOG(WARNING) << "Failed to get object Device.WiFi.DataElements.Network.Group.X.";
        return;
    }

    access_point_commit(amxd_object_get_parent(amxd_object_get_parent(group)), nullptr, nullptr,
                        nullptr);
}

/**
 * @brief Event handler for templates change.
 * event_templates_network_configuration_changed is invoked when value of a parameter of
 * Device.WiFi.Templates.Network is changed with set command.
 */
static void event_templates_network_configuration_changed(const char *const sig_name,
                                                          const amxc_var_t *const data,
                                                          void *const priv)
{
    amxd_object_t *templates_network_configuration =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!templates_network_configuration) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.Network";
        return;
    }

    g_database->templates_network_config.enable =
        amxd_object_get_bool(templates_network_configuration, "Enable", nullptr);
    g_database->templates_network_config.topology_flag =
        get_param_string(templates_network_configuration, "TopologyFlag");

    LOG(INFO) << "New configuration Templates received ; Enable: "
              << g_database->templates_network_config.enable
              << ", TopologyFlag: " << g_database->templates_network_config.topology_flag;

    templates_commit(amxd_object_get_parent(templates_network_configuration), nullptr, nullptr,
                     nullptr);
}

/**
 * @brief Event handler for BSSTemplate change.
 *
 * event_bss_template_configuration_changed is invoked when BSSTemplate object is modified
 *
 */
static void event_bss_template_configuration_changed(const char *const sig_name,
                                                     const amxc_var_t *const data, void *const priv)
{
    amxd_object_t *bss_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!bss_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.BSSTemplate.X";
        return;
    }

    templates_commit(amxd_object_get_parent(amxd_object_get_parent(bss_template)), nullptr, nullptr,
                     nullptr);
}

/**
 * @brief Event handler for BSSTemplate instance's change.
 *
 * event_bss_template_instance_changed is invoked when BSSTemplate instances are modified (i.e. added or removed)
 *
 */
static void event_bss_template_instance_changed(const char *const sig_name,
                                                const amxc_var_t *const data, void *const priv)
{
    amxd_object_t *bss_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!bss_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.BSSTemplate";
        return;
    }

    templates_commit(amxd_object_get_parent(bss_template), nullptr, nullptr, nullptr);
}

/**
 * @brief Event handler for RadioTemplate change.
 *
 * event_radio_template_configuration_changed is invoked when RadioTemplate object is modified
 *
 */
static void event_radio_template_configuration_changed(const char *const sig_name,
                                                       const amxc_var_t *const data,
                                                       void *const priv)
{
    amxd_object_t *radio_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!radio_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.RadioTemplate.X";
        return;
    }

    templates_commit(amxd_object_get_parent(amxd_object_get_parent(radio_template)), nullptr,
                     nullptr, nullptr);
}

/**
 * @brief Event handler for RadioTemplate instance's change.
 *
 * event_radio_template_instance_changed is invoked when RadioTemplate instances are modified (i.e. added or removed)
 *
 */
static void event_radio_template_instance_changed(const char *const sig_name,
                                                  const amxc_var_t *const data, void *const priv)
{
    amxd_object_t *radio_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!radio_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.RadioTemplate";
        return;
    }

    templates_commit(amxd_object_get_parent(radio_template), nullptr, nullptr, nullptr);
}

/**
 * @brief Event handler for SSCTemplate change.
 *
 * event_ssc_template_configuration_changed is invoked when SSCTemplate object is modified
 *
 */
static void event_ssc_template_configuration_changed(const char *const sig_name,
                                                     const amxc_var_t *const data, void *const priv)
{
    amxd_object_t *ssc_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!ssc_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.SSCTemplate.X";
        return;
    }

    templates_commit(amxd_object_get_parent(amxd_object_get_parent(ssc_template)), nullptr, nullptr,
                     nullptr);
}

/**
 * @brief Event handler for SSCTemplate instance's change.
 *
 * event_ssc_template_instance_changed is invoked when SSCTemplate instances are modified (i.e. added or removed)
 *
 */
static void event_ssc_template_instance_changed(const char *const sig_name,
                                                const amxc_var_t *const data, void *const priv)
{
    amxd_object_t *ssc_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!ssc_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.SSCTemplate";
        return;
    }

    templates_commit(amxd_object_get_parent(ssc_template), nullptr, nullptr, nullptr);
}

/**
 * @brief Event handler for SecurityTemplate change.
 *
 * event_security_template_configuration_changed is invoked when SecurityTemplate object is modified
 *
 */
static void event_security_template_configuration_changed(const char *const sig_name,
                                                          const amxc_var_t *const data,
                                                          void *const priv)
{
    amxd_object_t *security_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!security_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.SecurityTemplate.X";
        return;
    }

    templates_commit(amxd_object_get_parent(amxd_object_get_parent(security_template)), nullptr,
                     nullptr, nullptr);
}

/**
 * @brief Event handler for SecurityTemplate instance's change.
 *
 * event_security_template_instance_changed is invoked when SecurityTemplate instances are modified (i.e. added or removed)
 *
 */
static void event_security_template_instance_changed(const char *const sig_name,
                                                     const amxc_var_t *const data, void *const priv)
{
    amxd_object_t *security_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!security_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.SecurityTemplate";
        return;
    }

    templates_commit(amxd_object_get_parent(security_template), nullptr, nullptr, nullptr);
}

/**
 * @brief Event handler for SecurityGroup change.
 *
 * event_templates_security_group_configuration_changed is invoked when SecurityGroup object is modified
 *
 */
static void event_templates_security_group_configuration_changed(const char *const sig_name,
                                                                 const amxc_var_t *const data,
                                                                 void *const priv)
{
    amxd_object_t *security_group =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!security_group) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.SecurityGroup.X";
        return;
    }

    templates_commit(amxd_object_get_parent(amxd_object_get_parent(security_group)), nullptr,
                     nullptr, nullptr);
}

/**
 * @brief Event handler for SecurityGroup instance's change.
 *
 * event_templates_security_group_instance_changed is invoked when SecurityGroup instances are modified (i.e. added or removed)
 *
 */
static void event_templates_security_group_instance_changed(const char *const sig_name,
                                                            const amxc_var_t *const data,
                                                            void *const priv)
{
    amxd_object_t *security_group =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!security_group) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.SecurityGroup";
        return;
    }

    templates_commit(amxd_object_get_parent(security_group), nullptr, nullptr, nullptr);
}

/**
 * @brief Event handler for APMLDTemplate change.
 *
 * event_apmld_template_configuration_changed is invoked when APMLDTemplate object is modified
 *
 */
static void event_apmld_template_configuration_changed(const char *const sig_name,
                                                       const amxc_var_t *const data,
                                                       void *const priv)
{
    amxd_object_t *apmld_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!apmld_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.APMLDTemplate.X";
        return;
    }

    templates_commit(amxd_object_get_parent(amxd_object_get_parent(apmld_template)), nullptr,
                     nullptr, nullptr);
}

/**
 * @brief Event handler for APMLDTemplate instance's change.
 *
 * event_apmld_template_instance_changed is invoked when APMLDTemplate instances are modified (i.e. added or removed)
 *
 */
static void event_apmld_template_instance_changed(const char *const sig_name,
                                                  const amxc_var_t *const data, void *const priv)
{
    amxd_object_t *apmld_template =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!apmld_template) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.APMLDTemplate";
        return;
    }

    templates_commit(amxd_object_get_parent(apmld_template), nullptr, nullptr, nullptr);
}

/**
 * @brief Event handler for controller Network.Enable change.
 *
 * event_group_enable_changed is invoked when value of parameter Device.WiFi.DataElements.Network.Enable is changed
 * 
 */

static void event_network_enable_changed(const char *const sig_name, const amxc_var_t *const data,
                                         void *const priv)
{
    amxd_object_t *network_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!network_obj) {
        LOG(WARNING) << "Failed to get object Device.WiFi.DataElements.Network.";
        return;
    }

    access_point_commit(network_obj, nullptr, nullptr, nullptr);
}

std::vector<beerocks::nbapi::sActionsCallback> get_actions_callback_list(void)
{
    const std::vector<beerocks::nbapi::sActionsCallback> actions_list = {
        {"action_read_assoc_time", action_read_assoc_time},
        {"action_read_last_change", action_read_last_change},
        {"action_last_steer_time", action_last_steer_time},
    };
    return actions_list;
}

std::vector<beerocks::nbapi::sEvents> get_events_list(void)
{
    const std::vector<beerocks::nbapi::sEvents> events_list = {
        {"event_configuration_changed", event_configuration_changed},
        {"event_network_group_changed", event_network_group_changed},
        {"event_templates_network_configuration_changed",
         event_templates_network_configuration_changed},
        {"event_bss_template_configuration_changed", event_bss_template_configuration_changed},
        {"event_bss_template_instance_changed", event_bss_template_instance_changed},
        {"event_radio_template_configuration_changed", event_radio_template_configuration_changed},
        {"event_radio_template_instance_changed", event_radio_template_instance_changed},
        {"event_ssc_template_configuration_changed", event_ssc_template_configuration_changed},
        {"event_ssc_template_instance_changed", event_ssc_template_instance_changed},
        {"event_security_template_configuration_changed",
         event_security_template_configuration_changed},
        {"event_security_template_instance_changed", event_security_template_instance_changed},
        {"event_templates_security_group_configuration_changed",
         event_templates_security_group_configuration_changed},
        {"event_templates_security_group_instance_changed",
         event_templates_security_group_instance_changed},
        {"event_apmld_template_configuration_changed", event_apmld_template_configuration_changed},
        {"event_apmld_template_instance_changed", event_apmld_template_instance_changed},
        {"event_network_enable_changed", event_network_enable_changed}};
    return events_list;
}

std::vector<beerocks::nbapi::sFunctions> get_func_list(void)
{
    const std::vector<beerocks::nbapi::sFunctions> functions_list = {
        {"access_point_commit", DATAELEMENTS_ROOT_DM ".Network.AccessPointCommit",
         access_point_commit},
        {"client_steering", DATAELEMENTS_ROOT_DM ".Network.ClientSteering", client_steering},
        {"trigger_scan", DATAELEMENTS_ROOT_DM ".Network.Device.Radio.ScanTrigger", trigger_scan},
        {"BTMRequest", DATAELEMENTS_ROOT_DM ".Network.Device.Radio.BSS.STA.MultiAPSTA.BTMRequest",
         btm_request},
        {"trigger_set_spatial_reuse", DATAELEMENTS_ROOT_DM ".Network.Device.Radio.SetSpatialReuse",
         trigger_set_spatial_reuse},
        {"update_vbss_capabilities", DATAELEMENTS_ROOT_DM ".Network.Device.UpdateVBSSCapabilities",
         update_vbss_capabilities},
        {"trigger_vbss_creation", DATAELEMENTS_ROOT_DM ".Network.Device.Radio.TriggerVBSSCreation",
         trigger_vbss_creation},
        {"trigger_vbss_destruction",
         DATAELEMENTS_ROOT_DM ".Network.Device.Radio.BSS.TriggerVBSSDestruction",
         trigger_vbss_destruction},
        {"trigger_vbss_move", DATAELEMENTS_ROOT_DM ".Network.Device.Radio.BSS.TriggerVBSSMove",
         trigger_vbss_move},
        {"set_eht_operations", DATAELEMENTS_ROOT_DM ".Network.Device.Radio.BSS.SetEHTOperations",
         set_eht_operations},
        {"trigger_prioritization", DATAELEMENTS_ROOT_DM ".Network.SetServicePrioritization",
         trigger_prioritization},
        {"add_unassociated_station",
         DATAELEMENTS_ROOT_DM ".Network.Device.Radio.AddUnassociatedStation",
         add_unassociated_station},
        {"remove_unassociated_station",
         DATAELEMENTS_ROOT_DM ".Network.Device.Radio.RemoveUnassociatedStation",
         remove_unassociated_station},
        {"update_unassociatedStations_stats",
         DATAELEMENTS_ROOT_DM ".Network.UpdateUnassociatedStationsStats",
         update_unassociatedStations_stats}};
    return functions_list;
}

beerocks::nbapi::ambiorix_func_ptr get_access_point_commit(void) { return access_point_commit; }

} // namespace actions
} // namespace controller
} // namespace prplmesh
