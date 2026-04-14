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
#include <algorithm>
#include <cctype>

using namespace beerocks;
using namespace net;
using namespace son;
namespace prplmesh {
namespace controller {
namespace actions {

// Actions

son::db *g_database = nullptr;

static amxd_status_t template_commit(amxd_object_t *bss_template_obj, uint8_t &bss_index_seq);

static void template_rebuild_staged_configuration(amxd_object_t *templates_root);
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

static uint32_t get_param_uint32(amxd_object_t *object, const char *param_name)
{
    amxc_var_t param;
    uint32_t param_val = 0;

    amxc_var_init(&param);
    if (amxd_object_get_param(object, param_name, &param) == amxd_status_ok) {
        param_val = amxc_var_dyncast(uint32_t, &param);
    } else {
        LOG(ERROR) << "Failed to read uint32 parameter \"" << param_name << "\"";
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
 * @brief Parse comma-separated topology flags string
 */
static std::vector<std::string> parse_topology_flags(const std::string &topology_flag_str)
{
    std::vector<std::string> flags;
    if (topology_flag_str.empty()) {
        return flags;
    }

    std::istringstream iss(topology_flag_str);
    std::string flag;
    while (std::getline(iss, flag, ',')) {
        // Trim whitespace
        flag.erase(0, flag.find_first_not_of(" \t"));
        flag.erase(flag.find_last_not_of(" \t") + 1);
        if (!flag.empty()) {
            flags.push_back(flag);
        }
    }
    return flags;
}

/**
 * @brief Notify connected agents to refresh autoconfiguration (same path as AccessPointCommit).
 */
static void template_send_ap_config_renew_message()
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return;
    }

    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];
    ieee1905_1::CmduMessageTx cmdu_tx(m_tx_buffer, sizeof(m_tx_buffer));

    auto connected_agents = g_database->get_all_connected_agents();
    if (connected_agents.empty()) {
        LOG(DEBUG) << "No connected agents, skip AP_CONFIGURATION_RENEW";
        return;
    }

