/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "on_action.h"

#include <algorithm>
#include <bcl/beerocks_defines.h>
#include <bcl/beerocks_qos_utils.h>
#include <bcl/beerocks_string_utils.h>
#include <beerocks/tlvf/beerocks_message_bml.h>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <locale.h>
#include <sstream>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace beerocks;
using namespace net;
using namespace son;
namespace prplmesh {
namespace controller {
namespace actions {

// Actions

son::db *g_database = nullptr;

static constexpr const char X_PRPLWARE_COM_REASON[] = "X_PRPLWARE-COM_Reason";

static constexpr const char CHAN_SEL_REQ_STATUS_KEY[]                 = "Status";
static constexpr const char CHAN_SEL_REQ_STATUS_SUCCESS[]             = "Success";
static constexpr const char CHAN_SEL_REQ_STATUS_ERROR_INVALID_INPUT[] = "Error_Invalid_Input";
static constexpr const char CHAN_SEL_REQ_STATUS_ERROR_TIMEOUT[]       = "Error_Timeout";
static constexpr const char CHAN_SEL_REQ_STATUS_ERROR_OTHER[]         = "Error_Other";

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
    amxd_object_t *group_object = amxd_object_get_child(object, "X_PRPLWARE-COM_Group");
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
                get_param_string(access_point_inst, "X_PRPLWARE-COM_VapType"));