    if (!son_actions::send_ap_config_renew_msg(cmdu_tx, *g_database)) {
        LOG(ERROR) << "Failed to send AP_CONFIGURATION_RENEW_MESSAGE";
    }
}

/**
 * @brief Select enabled SecurityTemplate with highest Priority for a SecurityGroup.
 *
 * Uses LinkedSecurityTemplateID (comma-separated SecurityTemplateID values) and/or
 * SecurityTemplateReferences (comma-separated object paths).
 */
static amxd_object_t *template_resolve_security_template(amxd_object_t *templates_root,
                                                          amxd_object_t *security_group_obj)
{
    amxd_object_t *security_template_table = amxd_object_get_child(templates_root, "SecurityTemplate");
    if (!security_template_table) {
        LOG(WARNING) << "SecurityTemplate table not found";
        return nullptr;
    }

    amxd_object_t *best_inst = nullptr;
    uint32_t best_priority   = 0;

    auto consider = [&](amxd_object_t *inst) {
        if (!inst || !get_param_bool(inst, "Enable")) {
            return;
        }
        uint32_t p = get_param_uint32(inst, "Priority");
        if (!best_inst || p > best_priority) {
            best_inst     = inst;
            best_priority = p;
        }
    };

    std::string linked = get_param_string(security_group_obj, "LinkedSecurityTemplateID");
    for (const std::string &tid : parse_topology_flags(linked)) {
        if (tid.empty()) {
            continue;
        }
        amxd_object_for_each(instance, it, security_template_table)
        {
            amxd_object_t *inst = amxc_llist_it_get_data(it, amxd_object_t, it);
            if (get_param_string(inst, "SecurityTemplateID") == tid) {
                consider(inst);
                break;
            }
        }
    }

    std::string refs = get_param_string(security_group_obj, "SecurityTemplateReferences");
    for (const std::string &path : parse_topology_flags(refs)) {
        if (path.empty()) {
            continue;
        }
        amxd_object_t *inst =
            amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s", path.c_str());
        consider(inst);
    }

    return best_inst;
}

/**
 * @brief Fill bss_info.operating_class from RadioTemplate BandFlag / OpClassFlag.
 *
 * If both BandFlag and OpClassFlag are set, BandFlag wins (with warning).
 * BandFlag: tokens "2.4", "5", "6" only; all tokens must describe one band.
 * OpClassFlag: decimal op classes; all must map to the same 2.4/5/6 GHz band.
 */
static bool template_load_radio_operating_classes(amxd_object_t *radio_inst,
                                                  son::wireless_utils::sBssInfoConf &bss_info)
{
    bss_info.operating_class.clear();

    auto freq_from_band_token = [](const std::string &band) -> beerocks::eFreqType {
        if (band == "2.4") {
            return beerocks::FREQ_24G;
        }
        if (band == "5") {
            return beerocks::FREQ_5G;
        }
        if (band == "6") {
            return beerocks::FREQ_6G;
        }
        return beerocks::FREQ_UNKNOWN;
    };

    auto freq_from_op_class = [](uint8_t oc) -> beerocks::eFreqType {
        static const beerocks::eFreqType kBands[] = {beerocks::FREQ_24G, beerocks::FREQ_5G,
                                                     beerocks::FREQ_6G};
        for (auto ft : kBands) {
            const auto v = son::wireless_utils::get_operating_classes_of_freq_type(ft);
            if (std::find(v.begin(), v.end(), oc) != v.end()) {
                return ft;
            }
        }
        return beerocks::FREQ_UNKNOWN;
    };

    std::string band_flag     = get_param_string(radio_inst, "BandFlag");
    std::string op_class_flag = get_param_string(radio_inst, "OpClassFlag");

    if (!band_flag.empty() && !op_class_flag.empty()) {
        LOG(WARNING) << "RadioTemplate has both BandFlag and OpClassFlag; using BandFlag only";
    }

    if (!band_flag.empty()) {
        beerocks::eFreqType band_choice = beerocks::FREQ_UNKNOWN;
        std::istringstream iss(band_flag);
        std::string token;
        while (std::getline(iss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            if (token.empty()) {
                continue;
            }
            beerocks::eFreqType ft = freq_from_band_token(token);
            if (ft == beerocks::FREQ_UNKNOWN) {
                LOG(WARNING) << "Unknown BandFlag token \"" << token << "\"";
                continue;
            }
            if (band_choice == beerocks::FREQ_UNKNOWN) {
                band_choice = ft;
            } else if (band_choice != ft) {
                LOG(WARNING) << "BandFlag lists multiple frequency bands; invalid RadioTemplate";
                bss_info.operating_class.clear();
                return false;
            }
        }
        if (band_choice == beerocks::FREQ_UNKNOWN) {
            return false;
        }
        if (band_choice == beerocks::FREQ_24G) {
            bss_info.operating_class.splice(bss_info.operating_class.end(),
                                            son::wireless_utils::string_to_wsc_oper_class("24g"));
        } else if (band_choice == beerocks::FREQ_5G) {
            bss_info.operating_class.splice(bss_info.operating_class.end(),
                                            son::wireless_utils::string_to_wsc_oper_class("5gh"));
        } else {
            bss_info.operating_class.splice(bss_info.operating_class.end(),
                                            son::wireless_utils::string_to_wsc_oper_class("6g"));
        }
        return true;
    }

    if (op_class_flag.empty()) {
        return false;
    }

    beerocks::eFreqType op_band = beerocks::FREQ_UNKNOWN;
    std::istringstream iss(op_class_flag);
    std::string op_str;
    while (std::getline(iss, op_str, ',')) {
        op_str.erase(0, op_str.find_first_not_of(" \t"));
        op_str.erase(op_str.find_last_not_of(" \t") + 1);
        if (op_str.empty()) {
            continue;
        }
        char *endptr = nullptr;
        long v       = strtol(op_str.c_str(), &endptr, 10);
        if (endptr == op_str.c_str() || *endptr != '\0' || v < 0 || v > 255) {
            LOG(WARNING) << "Invalid OpClassFlag token \"" << op_str << "\"";
            continue;
        }
        uint8_t oc             = static_cast<uint8_t>(v);
        beerocks::eFreqType ft = freq_from_op_class(oc);
        if (ft == beerocks::FREQ_UNKNOWN) {
            LOG(WARNING) << "Operating class " << int(oc) << " not mapped to 2.4/5/6 GHz";
            bss_info.operating_class.clear();
            return false;
        }
        if (op_band == beerocks::FREQ_UNKNOWN) {
            op_band = ft;
        } else if (op_band != ft) {
            LOG(WARNING) << "OpClassFlag mixes operating classes from different bands";
            bss_info.operating_class.clear();
            return false;
        }
        bss_info.operating_class.push_back(oc);
    }

    return !bss_info.operating_class.empty();
}

/**
 * @brief Radio-first gate: true if this PHY radio’s band supports at least one template op class.
 */
static bool template_radio_matches_operating_classes(const Agent::sRadio &radio,
                                                     const std::list<uint8_t> &tpl_op_classes)
{
    if (radio.get_band() == beerocks::FREQ_UNKNOWN || tpl_op_classes.empty()) {
        return false;
    }
    const auto radio_ocs =
        son::wireless_utils::get_operating_classes_of_freq_type(radio.get_band());
    for (uint8_t oc : tpl_op_classes) {
        if (std::find(radio_ocs.begin(), radio_ocs.end(), oc) != radio_ocs.end()) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Resolve a reference path and update the linked ID field
 *
 * @param reference_path Full path like "Device.WiFi.Templates.RadioTemplate.1"
 * @param template_id_param_name Parameter name to read (e.g., "RadioTemplateID", "SSCTemplateID")
 * @param target_object Object where to set the linked ID
 * @param target_param_name Parameter name to set (e.g., "LinkedRadioTemplateID", "LinkedSSCTemplateID", "PrimarySSCTemplateID")
 * @return true on success, false otherwise
 */
static bool update_linked_template_id(const std::string &reference_path,
                                      const std::string &template_id_param_name,
                                      amxd_object_t *target_object,
                                      const std::string &target_param_name)
{
    if (reference_path.empty()) {
        // Clear the linked ID if reference is empty
        amxd_trans_t transaction;
        amxd_trans_init(&transaction);
        amxd_trans_set_attr(&transaction, amxd_tattr_change_ro, true);
        amxd_trans_select_object(&transaction, target_object);
        amxd_trans_set_value(cstring_t, &transaction, target_param_name.c_str(), "");
        amxd_status_t status = amxd_trans_apply(&transaction, beerocks::nbapi::Amxrt::getDatamodel());
        amxd_trans_clean(&transaction);
        return (status == amxd_status_ok);
    }

    // Find the referenced object
    amxd_object_t *referenced_obj = amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(),
                                                    "%s", reference_path.c_str());
    if (!referenced_obj) {
        LOG(WARNING) << "Failed to find referenced object: " << reference_path;
        return false;
    }

    // Read the TemplateID from the referenced object
    std::string template_id = get_param_string(referenced_obj, template_id_param_name.c_str());

    if (template_id.empty()) {
        LOG(WARNING) << "TemplateID is empty in referenced object: " << reference_path;
        return false;
    }

    // Update the linked ID using transaction with read-only attribute
    amxd_trans_t transaction;
    amxd_trans_init(&transaction);
    amxd_trans_set_attr(&transaction, amxd_tattr_change_ro, true);
    amxd_trans_select_object(&transaction, target_object);
    amxd_trans_set_value(cstring_t, &transaction, target_param_name.c_str(), template_id.c_str());
    amxd_status_t status = amxd_trans_apply(&transaction, beerocks::nbapi::Amxrt::getDatamodel());
    amxd_trans_clean(&transaction);

    if (status == amxd_status_ok) {
        LOG(DEBUG) << "Updated " << target_param_name << " to \"" << template_id
                   << "\" from reference " << reference_path;
        return true;
    } else {
        LOG(ERROR) << "Failed to update " << target_param_name << ", status: "
                   << amxd_status_string(status);
        return false;
    }
}

/**
 * @brief Event handler for BSSTemplate.RadioTemplateReference change.
 * Automatically updates LinkedRadioTemplateID when RadioTemplateReference changes.
 */
static void event_bss_radio_template_reference_changed(const char *const sig_name,
                                                        const amxc_var_t *const data,
                                                        void *const priv)
{
    amxd_object_t *bss_template_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!bss_template_obj) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.BSSTemplate";
        return;
    }

    std::string radio_template_ref = get_param_string(bss_template_obj, "RadioTemplateReference");

    LOG(DEBUG) << "BSSTemplate RadioTemplateReference changed to: \"" << radio_template_ref << "\"";

    update_linked_template_id(radio_template_ref, "RadioTemplateID", bss_template_obj,
                             "LinkedRadioTemplateID");
}

/**
 * @brief Event handler for BSSTemplate.SSCTemplateReference change.
 * Automatically updates LinkedSSCTemplateID when SSCTemplateReference changes.
 */
static void event_bss_ssc_template_reference_changed(const char *const sig_name,
                                                     const amxc_var_t *const data,
                                                     void *const priv)
{
    amxd_object_t *bss_template_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!bss_template_obj) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.BSSTemplate";
        return;
    }