            bool access_point_enable = amxd_object_get_bool(access_point_inst, "Enable", NULL);
            std::string group_name =
                get_param_string(access_point_inst, "X_PRPLWARE-COM_GroupName");
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

static void chan_sel_req_status(amxc_var_t *ret, const char *status, const char *reason)
{
    if (reason) {
        LOG(ERROR) << reason;
    }

    amxc_var_set_type(ret, AMXC_VAR_ID_HTABLE);
    amxc_var_add_key(cstring_t, ret, CHAN_SEL_REQ_STATUS_KEY, status);

    if (reason) {
        amxc_var_add_key(cstring_t, ret, X_PRPLWARE_COM_REASON, reason);
    }
}

static void chan_sel_req_invalid_param(amxc_var_t *ret, const char *reason)
{
    chan_sel_req_status(ret, CHAN_SEL_REQ_STATUS_ERROR_INVALID_INPUT, reason);
}

static void chan_sel_req_unknown_error(amxc_var_t *ret, const char *reason)
{
    chan_sel_req_status(ret, CHAN_SEL_REQ_STATUS_ERROR_OTHER, reason);
}

static void chan_sel_req_success(amxc_var_t *ret)
{
    chan_sel_req_status(ret, CHAN_SEL_REQ_STATUS_SUCCESS, nullptr);
}

static bool chan_sel_req_get_al_mac(amxd_object_t *object, sMacAddr *al_mac,
                                    std::stringstream *reason)
{
    // Device.WiFi.DataElements.Network.Device.1.Radio.1
    //  into
    // Device.WiFi.DataElements.Network.Device.1.ID

    amxd_object_t *radio_table = amxd_object_get_parent(object);
    if (!radio_table) {
        *reason << "Failed to get parent of radio object";
        return false;
    }

    amxd_object_t *device_instance = amxd_object_get_parent(radio_table);
    if (!device_instance) {
        *reason << "Failed to get parent of radio table object";
        return false;
    }

    const amxc_var_t *device_id_param = amxd_object_get_param_value(device_instance, "ID");
    if (!device_id_param) {
        *reason << "Failed to get 'ID' parameter of device instance object";
        return false;
    }

    const char *device_id_cstr = amxc_var_constcast(cstring_t, device_id_param);
    if (!device_id_cstr) {
        *reason << "Failed to get 'ID' parameter of device instance object as string";
        return false;
    }

    *al_mac = tlvf::mac_from_string(device_id_cstr);
    if (*al_mac == network_utils::ZERO_MAC) {
        *reason << "AL MAC is zero, that's not expected";
        return false;
    }

    return true;
}

static bool chan_sel_req_get_radio_mac(amxd_object_t *object, sMacAddr *radio_mac,
                                       std::stringstream *reason)
{
    // Device.WiFi.DataElements.Network.Device.1.Radio.1
    //  into
    // Device.WiFi.DataElements.Network.Device.1.Radio.1.ID

    const amxc_var_t *radio_mac_param = amxd_object_get_param_value(object, "ID");
    if (!radio_mac_param) {
        *reason << "Failed to get radio MAC address object";
        return false;
    }

    const char *radio_mac_cstr = amxc_var_constcast(cstring_t, radio_mac_param);
    if (!radio_mac_cstr) {
        *reason << "Failed to get radio MAC address as string value";
        return false;
    }

    *radio_mac = tlvf::mac_from_string(radio_mac_cstr);
    if (*radio_mac == network_utils::ZERO_MAC) {
        *reason << "Radio MAC is zero, that's not expected";
        return false;
    }

    return true;
}

static bool chan_sel_req_parse_classes(const amxc_var_t *classes, Controller::ChanPref *preferences,
                                       std::stringstream *reason)
{
    if (classes) {
        const bool no_instances = amxc_htable_size(amxc_var_constcast(amxc_htable_t, classes)) == 0;
        if (no_instances) {
            *reason << "'Class' table is empty, at least one Class instance is expected";
            return false;
        }
    } else {
        // `classes` can be NULL. amxc_var_for_each() is NULL-safe. It will
        // perform 0 iterations, as if the list is empty. That's intended. No
        // need in allocating dummy on-stack empty htable.
    }

    amxc_var_for_each(class_instance, classes)
    {
        const char *class_key = amxc_var_key(class_instance);
        if (!class_key) {
            // Can this really happen? Probably no, but defensive null check it is.
            *reason << "Empty class key for 'Class' instance";
            return false;
        }

        *reason << "Class." << class_key;

        const amxc_var_t *opclass_var = GET_ARG(class_instance, "OpClass");
        if (!opclass_var) {
            *reason << ": Missing 'OpClass' parameter";
            return false;
        }

        const uint8_t opclass_val = amxc_var_dyncast(uint8_t, opclass_var);
        if (!opclass_val) {
            char *opclass_str = amxc_var_get_cstring_t(opclass_var);
            *reason << ": Invalid 'OpClass' value" << opclass_str;
            free(opclass_str);
            return false;
        }

        const amxc_var_t *channels = GET_ARG(class_instance, "Channel");
        if (!channels) {
            *reason << ": Missing 'Channel' table";
            return false;
        }

        const bool no_instances =
            amxc_htable_size(amxc_var_constcast(amxc_htable_t, channels)) == 0;
        if (no_instances) {
            *reason << ": 'Channel' table is empty, at least one channel instance is expected";
            return false;
        }

        amxc_var_for_each(channel_instance, channels)
        {
            const char *channel_key = amxc_var_key(channel_instance);
            if (!channel_key) {
                *reason << ": Empty channel key";
                return false;
            }

            *reason << ".Channel." << channel_key;

            const amxc_var_t *channel_num_var = GET_ARG(channel_instance, "Channel");
            if (!channel_num_var) {
                *reason << ": Missing 'Channel' parameter";
                return false;
            }

            const amxc_var_t *channel_pref_var = GET_ARG(channel_instance, "Preference");
            if (!channel_pref_var) {
                *reason << ": Missing 'Preference' parameter";
                return false;
            }

            const uint8_t channel_num = amxc_var_dyncast(uint8_t, channel_num_var);
            if (!channel_num) {
                char *channel_str = amxc_var_get_cstring_t(channel_num_var);
                *reason << ": Invalid 'Channel' value: " << (channel_str ?: "?");
                free(channel_str);
                return false;
            }

            amxc_var_t casted_pref_var;
            amxc_var_init(&casted_pref_var);
            const int pref_convert_result =
                amxc_var_convert(&casted_pref_var, channel_pref_var, AMXC_VAR_ID_UINT8);
            const uint8_t pref = amxc_var_constcast(uint8_t, &casted_pref_var);
            amxc_var_clean(&casted_pref_var);

            const bool pref_convert_failed = (pref_convert_result != 0);
            if (pref_convert_failed) {
                char *pref_str = amxc_var_get_cstring_t(channel_pref_var);
                *reason << ": Invalid 'Preference' value: " << (pref_str ?: "?");
                free(pref_str);
                return false;
            }

            constexpr const uint8_t max_pref = (uint8_t)eChannelPreferenceRankingConsts::BEST;
            const bool pref_within_range     = (pref <= max_pref);
            if (pref_within_range == false) {
                *reason << ": 'Preference' value out of range: " << static_cast<int>(pref)
                        << ", expected 0-" << static_cast<int>(max_pref);
                return false;
            }

            const Controller::ChanPrefKey pref_key = {
                static_cast<Controller::ChanPrefOpClass>(opclass_val),
                static_cast<Controller::ChanPrefValue>(pref),
            };
            const Controller::ChanPrefChanNum channel_num_casted =
                static_cast<Controller::ChanPrefChanNum>(channel_num);
            (*preferences)[pref_key].push_back(channel_num_casted);
        }
    }

    return true;
}

/**
 * @brief Sends a Channel Selection Request with Channel Preference TLVs.
 *
 * Initiates setting Channel Selection parameters with the Creation TLV at the
 * current Radio and the given parameters
 *
 * Example of usage from the shell:
 * @code{.sh}
 * $ args="Class={1={OpClass=81,Channel={1={Channel=1,Preference=1}}}}'
 * $ ba-cli "Device.WiFi.DataElements.Network.Device.1.Radio.1.ChannelSelectionRequest($args)"
 * @endcode
 *
 * @param object The DM object pointing at the Radio for which the Channel Selection Request will be sent on
 * @param func The function context in case deferred execution is needed
 * @param args The arguments passed from the caller, expected to contain a
 *     "Class" htable. Output arguments `Status` and `X_PRPLWARE-COM_Reason` will
 *     be added into this object.
 * @param ret Not used.
 * @return amxd_status_t amxd_status_ok always, errors are signalled through
 *     `Status` output argument.
 */
amxd_status_t channel_selection_request(amxd_object_t *object, amxd_function_t *func,
                                        amxc_var_t *args, amxc_var_t *ret)
{
    std::stringstream reason;

    auto controller_ctx = g_database->get_controller_ctx();
    if (!controller_ctx) {
        reason << "Failed to get controller context.";
        chan_sel_req_unknown_error(args, reason.str().c_str());
        return amxd_status_ok;
    }

    sMacAddr al_mac;
    if (!chan_sel_req_get_al_mac(object, &al_mac, &reason)) {
        chan_sel_req_unknown_error(args, reason.str().c_str());
        return amxd_status_ok;
    }

    sMacAddr radio_mac;
    if (!chan_sel_req_get_radio_mac(object, &radio_mac, &reason)) {
        chan_sel_req_unknown_error(args, reason.str().c_str());
        return amxd_status_ok;
    }

    Controller::ChanPref radio_preferences;
    const amxc_var_t *classes = GET_ARG(args, "Class");
    if (!chan_sel_req_parse_classes(classes, &radio_preferences, &reason)) {
        chan_sel_req_invalid_param(args, reason.str().c_str());
        return amxd_status_ok;
    }

    const Controller::ChanPrefs preferences = {
        {radio_mac, radio_preferences},
    };

    if (!controller_ctx->trigger_channel_selection_request(al_mac, preferences)) {
        chan_sel_req_unknown_error(args, "Controller failed to trigger channel selection request");
        return amxd_status_ok;
    }

    chan_sel_req_success(args);
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

namespace {

/**
 * @brief Get a named list argument, or the argument itself if it is already a list.
 */
const amxc_var_t *get_list_argument(const amxc_var_t *args, const char *name)
{
    if (!args) {
        return nullptr;
    }

    auto list = GET_ARG(args, name);
    if (list && amxc_var_type_of(list) == AMXC_VAR_ID_LIST) {
        return list;
    }

    if (amxc_var_type_of(args) == AMXC_VAR_ID_LIST) {
        return args;
    }

    return nullptr;
}

/**
 * @brief Read a string field from an Ambiorix variant entry.
 */
bool read_string_value(const amxc_var_t *entry, const char *name, std::string &value)
{
    auto arg = GET_ARG(entry, name);
    if (!arg) {
        return false;
    }

    auto cstr = amxc_var_constcast(cstring_t, arg);
    if (!cstr) {
        return false;
    }

    value = cstr;
    return true;
}

/**
 * @brief Parse a list argument into STA MAC addresses.
 */
bool parse_mac_list_argument(const amxc_var_t *args, const char *name, std::vector<sMacAddr> &list)
{
    auto list_arg = get_list_argument(args, name);
    if (!list_arg) {
        return false;
    }

    list.clear();
    amxc_var_for_each(entry, list_arg)
    {
        auto sta_mac_cstr = amxc_var_constcast(cstring_t, entry);
        if (!sta_mac_cstr) {
            return false;
        }

        if (!network_utils::is_valid_mac(sta_mac_cstr)) {
            return false;
        }

        list.push_back(tlvf::mac_from_string(sta_mac_cstr));
    }

    return true;
}

/**
 * @brief Check whether a string is non-empty even-length hexadecimal.
 */
bool is_strict_hex_string(const std::string &value)
{
    if (value.empty() || (value.size() % 2) != 0) {
        return false;
    }

    return std::all_of(value.begin(), value.end(),
                       [](unsigned char character) { return std::isxdigit(character) != 0; });
}

/**
 * @brief Descriptor entry validated before committing it to the controller DB.
 */
struct sPendingQmDescriptor {
    sMacAddr client_mac = beerocks::net::network_utils::ZERO_MAC;
    std::string descriptor_element;
};

} // namespace

/**
 * @brief Configure TR-181 SetQMDescriptors inputs for the current BSS.
 */
amxd_status_t set_qm_descriptors(amxd_object_t *bss_instance, amxd_function_t *func,
                                 amxc_var_t *args, amxc_var_t *ret)
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

    amxc_var_t value;
    amxc_var_init(&value);

    amxd_object_get_param(bss_instance, "BSSID", &value);
    const std::string bssid_str = amxc_var_constcast(cstring_t, &value);
    amxc_var_clean(&value);

    if (bssid_str.empty() || !network_utils::is_valid_mac(bssid_str)) {
        LOG(ERROR) << "Invalid BSSID on BSS object";
        return amxd_status_parameter_not_found;
    }

    const auto bssid = tlvf::mac_from_string(bssid_str);
    auto radio       = g_database->get_radio_by_bssid(bssid);
    if (!radio) {
        LOG(ERROR) << "Failed to get radio for BSSID " << bssid_str;
        return amxd_status_parameter_not_found;
    }

    auto descriptors = get_list_argument(args, "QMDescriptor");
    if (!descriptors) {
        LOG(ERROR) << "QMDescriptor list is missing";
        return amxd_status_invalid_value;
    }

    std::vector<sPendingQmDescriptor> pending_descriptors;
    std::unordered_set<std::string> client_macs;

    amxc_var_for_each(entry, descriptors)
    {
        std::string entry_bssid_str;
        std::string client_mac_str;
        std::string descriptor_element;

        if (!read_string_value(entry, "ClientMAC", client_mac_str) ||
            !read_string_value(entry, "DescriptorElement", descriptor_element)) {
            LOG(ERROR) << "QMDescriptor entry is missing required fields";
            return amxd_status_invalid_value;
        }

        if (!network_utils::is_valid_mac(client_mac_str)) {
            LOG(ERROR) << "Invalid ClientMAC " << client_mac_str;
            return amxd_status_invalid_value;
        }

        const auto client_mac            = tlvf::mac_from_string(client_mac_str);
        const auto normalized_client_mac = tlvf::mac_to_string(client_mac);
        if (!client_macs.insert(normalized_client_mac).second) {
            LOG(ERROR) << "Duplicate QMDescriptor entry for client " << normalized_client_mac;
            return amxd_status_invalid_value;
        }

        if (read_string_value(entry, "BSSID", entry_bssid_str) && !entry_bssid_str.empty()) {
            if (!network_utils::is_valid_mac(entry_bssid_str) ||
                tlvf::mac_from_string(entry_bssid_str) != bssid) {
                LOG(ERROR) << "QMDescriptor entry BSSID does not match target BSS";
                return amxd_status_invalid_value;
            }
        }

        if (!is_strict_hex_string(descriptor_element)) {
            LOG(ERROR) << "Invalid DescriptorElement for client " << normalized_client_mac;
            return amxd_status_invalid_value;
        }

        const auto descriptor_bytes =
            beerocks::string_utils::hex_to_bytes<std::vector<uint8_t>>(descriptor_element);
        if (!qos_management::is_valid_qos_management_descriptor_element(descriptor_bytes)) {
            LOG(ERROR) << "Invalid DescriptorElement for client " << normalized_client_mac;
            return amxd_status_invalid_value;
        }

        pending_descriptors.push_back({client_mac, std::move(descriptor_element)});
    }

    if (pending_descriptors.empty()) {
        LOG(ERROR) << "QMDescriptor list is empty";
        return amxd_status_invalid_value;
    }

    for (const auto &descriptor : pending_descriptors) {
        if (!g_database->set_controller_qm_descriptor(bssid, descriptor.client_mac,
                                                      descriptor.descriptor_element)) {
            LOG(ERROR) << "Failed to configure controller QMDescriptor for client "
                       << descriptor.client_mac;
            return amxd_status_unknown_error;
        }
    }

    controller->trigger_prioritization_config();
    return amxd_status_ok;
}

/**
 * @brief Configure the TR-181 SetQoSManagement inputs for the current BSS.
 *
 * Optional parameters defined by TR-181 for
 * Device.WiFi.DataElements.Network.Device.Radio.BSS.SetQoSManagement,
 * QosMapEnable, DSCPPolicyEnable and SCSTrafficDescriptionEnable, are
 * currently rejected. The northbound surface keeps the full TR-181 shape,
 * while QoS management currently exposes only MSCSEnable and SCSEnable.
 */
amxd_status_t set_qos_management(amxd_object_t *bss_instance, amxd_function_t *func,
                                 amxc_var_t *args, amxc_var_t *ret)
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

    amxc_var_t value;
    amxc_var_init(&value);

    amxd_object_get_param(bss_instance, "BSSID", &value);
    const std::string bssid_str = amxc_var_constcast(cstring_t, &value);
    amxc_var_clean(&value);

    if (bssid_str.empty() || !network_utils::is_valid_mac(bssid_str)) {
        LOG(ERROR) << "Invalid BSSID on BSS object";
        return amxd_status_parameter_not_found;
    }

    auto radio = g_database->get_radio_by_bssid(tlvf::mac_from_string(bssid_str));
    if (!radio) {
        LOG(ERROR) << "Failed to get radio for BSSID " << bssid_str;
        return amxd_status_parameter_not_found;
    }

    son::db::sQosManagementSettings settings = {};
    g_database->get_bss_qos_management_settings(tlvf::mac_from_string(bssid_str), settings);

    auto settings_object = amxd_object_get_child(bss_instance, "SetQoSManagementInput");
    const auto qos_map_enable =
        GET_ARG(args, "QosMapEnable")
            ? GET_BOOL(args, "QosMapEnable")
            : (settings_object ? get_param_bool(settings_object, "QosMapEnable") : false);
    settings.mscs_enable = GET_ARG(args, "MSCSEnable")
                               ? GET_BOOL(args, "MSCSEnable")
                               : (settings_object ? get_param_bool(settings_object, "MSCSEnable")
                                                  : settings.mscs_enable);
    settings.scs_enable = GET_ARG(args, "SCSEnable")
                              ? GET_BOOL(args, "SCSEnable")
                              : (settings_object ? get_param_bool(settings_object, "SCSEnable")
                                                 : settings.scs_enable);
    const auto dscp_policy_enable =
        GET_ARG(args, "DSCPPolicyEnable")
            ? GET_BOOL(args, "DSCPPolicyEnable")
            : (settings_object ? get_param_bool(settings_object, "DSCPPolicyEnable") : false);
    const auto scs_traffic_description_enable =
        GET_ARG(args, "SCSTrafficDescriptionEnable")
            ? GET_BOOL(args, "SCSTrafficDescriptionEnable")
            : (settings_object ? get_param_bool(settings_object, "SCSTrafficDescriptionEnable")
                               : false);
    settings.valid = true;