    std::string ssc_template_ref = get_param_string(bss_template_obj, "SSCTemplateReference");

    LOG(DEBUG) << "BSSTemplate SSCTemplateReference changed to: \"" << ssc_template_ref << "\"";

    update_linked_template_id(ssc_template_ref, "SSCTemplateID", bss_template_obj,
                             "LinkedSSCTemplateID");
}

/**
 * @brief Event handler for Network.PrimarySSCTemplateReference change.
 * Automatically updates PrimarySSCTemplateID when PrimarySSCTemplateReference changes.
 */
static void event_network_primary_ssc_reference_changed(const char *const sig_name,
                                                        const amxc_var_t *const data,
                                                        void *const priv)
{
    amxd_object_t *network_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!network_obj) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.Network";
        return;
    }

    std::string primary_ssc_ref = get_param_string(network_obj, "PrimarySSCTemplateReference");

    LOG(DEBUG) << "Network PrimarySSCTemplateReference changed to: \"" << primary_ssc_ref << "\"";

    update_linked_template_id(primary_ssc_ref, "SSCTemplateID", network_obj,
                             "PrimarySSCTemplateID");
}

/**
 * @brief Event handler for BSSTemplate.SecurityGroupReference change.
 * Updates LinkedSecurityGroupID when SecurityGroupReference changes.
 */
static void event_bss_security_group_reference_changed(const char *const sig_name,
                                                       const amxc_var_t *const data,
                                                       void *const priv)
{
    amxd_object_t *bss_template_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!bss_template_obj) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.BSSTemplate";
        return;
    }

    std::string security_group_ref = get_param_string(bss_template_obj, "SecurityGroupReference");

    LOG(DEBUG) << "BSSTemplate SecurityGroupReference changed to: \"" << security_group_ref << "\"";

    update_linked_template_id(security_group_ref, "SecurityGroupID", bss_template_obj,
                              "LinkedSecurityGroupID");
}

/**
 * @brief Event handler for SecurityGroup.SecurityTemplateReferences change.
 * Updates LinkedSecurityTemplateID (csv of SecurityTemplateIDs) from reference paths.
 */
static void event_security_group_template_references_changed(const char *const sig_name,
                                                             const amxc_var_t *const data,
                                                             void *const priv)
{
    amxd_object_t *security_group_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!security_group_obj) {
        LOG(WARNING) << "Failed to get object Device.WiFi.Templates.SecurityGroup";
        return;
    }

    std::string refs_str = get_param_string(security_group_obj, "SecurityTemplateReferences");
    LOG(DEBUG) << "SecurityGroup SecurityTemplateReferences changed to: \"" << refs_str << "\"";

    std::vector<std::string> paths = parse_topology_flags(refs_str);
    std::string linked_ids;
    for (size_t i = 0; i < paths.size(); i++) {
        amxd_object_t *ref_obj = amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(),
                                               "%s", paths[i].c_str());
        if (!ref_obj) {
            LOG(WARNING) << "SecurityTemplate reference not found: " << paths[i];
            continue;
        }
        std::string tid = get_param_string(ref_obj, "SecurityTemplateID");
        if (tid.empty())
            continue;
        if (!linked_ids.empty())
            linked_ids += ",";
        linked_ids += tid;
    }

    amxd_trans_t transaction;
    amxd_trans_init(&transaction);
    amxd_trans_set_attr(&transaction, amxd_tattr_change_ro, true);
    amxd_trans_select_object(&transaction, security_group_obj);
    amxd_trans_set_value(cstring_t, &transaction, "LinkedSecurityTemplateID", linked_ids.c_str());
    amxd_status_t status = amxd_trans_apply(&transaction, beerocks::nbapi::Amxrt::getDatamodel());
    amxd_trans_clean(&transaction);

    if (status == amxd_status_ok) {
        LOG(DEBUG) << "Updated LinkedSecurityTemplateID to \"" << linked_ids << "\"";
    } else {
        LOG(ERROR) << "Failed to update LinkedSecurityTemplateID, status: "
                   << amxd_status_string(status);
    }
}

static std::vector<sMacAddr> filter_target_agents(
    const std::vector<std::shared_ptr<Agent>> &connected_agents,
    const std::vector<std::string> &network_topology_flags,
    const std::vector<sMacAddr> &network_alids,
    const std::vector<std::string> &bss_topology_flags,
    const std::vector<sMacAddr> &bss_alids)
{
    std::vector<sMacAddr> target_agents;

    for (const auto &agent : connected_agents) {
        bool should_deploy = false;

        if (!network_alids.empty()) {
            bool network_match = false;
            for (const auto &network_alid : network_alids) {
                if (agent->al_mac == network_alid) {
                    network_match = true;
                    break;
                }
            }
            if (!network_match) {
                continue;
            }
        }

        if (!bss_alids.empty()) {
            bool alid_match = false;
            for (const auto &bss_alid : bss_alids) {
                if (agent->al_mac == bss_alid) {
                    alid_match = true;
                    break;
                }
            }
            if (!alid_match) {
                continue;
            }
            should_deploy = true;
        }

        const bool is_root     = agent->is_gateway;
        const bool is_repeater = !agent->is_gateway;
        const bool bh_wired =
            (agent->backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_ETHERNET ||
             agent->backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_BRIDGE ||
             agent->backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_GW_BRIDGE);
        const bool is_wired_repeater    = is_repeater && bh_wired;
        const bool is_wireless_repeater = is_repeater && !bh_wired;

        if (!should_deploy && !network_topology_flags.empty()) {
            for (const auto &flag : network_topology_flags) {
                if (flag == "Root" && is_root) {
                    should_deploy = true;
                    break;
                }
                if (flag == "Repeater" && is_repeater) {
                    should_deploy = true;
                    break;
                }
                if (flag == "Wired_Repeater" && is_wired_repeater) {
                    should_deploy = true;
                    break;
                }
                if (flag == "Wireless_Repeater" && is_wireless_repeater) {
                    should_deploy = true;
                    break;
                }
            }
        }

        if (!should_deploy && !bss_topology_flags.empty()) {
            for (const auto &flag : bss_topology_flags) {
                if (flag == "Root" && is_root) {
                    should_deploy = true;
                    break;
                }
                if (flag == "Repeater" && is_repeater) {
                    should_deploy = true;
                    break;
                }
                if (flag == "Wired_Repeater" && is_wired_repeater) {
                    should_deploy = true;
                    break;
                }
                if (flag == "Wireless_Repeater" && is_wireless_repeater) {
                    should_deploy = true;
                    break;
                }
            }
        }

        if (!should_deploy && network_topology_flags.empty() && bss_topology_flags.empty() &&
            network_alids.empty() && bss_alids.empty()) {
            should_deploy = true;
        }

        if (should_deploy) {
            target_agents.push_back(agent->al_mac);
            LOG(DEBUG) << "Template scope includes agent " << agent->al_mac;
        }
    }

    return target_agents;
}

/** BBF: 4-octet hex AKM suite selector without internal delimiters (e.g. 000FAC12). */
static const std::string OWE_AKM_SELECTOR = "000FAC12";

/**
 * @brief Normalize 4-octet AKM suite selector (strip non-hex, uppercase).
 */
static std::string normalize_akm_selector(const std::string &raw)
{
    std::string out;
    for (char c : raw) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return (out.size() == 8u) ? out : "";
}

/**
 * @brief Apply SecurityTemplate DM fields to bss_info (WSC auth / encr / additional_auth).
 *        Non-empty SecurityIEs is not implemented in this path (FEAT-8 / PPM-3450).
 */