    if (qos_map_enable || dscp_policy_enable || scs_traffic_description_enable) {
        LOG(ERROR) << "QoS management does not expose QosMapEnable, "
                   << "DSCPPolicyEnable and SCSTrafficDescriptionEnable";
        return amxd_status_invalid_value;
    }

    if (!g_database->set_bss_qos_management_settings(tlvf::mac_from_string(bssid_str), settings)) {
        LOG(ERROR) << "Failed to configure BSS QoS management settings for " << bssid_str;
        return amxd_status_unknown_error;
    }

    controller->trigger_prioritization_config();
    return amxd_status_ok;
}

/**
 * @brief Trigger service prioritization and QoS management descriptor propagation.
 */
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
 * @brief Configure the controller MSCS disallowed STA list.
 */
amxd_status_t set_mscs_disallowed(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
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

    std::vector<sMacAddr> sta_list;
    if (!parse_mac_list_argument(args, "MSCSDisallowedStaList", sta_list)) {
        LOG(ERROR) << "Failed to parse MSCSDisallowedStaList";
        return amxd_status_invalid_value;
    }

    if (!g_database->set_mscs_disallowed_sta_list(sta_list)) {
        LOG(ERROR) << "Failed to set MSCS disallowed STA list";
        return amxd_status_unknown_error;
    }

    controller->trigger_prioritization_config();
    return amxd_status_ok;
}

/**
 * @brief Configure the controller SCS disallowed STA list.
 */
amxd_status_t set_scs_disallowed(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
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

    std::vector<sMacAddr> sta_list;
    if (!parse_mac_list_argument(args, "SCSDisallowedStaList", sta_list)) {
        LOG(ERROR) << "Failed to parse SCSDisallowedStaList";
        return amxd_status_invalid_value;
    }

    if (!g_database->set_scs_disallowed_sta_list(sta_list)) {
        LOG(ERROR) << "Failed to set SCS disallowed STA list";
        return amxd_status_unknown_error;
    }

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
    }
    std::string radio_mac(str);
    if (radio_mac.empty()) {
        LOG(ERROR) << "radio_mac is empty";
        amxc_var_clean(&value);
        return amxd_status_parameter_not_found;
    }