static void apply_security_template_to_bss_info(amxd_object_t *security_template_obj,
                                                son::wireless_utils::sBssInfoConf &bss_info)
{
    std::string security_ies = get_param_string(security_template_obj, "SecurityIEs");
    if (!security_ies.empty()) {
        LOG(DEBUG) << "SecurityTemplate SecurityIEs non-empty; binary IE path not implemented";
        return;
    }

    std::string rsno_support_flag       = get_param_string(security_template_obj, "RSNOSupportFlag");
    std::string supported_akm_flag      = get_param_string(security_template_obj, "SupportedAKMSuiteFlag");
    std::string supported_selector_flag = get_param_string(security_template_obj, "SupportedAKMSuiteSelectorFlag");

    const bool rsno_support = (rsno_support_flag == "true");

    amxd_object_t *rsne_obj = amxd_object_get_child(security_template_obj, "RSNE");
    std::string rsne_akm;
    std::string rsne_akm_selector_str;
    if (rsne_obj) {
        rsne_akm               = get_param_string(rsne_obj, "AKMSuite");
        rsne_akm_selector_str = get_param_string(rsne_obj, "AKMSuiteSelector");
    }

    std::vector<std::string> akm_flags     = parse_topology_flags(supported_akm_flag);
    std::vector<std::string> rsne_akm_list = parse_topology_flags(rsne_akm);

    auto contains = [](const std::vector<std::string> &vec, const std::string &s) {
        return std::find(vec.begin(), vec.end(), s) != vec.end();
    };

    const bool use_suite_selector =
        contains(akm_flags, "SuiteSelector") || contains(rsne_akm_list, "SuiteSelector");

    if (use_suite_selector) {
        std::string selector_str =
            rsne_akm_selector_str.empty() ? supported_selector_flag : rsne_akm_selector_str;
        std::vector<std::string> selector_values = parse_topology_flags(selector_str);
        for (auto &s : selector_values) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
            s = normalize_akm_selector(s);
        }
        selector_values.erase(std::remove_if(selector_values.begin(), selector_values.end(),
                                             [](const std::string &x) { return x.empty(); }),
                              selector_values.end());

        for (const auto &sel : selector_values) {
            if (sel == OWE_AKM_SELECTOR) {
                bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_OPEN;
                bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
                bss_info.additional_auth     = son::wireless_utils::eAdditionalAuth::NONE;
                LOG(DEBUG) << "SecurityTemplate: OWE (AKM suite selector)";
                return;
            }
        }
        LOG(DEBUG) << "SecurityTemplate: SuiteSelector set but no handled selector";
    }

    if (contains(akm_flags, "sae") || contains(rsne_akm_list, "sae")) {
        const bool has_psk = contains(akm_flags, "psk") || contains(rsne_akm_list, "psk");
        if (has_psk && rsno_support) {
            bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_RSN;
            bss_info.encryption_type       = WSC::eWscEncr::WSC_ENCR_AES;
            bss_info.additional_auth =
                son::wireless_utils::eAdditionalAuth::WPA3_PERSONAL_COMPATIBILITY;
            LOG(DEBUG) << "SecurityTemplate: WPA3-Personal-Compatibility (RSNO)";
        } else if (has_psk) {
            bss_info.authentication_type = WSC::eWscAuth(WSC::eWscAuth::WSC_AUTH_WPA2PSK |
                                                          WSC::eWscAuth::WSC_AUTH_SAE);
            bss_info.encryption_type = WSC::eWscEncr::WSC_ENCR_AES;
            bss_info.additional_auth = son::wireless_utils::eAdditionalAuth::NONE;
            LOG(DEBUG) << "SecurityTemplate: WPA3-Personal-Transition (PSK+SAE, no RSNO)";
        } else {
            bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_SAE;
            bss_info.encryption_type       = WSC::eWscEncr::WSC_ENCR_AES;
            bss_info.additional_auth       = son::wireless_utils::eAdditionalAuth::NONE;
            LOG(DEBUG) << "SecurityTemplate: WPA3-Personal (SAE)";
        }
        return;
    }

    if (contains(akm_flags, "psk") || contains(rsne_akm_list, "psk")) {
        bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
        bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
        bss_info.additional_auth     = son::wireless_utils::eAdditionalAuth::NONE;
        LOG(DEBUG) << "SecurityTemplate: WPA2-Personal";
        return;
    }

    LOG(DEBUG) << "SecurityTemplate: no supported AKM combination";
}

/**
 * @brief Stage one enabled BSSTemplate into the controller DB for in-scope agents.
 *
 * Only active when use_dataelements_vap_configs is true (Data Elements / template VAP path).
 * @param bss_index_seq In/out: BSS index for this template row; incremented after any agent stages.
 */
static amxd_status_t template_commit(amxd_object_t *bss_template_obj, uint8_t &bss_index_seq)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return amxd_status_ok;
    }

    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(DEBUG) << "template_commit ignored (use_dataelements_vap_configs is false)";
        return amxd_status_ok;
    }

    if (!bss_template_obj) {
        LOG(ERROR) << "BSSTemplate object is null";
        return amxd_status_object_not_found;
    }

    const uint32_t bss_instance_index = amxd_object_get_index(bss_template_obj);

    if (!get_param_bool(bss_template_obj, "Enable")) {
        LOG(DEBUG) << "BSSTemplate[" << bss_instance_index << "] disabled, skip";
        return amxd_status_ok;
    }

    amxd_object_t *bss_template_table = amxd_object_get_parent(bss_template_obj);
    if (!bss_template_table) {
        LOG(ERROR) << "Failed to get BSSTemplate table";
        return amxd_status_object_not_found;
    }

    amxd_object_t *templates_root = amxd_object_get_parent(bss_template_table);
    if (!templates_root) {
        LOG(ERROR) << "Failed to get Templates root";
        return amxd_status_object_not_found;
    }

    amxd_object_t *network_obj = amxd_object_get_child(templates_root, "Network");
    if (!network_obj) {
        LOG(ERROR) << "Network object not found under Templates root";
        return amxd_status_object_not_found;
    }

    if (!get_param_bool(network_obj, "Enable")) {
        LOG(DEBUG) << "Templates.Network.Enable is false, skip";
        return amxd_status_ok;
    }

    std::string network_topology_flag_str = get_param_string(network_obj, "TopologyFlag");
    std::string network_ieee1905_alid_str  = get_param_string(network_obj, "IEEE1905ALID");
    std::string network_primary_ssc_ref   = get_param_string(network_obj, "PrimarySSCTemplateReference");
    std::vector<std::string> network_topology_flags = parse_topology_flags(network_topology_flag_str);

    std::vector<sMacAddr> network_alids;
    if (!network_ieee1905_alid_str.empty()) {
        std::istringstream iss(network_ieee1905_alid_str);
        std::string mac_str;
        while (std::getline(iss, mac_str, ',')) {
            mac_str.erase(0, mac_str.find_first_not_of(" \t"));
            mac_str.erase(mac_str.find_last_not_of(" \t") + 1);
            if (mac_str.empty()) {
                continue;
            }
            sMacAddr alid = tlvf::mac_from_string(mac_str);
            if (alid != beerocks::net::network_utils::ZERO_MAC) {
                network_alids.push_back(alid);
            } else {
                LOG(WARNING) << "Invalid MAC in Network IEEE1905ALID: " << mac_str;
            }
        }
    }

    std::string bss_ssid = get_param_string(bss_template_obj, "SSID");
    if (bss_ssid.empty()) {
        LOG(WARNING) << "BSSTemplate[" << bss_instance_index << "] SSID empty, skip";
        return amxd_status_ok;
    }

    std::string bss_key_passphrase     = get_param_string(bss_template_obj, "KeyPassphrase");
    bool bss_advertisement_enable      = get_param_bool(bss_template_obj, "AdvertisementEnable");
    std::string bss_topology_flag_str  = get_param_string(bss_template_obj, "TopologyFlag");
    std::string bss_ieee1905_alid_str   = get_param_string(bss_template_obj, "IEEE1905ALID");
    std::string bss_radio_template_ref = get_param_string(bss_template_obj, "RadioTemplateReference");
    std::string bss_ssc_template_ref   = get_param_string(bss_template_obj, "SSCTemplateReference");
    std::string bss_linked_ssc_id      = get_param_string(bss_template_obj, "LinkedSSCTemplateID");
    std::string bss_security_group_ref = get_param_string(bss_template_obj, "SecurityGroupReference");
    std::string bss_apmld_ref          = get_param_string(bss_template_obj, "APMLDTemplateReference");

    if (bss_radio_template_ref.empty() || bss_ssc_template_ref.empty() ||
        bss_security_group_ref.empty()) {
        LOG(WARNING) << "BSSTemplate[" << bss_instance_index
                     << "] missing Radio/SSC/SecurityGroup reference (non-deployable)";
        return amxd_status_ok;
    }

    if (!network_primary_ssc_ref.empty()) {
        amxd_object_t *pri_ssc =
            amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s",
                          network_primary_ssc_ref.c_str());
        if (pri_ssc) {
            const std::string primary_id = get_param_string(pri_ssc, "SSCTemplateID");
            if (!primary_id.empty() && primary_id != bss_linked_ssc_id) {
                LOG(WARNING) << "BSSTemplate SSC does not match Network.PrimarySSCTemplateReference";
                return amxd_status_ok;
            }
        }
    }

    std::vector<std::string> bss_topology_flags = parse_topology_flags(bss_topology_flag_str);

    std::vector<sMacAddr> bss_alids;
    if (!bss_ieee1905_alid_str.empty()) {
        std::istringstream iss(bss_ieee1905_alid_str);
        std::string mac_str;
        while (std::getline(iss, mac_str, ',')) {
            mac_str.erase(0, mac_str.find_first_not_of(" \t"));
            mac_str.erase(mac_str.find_last_not_of(" \t") + 1);
            if (mac_str.empty()) {
                continue;
            }
            sMacAddr alid = tlvf::mac_from_string(mac_str);
            if (alid != beerocks::net::network_utils::ZERO_MAC) {
                bss_alids.push_back(alid);
            } else {
                LOG(WARNING) << "Invalid MAC in BSSTemplate IEEE1905ALID: " << mac_str;
            }
        }
    }

    son::wireless_utils::sBssInfoConf bss_info;
    bss_info.ssid        = bss_ssid;
    bss_info.network_key = bss_key_passphrase;
    bss_info.hidden_ssid = bss_advertisement_enable ? WSC::eWscVendorExtHiddenSsid::DISABLED
                                                    : WSC::eWscVendorExtHiddenSsid::ENABLED;
    bss_info.vap_type =
        wireless_utils::string_to_vap_type(get_param_string(bss_template_obj, "X_PRPLWARE_VapType"));

    amxd_object_t *security_group_obj =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s",
                      bss_security_group_ref.c_str());
    if (!security_group_obj) {
        LOG(WARNING) << "SecurityGroup not found: " << bss_security_group_ref;
        return amxd_status_ok;
    }

    amxd_object_t *security_inst = template_resolve_security_template(templates_root, security_group_obj);
    if (!security_inst) {
        LOG(WARNING) << "No enabled SecurityTemplate for BSSTemplate[" << bss_instance_index << "]";
        return amxd_status_ok;
    }

    apply_security_template_to_bss_info(security_inst, bss_info);

    amxd_object_t *radio_inst =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s",
                      bss_radio_template_ref.c_str());
    if (!radio_inst || !get_param_bool(radio_inst, "Enable")) {
        LOG(WARNING) << "RadioTemplate missing or disabled: " << bss_radio_template_ref;
        return amxd_status_ok;
    }

    if (!template_load_radio_operating_classes(radio_inst, bss_info)) {
        LOG(WARNING) << "No valid operating classes from RadioTemplate for BSSTemplate["
                     << bss_instance_index << "]";
        return amxd_status_ok;
    }

    amxd_object_t *ssc_inst =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s", bss_ssc_template_ref.c_str());
    if (!ssc_inst || !get_param_bool(ssc_inst, "SSCEnable")) {
        LOG(WARNING) << "SSCTemplate missing or SSCEnable=false: " << bss_ssc_template_ref;
        return amxd_status_ok;
    }

    std::string haul_type = get_param_string(ssc_inst, "HaulType");
    bss_info.backhaul    = (haul_type.find("Backhaul") != std::string::npos);
    bss_info.fronthaul   = (haul_type.find("Fronthaul") != std::string::npos);
    if (!bss_info.backhaul && !bss_info.fronthaul) {
        LOG(WARNING) << "SSCTemplate HaulType has no Fronthaul/Backhaul for BSSTemplate["
                     << bss_instance_index << "]";
        return amxd_status_ok;
    }

    if (!bss_apmld_ref.empty()) {
        amxd_object_t *apmld_inst =
            amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s", bss_apmld_ref.c_str());
        if (apmld_inst && get_param_bool(apmld_inst, "MLOEnable")) {
            const std::string mld_key = get_param_string(apmld_inst, "APMLDTemplateID");
            if (!mld_key.empty()) {
                son::wireless_utils::sMldInfoConf mld_info;
                mld_info.ssid  = bss_ssid;
                mld_info.str   = get_param_bool(apmld_inst, "STREnable");
                mld_info.nstr  = get_param_bool(apmld_inst, "NSTREnable");
                mld_info.emlsr = get_param_bool(apmld_inst, "EMLSREnable");
                mld_info.emlmr = get_param_bool(apmld_inst, "EMLMREnable");
                g_database->add_mld_info_configuration(mld_info, mld_key);
                bss_info.mld_id = mld_key;
                LOG(DEBUG) << "BSSTemplate[" << bss_instance_index << "] APMLD " << mld_key;
            }
        }
    }

    if (bss_info.authentication_type != WSC::eWscAuth::WSC_AUTH_OPEN &&
        bss_info.network_key.empty()) {
        LOG(WARNING) << "BSSTemplate " << bss_ssid << " missing KeyPassphrase for selected security";
    }

    auto connected_agents = g_database->get_all_connected_agents();
    if (connected_agents.empty()) {
        LOG(DEBUG) << "No connected agents";
        return amxd_status_ok;
    }

    std::vector<sMacAddr> target_agents =
        filter_target_agents(connected_agents, network_topology_flags, network_alids,
                             bss_topology_flags, bss_alids);
    if (target_agents.empty()) {
        LOG(DEBUG) << "No agents in scope for BSSTemplate[" << bss_instance_index << "]";
        return amxd_status_ok;
    }

    if (bss_index_seq == 0 || bss_index_seq > 254) {
        LOG(WARNING) << "bss_index_seq out of range";
        return amxd_status_unknown_error;
    }

    const uint8_t row_bss_index = bss_index_seq;
    bool staged_any              = false;

    for (const auto &target_agent_mac : target_agents) {
        auto agent = g_database->get_agent(target_agent_mac);
        if (!agent) {
            continue;
        }

        bool any_radio_matches = false;
        for (const auto &kv : agent->radios) {
            if (!kv.second) {
                continue;
            }
            if (template_radio_matches_operating_classes(*kv.second, bss_info.operating_class)) {
                any_radio_matches = true;
                break;
            }
        }
        if (!any_radio_matches) {
            LOG(DEBUG) << "Skip agent " << target_agent_mac
                       << " (no radio matches RadioTemplate operating classes)";
            continue;
        }

        son::wireless_utils::sBssInfoConf per_bss = bss_info;

        if (!agent->rsn_overriding_supported &&
            per_bss.authentication_type == WSC::eWscAuth::WSC_AUTH_RSN &&
            per_bss.additional_auth ==
                son::wireless_utils::eAdditionalAuth::WPA3_PERSONAL_COMPATIBILITY) {
            bool any_6g = false;
            for (uint8_t oc : per_bss.operating_class) {
                if (oc >= OPCLASS_6GHZ_USING_CENTER_CHANNEL_FIRST &&
                    oc <= OPCLASS_6GHZ_USING_CENTER_CHANNEL_LAST &&
                    oc != OPCLASS_6GHZ_EXCEPTION) {
                    any_6g = true;
                    break;
                }
            }
            if (any_6g) {
                per_bss.authentication_type = WSC::eWscAuth::WSC_AUTH_SAE;
                per_bss.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
            } else {
                per_bss.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
                per_bss.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
            }
            per_bss.additional_auth = son::wireless_utils::eAdditionalAuth::NONE;
            LOG(DEBUG) << "Agent " << target_agent_mac << " lacks RSN override; security downgraded";
        }

        per_bss.bss_index = row_bss_index;
        g_database->add_bss_info_configuration(target_agent_mac, per_bss);
        staged_any = true;
        LOG(DEBUG) << "Staged BSS for agent " << target_agent_mac << " SSID \"" << bss_ssid << "\"";
    }

    if (staged_any) {
        bss_index_seq++;
    }

    return amxd_status_ok;
}