    // Prefer optional agent_mac argument when provided; otherwise derive from Device parent.
    // Hierarchy: Radio instance -> Radio template -> Device instance.
    std::string agent_mac;
    const char *agent_mac_arg = GET_CHAR(args, "agent_mac");
    if (agent_mac_arg && *agent_mac_arg) {
        if (!network_utils::is_valid_mac(agent_mac_arg)) {
            LOG(ERROR) << "Invalid value for agent_mac provided";
            amxc_var_clean(&value);
            return amxd_status_invalid_arg;
        }
        agent_mac = agent_mac_arg;
    } else {
        amxd_object_t *device = amxd_object_get_parent(amxd_object_get_parent(object));
        amxd_object_get_param(device, "ID", &value);
        str = amxc_var_constcast(cstring_t, &value);
        if (str == nullptr) {
            LOG(ERROR) << "Failed fetching ID";
            amxc_var_clean(&value);
            return amxd_status_object_not_found;
        }
        agent_mac = str;
        if (agent_mac.empty()) {
            LOG(ERROR) << "agent_mac is empty";
            amxc_var_clean(&value);
            return amxd_status_parameter_not_found;
        }
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
    }
    std::string radio_mac(str);
    if (radio_mac.empty()) {
        LOG(ERROR) << "radio_mac is empty";
        amxc_var_clean(&value);
        return amxd_status_parameter_not_found;
    }