/**
 * @brief Rebuild template-driven staged BSS/MLD from DM.
 *        If Templates.Network.Enable is false: clear everything and renew (no BSS deployed).
 *        If true: re-stage all enabled BSSTemplates (by Priority) and renew.
 */
static void template_rebuild_staged_configuration(amxd_object_t *templates_root)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return;
    }
    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(DEBUG) << "template_rebuild_staged_configuration ignored (use_dataelements_vap_configs is false)";
        return;
    }
    if (!templates_root) {
        LOG(ERROR) << "templates_root is null";
        return;
    }

    amxd_object_t *network_obj = amxd_object_get_child(templates_root, "Network");
    if (!network_obj) {
        LOG(ERROR) << "Network object not found under Templates root";
        return;
    }

    if (!get_param_bool(network_obj, "Enable")) {
        g_database->clear_bss_info_configuration();
        g_database->clear_mld_info_configuration();
        template_send_ap_config_renew_message();
        LOG(DEBUG) << "Templates.Network.Enable=false: cleared staged BSS/MLD and sent AP config renew";
        return;
    }

    amxd_object_t *bss_table = amxd_object_get_child(templates_root, "BSSTemplate");
    if (!bss_table) {
        LOG(ERROR) << "BSSTemplate table not found";
        return;
    }

    std::vector<amxd_object_t *> bss_to_commit;
    amxd_object_for_each(instance, it, bss_table)
    {
        amxd_object_t *inst = amxc_llist_it_get_data(it, amxd_object_t, it);
        if (!get_param_bool(inst, "Enable")) {
            continue;
        }
        if (get_param_string(inst, "SSID").empty()) {
            continue;
        }
        bss_to_commit.push_back(inst);
    }

    std::sort(bss_to_commit.begin(), bss_to_commit.end(),
              [](amxd_object_t *a, amxd_object_t *b) {
                  return get_param_uint32(a, "Priority") > get_param_uint32(b, "Priority");
              });

    g_database->clear_bss_info_configuration();
    g_database->clear_mld_info_configuration();

    uint8_t bss_index_seq = 1;
    for (amxd_object_t *inst : bss_to_commit) {
        amxd_status_t st = template_commit(inst, bss_index_seq);
        if (st != amxd_status_ok) {
            LOG(DEBUG) << "template_commit returned " << amxd_status_string(st) << " for BSSTemplate."
                       << amxd_object_get_index(inst);
        }
    }

    template_send_ap_config_renew_message();
}