    amxd_object_t *device = amxd_object_get_parent(amxd_object_get_parent(object));
    amxd_object_get_param(device, "ID", &value);
    str = amxc_var_constcast(cstring_t, &value);
    if (str == nullptr) {
        LOG(ERROR) << "Failed fetching ID";
        amxc_var_clean(&value);
        return amxd_status_object_not_found;
    }

    std::string agent_mac(str);
    amxc_var_clean(&value);
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

// Events

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
    nbapi_config.higher_layer_request_interval_seconds = std::chrono::seconds{
        amxd_object_get_int32_t(configuration, "HigherLayerRequestIntervalSec", nullptr)};

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
 * @brief Event handler for controller traffic separation configuration changes.
 *
 * TS can be enabled after controller startup in controller+agent mode, so renew
 * autoconfig to push the updated TS TLVs to the local agent as well.
 */
static void event_traffic_separation_changed(const char *const sig_name,
                                             const amxc_var_t *const data, void *const priv)
{
    if (!send_ap_config_renew()) {
        LOG(ERROR) << "Failed to renew AP config after traffic separation change";
    }
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

/**
 * @brief Event handler for DataElements Network.Device instance changes.
 *
 * It's invoked when a Device.WiFi.DataElements.Network.Device instance is
 * added, removed, or has its ID changed. It updates the
 * AssocWiFiNetworkDeviceRef field of the corresponding IEEE1905.Network.AL.{i}
 * entry in the data model.
 *
 * @param sig_name name of the Ambiorix signal that triggered this callback
 *                 (e.g. "dm:instance-added", "dm:instance-removed", "dm:object-changed")
 * @param data signal data carrying the affected object parameters and keys
 * @param priv private data (unused)
 */
static void event_ieee1905_dataelements_network_device_changed(const char *const sig_name,
                                                               const amxc_var_t *const data,
                                                               void *const priv)
{
    if (!g_database) {
        LOG(WARNING) << "Database is not initialized yet";
        return;
    }

    if (!g_database->ieee1905_network || !data) {
        return;
    }

    auto ambiorix = g_database->get_ambiorix_obj();
    if (!ambiorix) {
        LOG(ERROR) << "Ambiorix object is not available";
        return;
    }

    auto update_assoc_ref_for_al = [&](const char *id) -> bool {
        if (!id || !*id) {
            return true;
        }

        auto id_s   = std::string(id);
        auto al_mac = tlvf::mac_from_string(id_s);
        auto al_it  = g_database->ieee1905_network->al.find(al_mac);
        if (al_it == g_database->ieee1905_network->al.end() || !al_it->second.dm_path) {
            return true;
        }

        auto device_index = ambiorix->get_instance_index(
            DATAELEMENTS_ROOT_DM ".Network.Device.[ID == '%s'].", id_s);

        const auto &assoc_wifi_network_device_ref =
            device_index ? "Device.WiFi.DataElements.Network.Device." + std::to_string(device_index)
                         : std::string{};

        return al_it->second.dm_path.set("AssocWiFiNetworkDeviceRef",
                                         assoc_wifi_network_device_ref);
    };

    const std::string signal     = sig_name ? sig_name : "";
    const bool is_object_changed = (signal == "dm:object-changed");
    const bool is_instance_event =
        (signal == "dm:instance-added" || signal == "dm:instance-removed");

    bool ok = true;

    if (is_object_changed) {
        const auto *id_from = GETP_CHAR(data, "parameters.ID.from");
        const auto *id_to   = GETP_CHAR(data, "parameters.ID.to");

        ok &= update_assoc_ref_for_al(id_from);
        ok &= update_assoc_ref_for_al(id_to);
    }

    if (is_instance_event) {
        const auto *id     = GETP_CHAR(data, "parameters.ID");
        const auto *key_id = GETP_CHAR(data, "keys.ID");

        ok &= update_assoc_ref_for_al(id);
        ok &= update_assoc_ref_for_al(key_id);
    }

    if (!ok) {
        LOG(ERROR) << "Failed to update AssocWiFiNetworkDeviceRef on DataElements event " << signal;
    }
}

/**
 * @brief Event handler for IEEE1905 Network.Enable change.
 *
 * Invoked when the value of IEEE1905_ROOT_DM.Network.Enable changes.
 * It reads the new Enable value and forwards it to the controller via
 * Controller::handle_ieee1905_network_enable_changed().
 *
 * @param sig_name name of the Ambiorix signal that triggered this callback
 * @param data signal data used to retrieve the affected data model object
 * @param priv private data (unused)
 */
static void event_ieee1905_network_enable_changed(const char *const sig_name,
                                                  const amxc_var_t *const data, void *const priv)
{
    if (!g_database) {
        LOG(WARNING) << "Database is not initialized yet";
        return;
    }

    auto *network_obj = amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    if (!network_obj) {
        LOG(WARNING) << "Failed to get object " << IEEE1905_ROOT_DM << ".Network.";
        return;
    }

    amxd_status_t status;
    const bool enabled = amxd_object_get_bool(network_obj, "Enable", &status);
    if (status != amxd_status_ok) {
        LOG(ERROR) << "Failed to get " << IEEE1905_ROOT_DM << ".Network.Enable";
        return;
    }

    auto controller_ctx = g_database->get_controller_ctx();
    if (!controller_ctx) {
        LOG(WARNING) << "Failed to get controller context.";
        return;
    }

    if (!controller_ctx->handle_ieee1905_network_enable_changed(enabled)) {
        LOG(WARNING) << "Failed to handle " << IEEE1905_ROOT_DM << ".Network.Enable change.";
    }
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
        {"event_traffic_separation_changed", event_traffic_separation_changed},
        {"event_ieee1905_dataelements_network_device_changed",
         event_ieee1905_dataelements_network_device_changed},
        {"event_ieee1905_network_enable_changed", event_ieee1905_network_enable_changed},
        {"event_network_group_changed", event_network_group_changed},
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
        {"channel_selection_request",
         DATAELEMENTS_ROOT_DM ".Network.Device.Radio.ChannelSelectionRequest",
         channel_selection_request},
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
        {"set_qm_descriptors", DATAELEMENTS_ROOT_DM ".Network.Device.Radio.BSS.SetQMDescriptors",
         set_qm_descriptors},
        {"set_qos_management", DATAELEMENTS_ROOT_DM ".Network.Device.Radio.BSS.SetQoSManagement",
         set_qos_management},
        {"trigger_prioritization", DATAELEMENTS_ROOT_DM ".Network.SetServicePrioritization",
         trigger_prioritization},
        {"set_mscs_disallowed", DATAELEMENTS_ROOT_DM ".Network.SetMSCSDisallowed",
         set_mscs_disallowed},
        {"set_scs_disallowed", DATAELEMENTS_ROOT_DM ".Network.SetSCSDisallowed",
         set_scs_disallowed},
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