/**
 * @brief Handle BSSTemplate.Enable changes under Device.WiFi.Templates.
 *
 * When Templates.Network.Enable is false, all staged BSS/MLD configuration is cleared.
 * When it is true, all enabled BSSTemplates are re-staged (sorted by Priority) because the ODL
 * filter only tracks Enable changes; both enable and disable reduce to "rebuild enabled set".
 */
static void event_template_changed(const char *const sig_name, const amxc_var_t *const data,
                                   void *const priv)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return;
    }

    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(DEBUG) << "Ignoring Templates.BSSTemplate event (use_dataelements_vap_configs is false)";
        return;
    }

    amxd_object_t *bss_template_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    if (!bss_template_obj) {
        LOG(ERROR) << "Failed to get BSSTemplate object from signal";
        return;
    }

    amxd_object_t *bss_template_table = amxd_object_get_parent(bss_template_obj);
    if (!bss_template_table) {
        LOG(ERROR) << "Failed to get BSSTemplate table";
        return;
    }

    amxd_object_t *templates_root = amxd_object_get_parent(bss_template_table);
    if (!templates_root) {
        LOG(ERROR) << "Failed to get Templates root";
        return;
    }

    amxd_object_t *network_obj = amxd_object_get_child(templates_root, "Network");
    if (!network_obj) {
        LOG(ERROR) << "Network object not found under Templates root";
        return;
    }

    LOG(DEBUG) << "event_template_changed: Templates.Network.Enable="
               << (get_param_bool(network_obj, "Enable") ? "true" : "false") << " instance="
               << amxd_object_get_index(bss_template_obj);
    template_rebuild_staged_configuration(templates_root);
}

static void event_templates_network_enable_changed(const char *const sig_name,
                                                   const amxc_var_t *const data,
                                                   void *const priv)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return;
    }
    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(DEBUG) << "Ignoring Templates.Network event (use_dataelements_vap_configs is false)";
        return;
    }

    amxd_object_t *network_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    if (!network_obj) {
        LOG(ERROR) << "Failed to get Templates.Network from signal";
        return;
    }

    amxd_object_t *templates_root = amxd_object_get_parent(network_obj);
    if (!templates_root) {
        LOG(ERROR) << "Failed to get Templates root";
        return;
    }

    LOG(DEBUG) << "event_templates_network_enable_changed: Templates.Network.Enable="
               << (get_param_bool(network_obj, "Enable") ? "true" : "false");

    template_rebuild_staged_configuration(templates_root);
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
        {"event_network_enable_changed", event_network_enable_changed},//};
        {"event_template_changed", event_template_changed},
        {"event_templates_network_enable_changed", event_templates_network_enable_changed},
        {"event_bss_radio_template_reference_changed", event_bss_radio_template_reference_changed},
        {"event_bss_ssc_template_reference_changed", event_bss_ssc_template_reference_changed},
        {"event_network_primary_ssc_reference_changed", event_network_primary_ssc_reference_changed},
        {"event_bss_security_group_reference_changed", event_bss_security_group_reference_changed},
	{"event_security_group_template_references_changed", event_security_group_template_references_changed}};
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
