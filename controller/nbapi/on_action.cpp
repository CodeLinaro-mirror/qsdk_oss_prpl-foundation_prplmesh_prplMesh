/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "on_action.h"

#include <algorithm>
#include <array>
#include <bcl/beerocks_config_file.h>
#include <bcl/beerocks_defines.h>
#include <bcl/son/son_wireless_utils.h>
#include <bcl/beerocks_qos_utils.h>
#include <bcl/beerocks_string_utils.h>
#include <beerocks/tlvf/beerocks_message_bml.h>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale.h>
#include <sstream>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <cstdlib>
#include <limits>
#include <set>
#include <tuple>
#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/wfa_map/tlvTransmitPowerLimit.h>
#include <mapf/common/utils.h>

using namespace beerocks;
using namespace net;
using namespace son;
namespace prplmesh {
namespace controller {
namespace actions {

// Actions

son::db *g_database = nullptr;

namespace {
bool g_templates_dm_initialized         = false;
bool g_templates_commit_pending         = false;
bool g_templates_topology_restage_armed = false;
bool g_templates_apply_in_progress      = false;
std::unordered_map<std::string, int8_t> g_template_applied_tx_power_limit_dbm;
}

static void template_sync_all_linked_ids(amxd_object_t *templates_root);
static bool template_rebuild_staged_configuration(amxd_object_t *templates_root);
static void templates_commit(void);

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
 * @brief Notify target agents to refresh autoconfiguration with unicast renew messages.
 */
static void template_send_ap_config_renew_message(const std::vector<sMacAddr> &target_macs)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return;
    }

    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];
    ieee1905_1::CmduMessageTx cmdu_tx(m_tx_buffer, sizeof(m_tx_buffer));

    auto connected_agents = g_database->get_all_connected_agents();
    if (connected_agents.empty()) {
        LOG(INFO) << "wifi templates: skip AP_CONFIGURATION_RENEW (no connected agents)";
        return;
    }

    if (target_macs.empty()) {
        LOG(DEBUG) << "wifi templates: skip AP_CONFIGURATION_RENEW (no target agents)";
        return;
    }

    for (const auto &mac : target_macs) {
        if (!son_actions::send_ap_config_renew_msg(cmdu_tx, *g_database, mac)) {
            LOG(ERROR) << "Failed to send AP_CONFIGURATION_RENEW_MESSAGE to " << mac;
        } else {
            LOG(INFO) << "wifi templates: sent AP_AUTOCONFIGURATION_RENEW_MESSAGE to " << mac;
        }
    }
}

static std::string template_radio_tx_power_key(const sMacAddr &agent_al_mac, const sMacAddr &radio_uid)
{
    return tlvf::mac_to_string(agent_al_mac) + "|" + tlvf::mac_to_string(radio_uid);
}

static bool template_resolve_nominal_radio_tx_limit_dbm(const sMacAddr &radio_uid, int8_t &tx_limit_dbm)
{
    if (!g_database || radio_uid == beerocks::net::network_utils::ZERO_MAC) {
        return false;
    }

    auto radio = g_database->get_radio_by_uid(radio_uid);
    if (!radio) {
        return false;
    }

    auto current_channel_match = std::find_if(
        radio->supported_channels.begin(), radio->supported_channels.end(),
        [&](const auto &supported_channel) {
            return (supported_channel.get_channel() == radio->wifi_channel.get_channel()) &&
                   (supported_channel.get_bandwidth() == radio->wifi_channel.get_bandwidth()) &&
                   (supported_channel.get_freq_type() == radio->wifi_channel.get_freq_type());
        });
    if (current_channel_match != radio->supported_channels.end()) {
        tx_limit_dbm = static_cast<int8_t>(std::min<int>(current_channel_match->get_tx_power(),
                                                         std::numeric_limits<int8_t>::max()));
        return true;
    }

    if (!radio->supported_channels.empty()) {
        auto strongest_channel = std::max_element(
            radio->supported_channels.begin(), radio->supported_channels.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.get_tx_power() < rhs.get_tx_power(); });
        tx_limit_dbm = static_cast<int8_t>(std::min<int>(strongest_channel->get_tx_power(),
                                                         std::numeric_limits<int8_t>::max()));
        return true;
    }

    if (radio->tx_power > 0 && radio->tx_power <= std::numeric_limits<int8_t>::max()) {
        tx_limit_dbm = static_cast<int8_t>(radio->tx_power);
        return true;
    }

    return false;
}

static bool template_send_radio_transmit_power_limit(const sMacAddr &agent_al_mac,
                                                     const sMacAddr &radio_uid, int8_t limit_dbm)
{
    if (!g_database || agent_al_mac == beerocks::net::network_utils::ZERO_MAC ||
        radio_uid == beerocks::net::network_utils::ZERO_MAC) {
        return false;
    }

    uint8_t tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];
    ieee1905_1::CmduMessageTx cmdu_tx(tx_buffer, sizeof(tx_buffer));

    if (!cmdu_tx.create(0, ieee1905_1::eMessageType::CHANNEL_SELECTION_REQUEST_MESSAGE)) {
        LOG(ERROR) << "Failed creating CHANNEL_SELECTION_REQUEST for template tx power";
        return false;
    }

    auto tlv = cmdu_tx.addClass<wfa_map::tlvTransmitPowerLimit>();
    if (!tlv) {
        LOG(ERROR) << "Failed adding tlvTransmitPowerLimit";
        return false;
    }

    tlv->radio_uid()                = radio_uid;
    tlv->transmit_power_limit_dbm() = limit_dbm;

    if (!son_actions::send_cmdu_to_agent(agent_al_mac, cmdu_tx, *g_database)) {
        LOG(ERROR) << "Failed sending tlvTransmitPowerLimit to " << agent_al_mac;
        return false;
    }

    LOG(DEBUG) << "Template TransmitPowerLimit sent: agent=" << agent_al_mac << " ruid=" << radio_uid
               << " dBm=" << int(limit_dbm);
    return true;
}

static void template_apply_pending_radio_transmit_power_limits(
    const std::vector<std::tuple<sMacAddr, sMacAddr, int8_t>> &desired)
{
    if (!g_database) {
        return;
    }

    std::unordered_map<std::string, int8_t> desired_by_key;
    for (const auto &e : desired) {
        desired_by_key[template_radio_tx_power_key(std::get<0>(e), std::get<1>(e))] = std::get<2>(e);
    }

    for (const auto &kv : desired_by_key) {
        const auto applied_it = g_template_applied_tx_power_limit_dbm.find(kv.first);
        if (applied_it != g_template_applied_tx_power_limit_dbm.end() && applied_it->second == kv.second) {
            continue;
        }
        const auto separator = kv.first.find('|');
        if (separator == std::string::npos) {
            continue;
        }
        const auto agent_al_mac = tlvf::mac_from_string(kv.first.substr(0, separator));
        const auto radio_uid    = tlvf::mac_from_string(kv.first.substr(separator + 1));
        if (template_send_radio_transmit_power_limit(agent_al_mac, radio_uid, kv.second)) {
            g_template_applied_tx_power_limit_dbm[kv.first] = kv.second;
        }
    }

    std::vector<std::string> keys_to_clear;
    for (const auto &applied : g_template_applied_tx_power_limit_dbm) {
        if (desired_by_key.find(applied.first) != desired_by_key.end()) {
            continue;
        }

        const auto separator = applied.first.find('|');
        if (separator == std::string::npos) {
            keys_to_clear.push_back(applied.first);
            continue;
        }

        const auto agent_al_mac = tlvf::mac_from_string(applied.first.substr(0, separator));
        const auto radio_uid    = tlvf::mac_from_string(applied.first.substr(separator + 1));

        int8_t nominal_dbm = 0;
        if (!template_resolve_nominal_radio_tx_limit_dbm(radio_uid, nominal_dbm)) {
            LOG(DEBUG) << "Deferring template tx power rollback until nominal capabilities exist for "
                       << radio_uid;
            continue;
        }
        if (template_send_radio_transmit_power_limit(agent_al_mac, radio_uid, nominal_dbm)) {
            keys_to_clear.push_back(applied.first);
        }
    }

    for (const auto &k : keys_to_clear) {
        g_template_applied_tx_power_limit_dbm.erase(k);
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
        if (band == "5" || band == "5_UNII_1" || band == "5_UNII_2" || band == "5_UNII_3" ||
            band == "5_UNII_4") {
            return beerocks::FREQ_5G;
        }
        if (band == "6" || band == "6_UNII_5" || band == "6_UNII_6" || band == "6_UNII_7" ||
            band == "6_UNII_8") {
            return beerocks::FREQ_6G;
        }
        if (band == "Sub_1GHz") {
            // BBF token exists, but not supported by this controller implementation yet.
            return beerocks::FREQ_UNKNOWN;
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

/** BBF Templates_matching_notes: RSNOE/RSNO2E omitted when all parameters empty or MFP default-only. */
static bool template_rsn_child_has_explicit_nondefault_content(amxd_object_t *o)
{
    if (!o) {
        return false;
    }
    if (!get_param_string(o, "GroupDataCipherSuite").empty() ||
        !get_param_string(o, "PairwiseCipherSuite").empty() ||
        !get_param_string(o, "AKMSuite").empty() || !get_param_string(o, "AKMSuiteSelector").empty() ||
        !get_param_string(o, "GroupManagementCipherSuite").empty()) {
        return true;
    }
    const std::string mfp = get_param_string(o, "MFP");
    return (!mfp.empty() && mfp != "Capable");
}

/** BBF: if 2 or more radios on same 5/6 GHz band, BandFlag must use UNII tokens, not only "5"/"6". */
static unsigned template_agent_radio_count_on_band(const Agent &agent, beerocks::eFreqType band)
{
    unsigned n = 0;
    for (const auto &kv : agent.radios) {
        if (kv.second && kv.second->get_band() == band) {
            ++n;
        }
    }
    return n;
}

static bool template_bandflag_coarse_without_unii(const std::string &band_flag_csv)
{
    if (band_flag_csv.empty()) {
        return false;
    }
    bool saw_unii = false;
    bool saw_coarse = false;
    std::istringstream iss(band_flag_csv);
    std::string token;
    while (std::getline(iss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (token.empty()) {
            continue;
        }
        std::string tl = token;
        std::transform(tl.begin(), tl.end(), tl.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (tl.find("unii") != std::string::npos) {
            saw_unii = true;
        }
        if (token == "5" || token == "6") {
            saw_coarse = true;
        }
    }
    return saw_coarse && !saw_unii;
}

static bool template_radio_bandflag_satisfies_unii_multi_radio_rule(const Agent &agent,
                                                                    beerocks::eFreqType radio_band,
                                                                    const std::string &band_flag_csv)
{
    if (!template_bandflag_coarse_without_unii(band_flag_csv)) {
        return true;
    }
    if (radio_band == beerocks::FREQ_5G) {
        return template_agent_radio_count_on_band(agent, beerocks::FREQ_5G) < 2U;
    }
    if (radio_band == beerocks::FREQ_6G) {
        return template_agent_radio_count_on_band(agent, beerocks::FREQ_6G) < 2U;
    }
    return true;
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
    const std::string current_linked =
        get_param_string(target_object, target_param_name.c_str());

    if (reference_path.empty()) {
        if (current_linked.empty()) {
            return true;
        }

        amxd_trans_t transaction;
        amxd_trans_init(&transaction);
        amxd_trans_set_attr(&transaction, amxd_tattr_change_ro, true);
        amxd_trans_select_object(&transaction, target_object);
        amxd_trans_set_value(cstring_t, &transaction, target_param_name.c_str(), "");
        amxd_status_t status =
            amxd_trans_apply(&transaction, beerocks::nbapi::Amxrt::getDatamodel());
        amxd_trans_clean(&transaction);
        return (status == amxd_status_ok);
    }

    amxd_object_t *referenced_obj = amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(),
                                                   "%s", reference_path.c_str());
    if (!referenced_obj) {
        LOG(WARNING) << "Failed to find referenced object: " << reference_path;
        return false;
    }

    std::string template_id = get_param_string(referenced_obj, template_id_param_name.c_str());

    if (template_id.empty()) {
        LOG(WARNING) << "TemplateID is empty in referenced object: " << reference_path;
        return false;
    }

    if (current_linked == template_id) {
        return true;
    }

    amxd_trans_t transaction;
    amxd_trans_init(&transaction);
    amxd_trans_set_attr(&transaction, amxd_tattr_change_ro, true);
    amxd_trans_select_object(&transaction, target_object);
    amxd_trans_set_value(cstring_t, &transaction, target_param_name.c_str(), template_id.c_str());
    amxd_status_t status =
        amxd_trans_apply(&transaction, beerocks::nbapi::Amxrt::getDatamodel());
    amxd_trans_clean(&transaction);

    if (status == amxd_status_ok) {
        LOG(DEBUG) << "Updated " << target_param_name << " to \"" << template_id
                   << "\" from reference " << reference_path;
        return true;
    }

    LOG(ERROR) << "Failed to update " << target_param_name << ", status: "
               << amxd_status_string(status);
    return false;
}

static bool templates_events_enabled(void)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return false;
    }
    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(ERROR) << "Ignoring Templates event: UseDataElementsVapConfigs is false";
        return false;
    }
    return true;
}

static void template_sync_bss_linked_ids(amxd_object_t *bss_template_obj)
{
    if (!bss_template_obj) {
        return;
    }

    update_linked_template_id(get_param_string(bss_template_obj, "RadioTemplateReference"),
                              "RadioTemplateID", bss_template_obj, "LinkedRadioTemplateID");
    update_linked_template_id(get_param_string(bss_template_obj, "SSCTemplateReference"),
                              "SSCTemplateID", bss_template_obj, "LinkedSSCTemplateID");
    update_linked_template_id(get_param_string(bss_template_obj, "SecurityGroupReference"),
                              "SecurityGroupID", bss_template_obj, "LinkedSecurityGroupID");
    update_linked_template_id(get_param_string(bss_template_obj, "APMLDTemplateReference"),
                              "APMLDTemplateID", bss_template_obj, "LinkedAPMLDTemplateID");
}

static void template_sync_network_primary_ssc(amxd_object_t *network_obj)
{
    if (!network_obj) {
        return;
    }

    update_linked_template_id(get_param_string(network_obj, "PrimarySSCTemplateReference"),
                              "SSCTemplateID", network_obj, "PrimarySSCTemplateID");
}

static void template_sync_security_group_linked_ids(amxd_object_t *security_group_obj)
{
    if (!security_group_obj) {
        return;
    }

    std::string refs_str = get_param_string(security_group_obj, "SecurityTemplateReferences");
    std::vector<std::string> paths = parse_topology_flags(refs_str);
    std::string linked_ids;
    for (size_t i = 0; i < paths.size(); i++) {
        amxd_object_t *ref_obj =
            amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s", paths[i].c_str());
        if (!ref_obj) {
            LOG(WARNING) << "SecurityTemplate reference not found: " << paths[i];
            continue;
        }
        std::string tid = get_param_string(ref_obj, "SecurityTemplateID");
        if (tid.empty()) {
            continue;
        }
        if (!linked_ids.empty()) {
            linked_ids += ",";
        }
        linked_ids += tid;

    }

    const std::string current_linked =
        get_param_string(security_group_obj, "LinkedSecurityTemplateID");
    if (current_linked == linked_ids) {
        return;
    }

    amxd_trans_t transaction;
    amxd_trans_init(&transaction);
    amxd_trans_set_attr(&transaction, amxd_tattr_change_ro, true);
    amxd_trans_select_object(&transaction, security_group_obj);
    amxd_trans_set_value(cstring_t, &transaction, "LinkedSecurityTemplateID", linked_ids.c_str());
    amxd_status_t status = amxd_trans_apply(&transaction, beerocks::nbapi::Amxrt::getDatamodel());
    amxd_trans_clean(&transaction);

    if (status != amxd_status_ok) {
        LOG(ERROR) << "Failed to update LinkedSecurityTemplateID, status: "
                   << amxd_status_string(status);
    }
}

static void template_sync_all_linked_ids(amxd_object_t *templates_root)
{
    if (!templates_root) {
        return;
    }

    amxd_object_t *network_obj = amxd_object_get_child(templates_root, "Network");
    if (network_obj) {
        template_sync_network_primary_ssc(network_obj);
    }

    amxd_object_t *bss_table = amxd_object_get_child(templates_root, "BSSTemplate");
    if (bss_table) {
        amxd_object_for_each(instance, it, bss_table)
        {
            amxd_object_t *inst = amxc_llist_it_get_data(it, amxd_object_t, it);
            if (inst) {
                template_sync_bss_linked_ids(inst);
            }
        }
    }

    amxd_object_t *sg_table = amxd_object_get_child(templates_root, "SecurityGroup");
    if (sg_table) {
        amxd_object_for_each(instance, it, sg_table)
        {
            amxd_object_t *inst = amxc_llist_it_get_data(it, amxd_object_t, it);
            if (inst) {
                template_sync_security_group_linked_ids(inst);
            }
        }
    }
}

static void event_templates_network_configuration_changed(const char *const sig_name,
                                                          const amxc_var_t *const data,
                                                          void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }

    amxd_object_t *network_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    if (!network_obj) {
        LOG(ERROR) << "Failed to get Templates.Network from signal";
        return;
    }

    LOG(DEBUG) << "event_templates_network_configuration_changed: Enable="
               << (get_param_bool(network_obj, "Enable") ? "true" : "false")
               << " TopologyFlag=\"" << get_param_string(network_obj, "TopologyFlag") << "\"";

    templates_commit_request();
    templates_schedule_commit_apply();
}

static void event_bss_template_configuration_changed(const char *const sig_name,
                                                     const amxc_var_t *const data,
                                                     void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }

    amxd_object_t *bss_template_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    if (!bss_template_obj) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.BSSTemplate instance from signal";
        return;
    }

    LOG(DEBUG) << "event_bss_template_configuration_changed: instance="
               << amxd_object_get_index(bss_template_obj)
               << " Enable=" << (get_param_bool(bss_template_obj, "Enable") ? "true" : "false");
    templates_commit_request();
}

static void event_bss_template_instance_changed(const char *const sig_name,
                                                const amxc_var_t *const data,
                                                void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }

    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.BSSTemplate from instance signal";
        return;
    }

    LOG(DEBUG) << "event_bss_template_instance_changed";
    templates_commit_request();
}

static void event_radio_template_configuration_changed(const char *const sig_name,
                                                       const amxc_var_t *const data,
                                                       void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.RadioTemplate instance from signal";
        return;
    }
    LOG(DEBUG) << "event_radio_template_configuration_changed";
    templates_commit_request();
}

static void event_radio_template_instance_changed(const char *const sig_name,
                                                  const amxc_var_t *const data,
                                                  void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.RadioTemplate from instance signal";
        return;
    }
    LOG(DEBUG) << "event_radio_template_instance_changed";
    templates_commit_request();
}

static void event_ssc_template_configuration_changed(const char *const sig_name,
                                                     const amxc_var_t *const data,
                                                     void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.SSCTemplate instance from signal";
        return;
    }
    LOG(DEBUG) << "event_ssc_template_configuration_changed";
    templates_commit_request();
}

static void event_ssc_template_instance_changed(const char *const sig_name,
                                                const amxc_var_t *const data,
                                                void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.SSCTemplate from instance signal";
        return;
    }
    LOG(DEBUG) << "event_ssc_template_instance_changed";
    templates_commit_request();
}

static void event_security_template_configuration_changed(const char *const sig_name,
                                                          const amxc_var_t *const data,
                                                          void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.SecurityTemplate instance from signal";
        return;
    }
    LOG(DEBUG) << "event_security_template_configuration_changed";
    templates_commit_request();
    templates_schedule_commit_apply();
}

static void event_security_template_instance_changed(const char *const sig_name,
                                                     const amxc_var_t *const data,
                                                     void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.SecurityTemplate from instance signal";
        return;
    }
    LOG(DEBUG) << "event_security_template_instance_changed";
    templates_commit_request();
    templates_schedule_commit_apply();
}

static void event_templates_security_group_configuration_changed(const char *const sig_name,
                                                                 const amxc_var_t *const data,
                                                                 void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }

    amxd_object_t *security_group_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    if (!security_group_obj) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.SecurityGroup instance from signal";
        return;
    }

    LOG(DEBUG) << "event_templates_security_group_configuration_changed";
    templates_commit_request();
    templates_schedule_commit_apply();
}

static void event_templates_security_group_instance_changed(const char *const sig_name,
                                                            const amxc_var_t *const data,
                                                            void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.SecurityGroup from instance signal";
        return;
    }
    LOG(DEBUG) << "event_templates_security_group_instance_changed";
    templates_commit_request();
}

static void event_apmld_template_configuration_changed(const char *const sig_name,
                                                       const amxc_var_t *const data,
                                                       void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.APMLDTemplate instance from signal";
        return;
    }
    LOG(DEBUG) << "event_apmld_template_configuration_changed";
    templates_commit_request();
}

static void event_apmld_template_instance_changed(const char *const sig_name,
                                                  const amxc_var_t *const data,
                                                  void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get Device.WiFi.Templates.APMLDTemplate from instance signal";
        return;
    }
    LOG(DEBUG) << "event_apmld_template_instance_changed";
    templates_commit_request();
}

static bool template_agent_has_backhaul_sta(const Agent &agent)
{
    for (const auto &radio_kv : agent.radios) {
        const auto &radio = radio_kv.second;
        if (radio &&
            radio->backhaul_station_mac != beerocks::net::network_utils::ZERO_MAC) {
            return true;
        }
    }
    return false;
}

static bool template_agent_is_wireless_repeater(const Agent &agent)
{
    if (agent.is_gateway) {
        return false;
    }
    if (agent.backhaul.wireless_backhaul_radio != nullptr) {
        return true;
    }
    if (agent.backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_WIFI_UNSPECIFIED ||
        agent.backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_WIFI_INTEL) {
        return true;
    }
    if (template_agent_has_backhaul_sta(agent)) {
        return true;
    }
    if (g_database && agent.backhaul.backhaul_interface != beerocks::net::network_utils::ZERO_MAC &&
        agent.backhaul.backhaul_interface != agent.al_mac) {
        if (g_database->is_sta_wireless(
                tlvf::mac_to_string(agent.backhaul.backhaul_interface))) {
            return true;
        }
    }
    return false;
}

static bool template_track_apmld_ssid_consistency(
    amxd_object_t *bss_template_obj, const std::string &ssid,
    std::unordered_map<std::string, std::string> &apmld_ssid_by_id)
{
    const std::string apmld_ref = get_param_string(bss_template_obj, "APMLDTemplateReference");
    if (apmld_ref.empty()) {
        return true;
    }

    amxd_object_t *apmld_inst =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s", apmld_ref.c_str());
    if (!apmld_inst || !get_param_bool(apmld_inst, "MLOEnable")) {
        return true;
    }

    const std::string mld_key = get_param_string(apmld_inst, "APMLDTemplateID");
    if (mld_key.empty()) {
        return true;
    }

    auto it = apmld_ssid_by_id.find(mld_key);
    if (it == apmld_ssid_by_id.end()) {
        apmld_ssid_by_id.emplace(mld_key, ssid);
        return true;
    }
    if (it->second != ssid) {
        LOG(WARNING) << "APMLDTemplateID " << mld_key
                     << " requires same SSID on all affiliated BSSTemplates";
        return false;
    }
    return true;
}

enum class eTemplateTriState { NULL_VALUE, TRUE_VALUE, FALSE_VALUE };

static eTemplateTriState template_parse_tri_state_flag(const std::string &value)
{
    if (value == "true") {
        return eTemplateTriState::TRUE_VALUE;
    }
    if (value == "false") {
        return eTemplateTriState::FALSE_VALUE;
    }
    return eTemplateTriState::NULL_VALUE;
}

static bool template_tri_state_satisfied(eTemplateTriState required, bool agent_has_trait)
{
    if (required == eTemplateTriState::NULL_VALUE) {
        return true;
    }
    if (required == eTemplateTriState::TRUE_VALUE) {
        return agent_has_trait;
    }
    return !agent_has_trait;
}

static bool template_agent_is_secure_equipment(const Agent &agent)
{
    if (agent.is_gateway) {
        return true;
    }
    return agent.security_capabilities.valid_cipher_suites &&
           agent.security_capabilities.valid_akm_suites;
}

static uint32_t template_agent_wlan_hop_count(const Agent &agent)
{
    if (agent.is_gateway || !g_database) {
        return 0;
    }

    uint32_t hops = 0;
    std::unordered_set<sMacAddr> visited;
    std::shared_ptr<Agent> cur = g_database->get_agent(agent.al_mac);

    while (cur && !cur->is_gateway) {
        if (!visited.insert(cur->al_mac).second) {
            break;
        }
        if (template_agent_is_wireless_repeater(*cur)) {
            ++hops;
        }
        auto parent = cur->backhaul.parent_agent.lock();
        if (!parent) {
            break;
        }
        cur = parent;
    }
    return hops;
}

static bool template_radio_agent_supports_dfs(const Agent::sRadio &radio)
{
    if (!radio.supports_5ghz) {
        return false;
    }
    for (const auto &channel : radio.supported_channels) {
        if (channel.is_dfs_channel()) {
            return true;
        }
    }
    return false;
}

static bool template_radio_agent_supports_afc(const Agent::sRadio &radio)
{
    return radio.supports_6ghz;
}

static bool template_operating_classes_include_5g(
    const std::list<uint8_t> &operating_classes)
{
    for (uint8_t oc : operating_classes) {
        if (oc >= OPCLASS_5GHZ_USING_CENTER_CHANNEL_FIRST &&
            oc <= OPCLASS_5GHZ_USING_CENTER_CHANNEL_LAST) {
            return true;
        }
        if (oc >= 115 && oc <= 130) {
            return true;
        }
    }
    return false;
}

static bool template_operating_classes_include_6g(
    const std::list<uint8_t> &operating_classes)
{
    for (uint8_t oc : operating_classes) {
        if (oc >= OPCLASS_6GHZ_USING_CENTER_CHANNEL_FIRST &&
            oc <= OPCLASS_6GHZ_USING_CENTER_CHANNEL_LAST &&
            oc != OPCLASS_6GHZ_EXCEPTION) {
            return true;
        }
    }
    return false;
}

static bool template_agent_is_wired_repeater(const Agent &agent)
{
    if (agent.is_gateway || template_agent_is_wireless_repeater(agent)) {
        return false;
    }
    return (agent.backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_ETHERNET ||
            agent.backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_BRIDGE ||
            agent.backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_GW_BRIDGE);
}

/**
 * BBF Templates: TopologyFlag matches if any list item matches (OR). Empty list means no
 * topology-role restriction from that parameter. "ALID" uses the paired IEEE1905ALID MAC list.
 */
static bool template_agent_matches_topology_flags(const Agent &agent, const std::vector<std::string> &flags,
                                             const std::vector<sMacAddr> &alids_for_alid_token,
                                             bool ignore_topology_flags)
{
    if (ignore_topology_flags) {
        return true;
    }

    if (flags.empty()) {
        return true;
    }
    const bool is_root              = agent.is_gateway;
    const bool is_repeater          = !agent.is_gateway;
    const bool is_wired_repeater    = template_agent_is_wired_repeater(agent);
    const bool is_wireless_repeater = template_agent_is_wireless_repeater(agent);

    for (const auto &flag : flags) {
        if (flag == "Root" && is_root) {
            return true;
        }
        if (flag == "Repeater" && is_repeater) {
            return true;
        }
        if (flag == "Wired_Repeater" && is_wired_repeater) {
            return true;
        }
        if (flag == "Wireless_Repeater" && is_wireless_repeater) {
            return true;
        }
        if (flag == "ALID") {
            std::string alid_list_str;
            for (const auto &mac : alids_for_alid_token) {
                if (!alid_list_str.empty()) {
                    alid_list_str += ", ";
                }
                alid_list_str += tlvf::mac_to_string(mac);
            }
            LOG(INFO) << "Evaluating ALID override. Target Agent: " << agent.al_mac << " | Whitelisted ALIDs: [" << alid_list_str << "]";

            if (std::find(alids_for_alid_token.begin(), alids_for_alid_token.end(), agent.al_mac) !=
                alids_for_alid_token.end()) {
                    return true;
            }
        }
    }
    return false;
}

static std::vector<sMacAddr> template_filter_target_agents(
    const std::vector<std::shared_ptr<Agent>> &connected_agents,
    const std::vector<std::string> &network_topology_flags,
    const std::vector<sMacAddr> &network_alids,
    const std::vector<std::string> &bss_topology_flags,
    const std::vector<sMacAddr> &bss_alids)
{
    std::vector<sMacAddr> target_agents;
    const bool ignore_network_topology =
        (connected_agents.size() <= 1) && network_topology_flags.empty();
    const bool ignore_bss_topology =
        (connected_agents.size() <= 1) && bss_topology_flags.empty();

    for (const auto &agent : connected_agents) {
        if (!agent) {
            continue;
        }

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
        }

        const bool match_network = template_agent_matches_topology_flags(
            *agent, network_topology_flags, network_alids, ignore_network_topology);
        const bool match_bss = template_agent_matches_topology_flags(
            *agent, bss_topology_flags, bss_alids, ignore_bss_topology);
        const bool should_deploy = match_network && match_bss;

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

/** Legacy suites advertised by agent capability_reporting_task when TLV cache is empty. */
static bool template_legacy_akm_suite_type(uint8_t suite_type)
{
    return suite_type == 0x02 || suite_type == 0x08 || suite_type == 0x18;
}

static bool template_legacy_cipher_suite_type(uint8_t suite_type)
{
    return suite_type == 0x04 || suite_type == 0x06 || suite_type == 0x09;
}

static const char *DPP_AKM_SELECTOR = "506F9A02"; // excluded from deployment per product scope

static bool template_agent_akm_suite_type_supported(const Agent &agent, bool fronthaul, bool backhaul,
                                                     uint8_t suite_type)
{
    if (!agent.security_capabilities.valid_akm_suites) {
        return template_legacy_akm_suite_type(suite_type);
    }
    std::bitset<256> s;
    if (fronthaul || !backhaul) {
        s |= agent.security_capabilities.fronthaul_akm_suite_types;
    }
    if (backhaul) {
        s |= agent.security_capabilities.backhaul_akm_suite_types;
    }
    return s.test(suite_type);
}

static bool template_agent_cipher_suite_type_supported(const Agent &agent, uint8_t suite_type)
{
    if (!agent.security_capabilities.valid_cipher_suites) {
        return template_legacy_cipher_suite_type(suite_type);
    }
    return agent.security_capabilities.cipher_suite_types.test(suite_type);
}

static bool template_parse_suite_token(std::string tok, std::array<uint8_t, 4> &out);

static bool template_security_rsne_cipher_suites_match_agent(const Agent &agent, amxd_object_t *rsne_obj)
{
    if (!rsne_obj) {
        return true;
    }
    auto check_csv = [&](const char *param) {
        for (const auto &t : parse_topology_flags(get_param_string(rsne_obj, param))) {
            std::array<uint8_t, 4> oui{};
            if (!template_parse_suite_token(t, oui)) {
                continue;
            }
            if (oui[0] == 0x00 && oui[1] == 0x0F && oui[2] == 0xAC) {
                if (!template_agent_cipher_suite_type_supported(agent, oui[3])) {
                    return false;
                }
            }
        }
        return true;
    };
    if (!check_csv("PairwiseCipherSuite")) {
        return false;
    }
    if (!get_param_string(rsne_obj, "GroupDataCipherSuite").empty()) {
        std::array<uint8_t, 4> g{};
        if (template_parse_suite_token(get_param_string(rsne_obj, "GroupDataCipherSuite"), g) &&
            g[0] == 0x00 && g[1] == 0x0F && g[2] == 0xAC &&
            !template_agent_cipher_suite_type_supported(agent, g[3])) {
            return false;
        }
    }
    return true;
}

/** Minimal RSNE scan: verify 00-0F-AC AKM suite types in first RSNE (0x30) in concatenated IEs. */
static bool template_security_ies_hex_akms_match_agent(const Agent &agent, bool fh, bool bh,
                                                       const std::vector<uint8_t> &ies)
{
    if (!agent.security_capabilities.valid_akm_suites) {
        return true;
    }
    for (size_t i = 0; i + 1 < ies.size();) {
        uint8_t id  = ies[i++];
        uint8_t len = ies[i++];
        if (i + len > ies.size()) {
            break;
        }
        if (id == 0x30 && len >= 14) {
            size_t p = i;
            p += 2; // version
            p += 4; // group
            if (p + 2 > i + len) {
                break;
            }
            uint16_t n_pw = static_cast<uint16_t>(ies[p] | (uint16_t(ies[p + 1]) << 8));
            p += 2;
            if (p + n_pw * 4 > i + len) {
                break;
            }
            p += n_pw * 4;
            if (p + 2 > i + len) {
                break;
            }
            uint16_t n_akm = static_cast<uint16_t>(ies[p] | (uint16_t(ies[p + 1]) << 8));
            p += 2;
            for (uint16_t k = 0; k < n_akm && p + 4 <= i + len; ++k, p += 4) {
                if (ies[p] == 0x00 && ies[p + 1] == 0x0F && ies[p + 2] == 0xAC) {
                    if (!template_agent_akm_suite_type_supported(agent, fh, bh, ies[p + 3])) {
                        return false;
                    }
                }
            }
            return true;
        }
        i += len;
    }
    return true;
}

static bool template_operating_classes_include_6ghz(const std::list<uint8_t> &operating_class)
{
    for (uint8_t oc : operating_class) {
        if (oc >= OPCLASS_6GHZ_USING_CENTER_CHANNEL_FIRST &&
            oc <= OPCLASS_6GHZ_USING_CENTER_CHANNEL_LAST && oc != OPCLASS_6GHZ_EXCEPTION) {
            return true;
        }
    }
    return false;
}

static void template_merge_unique_csv(std::vector<std::string> &dst, const std::string &csv)
{
    for (const auto &t : parse_topology_flags(csv)) {
        if (std::find(dst.begin(), dst.end(), t) == dst.end()) {
            dst.push_back(t);
        }
    }
}

static void template_merge_akm_from_rsn_child_objects(amxd_object_t *security_template_obj,
                                                     std::vector<std::string> &akm_tokens,
                                                     std::string &merged_akm_selector_csv)
{
    static const char *k_rsne_children[] = {"RSNE", "RSNOE", "RSNO2E"};
    for (const char *name : k_rsne_children) {
        amxd_object_t *o = amxd_object_get_child(security_template_obj, name);
        if (!o) {
            continue;
        }
        template_merge_unique_csv(akm_tokens, get_param_string(o, "AKMSuite"));
        const std::string sel = get_param_string(o, "AKMSuiteSelector");
        if (!sel.empty()) {
            if (std::find(akm_tokens.begin(), akm_tokens.end(), std::string("SuiteSelector")) ==
                akm_tokens.end()) {
                akm_tokens.push_back("SuiteSelector");
            }
            if (merged_akm_selector_csv.empty()) {
                merged_akm_selector_csv = sel;
            }
        }
    }
}

static void template_merge_saeh2e_from_rsnx(amxd_object_t *security_template_obj,
                                            std::vector<std::string> &akm_tokens)
{
    static const char *k_rsnx[] = {"RSNXE", "RSNXOE"};
    for (const char *name : k_rsnx) {
        amxd_object_t *o = amxd_object_get_child(security_template_obj, name);
        if (!o || !get_param_bool(o, "SAEH2E")) {
            continue;
        }
        if (std::find(akm_tokens.begin(), akm_tokens.end(), std::string("sae")) == akm_tokens.end()) {
            akm_tokens.push_back("sae");
        }
        return;
    }
}

static void template_append_u16_le(std::vector<uint8_t> &v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xff));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xff));
}

static void template_finalize_ie_len(std::vector<uint8_t> &v, size_t len_idx)
{
    if (len_idx >= v.size()) {
        return;
    }
    v[len_idx] = static_cast<uint8_t>(v.size() - len_idx - 1);
}

/** RSNE / WFA RSNO* payload body: Version through Group Management Cipher Suite (PMKID count 0). */
static void template_append_rsne_like_body(std::vector<uint8_t> &v,
                                            const std::array<uint8_t, 4> &group,
                                            const std::vector<std::array<uint8_t, 4>> &pairwise,
                                            const std::vector<std::array<uint8_t, 4>> &akm,
                                            uint16_t rsn_cap_le,
                                            const std::array<uint8_t, 4> &group_mgmt)
{
    template_append_u16_le(v, 1); // version
    v.insert(v.end(), group.begin(), group.end());
    const uint16_t n_pw = static_cast<uint16_t>(std::min<size_t>(pairwise.size(), 255));
    template_append_u16_le(v, n_pw);
    for (uint16_t i = 0; i < n_pw; ++i) {
        v.insert(v.end(), pairwise[i].begin(), pairwise[i].end());
    }
    const uint16_t n_akm = static_cast<uint16_t>(std::min<size_t>(akm.size(), 255));
    template_append_u16_le(v, n_akm);
    for (uint16_t i = 0; i < n_akm; ++i) {
        v.insert(v.end(), akm[i].begin(), akm[i].end());
    }
    template_append_u16_le(v, rsn_cap_le);
    template_append_u16_le(v, 0); // PMKID count
    v.insert(v.end(), group_mgmt.begin(), group_mgmt.end());
}

static bool template_parse_suite_token(std::string tok, std::array<uint8_t, 4> &out)
{
    tok.erase(0, tok.find_first_not_of(" \t"));
    tok.erase(tok.find_last_not_of(" \t") + 1);
    if (tok.empty()) {
        return false;
    }
    for (auto &c : tok) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (tok == "ccmp" || tok == "aes" || tok == "ccmp-128" || tok == "ccmp128") {
        out = {{0x00, 0x0F, 0xAC, 0x04}};
        return true;
    }
    if (tok == "gcmp" || tok == "gcmp-128") {
        out = {{0x00, 0x0F, 0xAC, 0x08}};
        return true;
    }
    if (tok == "gcmp-256" || tok == "gcmp256") {
        out = {{0x00, 0x0F, 0xAC, 0x09}};
        return true;
    }
    std::string hex;
    for (char c : tok) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            hex += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    if (hex.size() != 8) {
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        out[i] = static_cast<uint8_t>(std::strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16));
    }
    return true;
}

static void template_read_cipher_list(amxd_object_t *obj, const char *param,
                                      std::vector<std::array<uint8_t, 4>> &out,
                                      const std::array<uint8_t, 4> &def_one)
{
    out.clear();
    if (!obj) {
        out.push_back(def_one);
        return;
    }
    const std::string csv = get_param_string(obj, param);
    if (csv.empty()) {
        out.push_back(def_one);
        return;
    }
    for (const auto &t : parse_topology_flags(csv)) {
        std::array<uint8_t, 4> s{};
        if (template_parse_suite_token(t, s)) {
            out.push_back(s);
        }
    }
    if (out.empty()) {
        out.push_back(def_one);
    }
}

static bool template_read_one_suite(amxd_object_t *obj, const char *param,
                                     const std::array<uint8_t, 4> &deflt, std::array<uint8_t, 4> &out)
{
    if (!obj) {
        out = deflt;
        return false;
    }
    const std::string s = get_param_string(obj, param);
    if (s.empty()) {
        out = deflt;
        return false;
    }
    std::string tok = s;
    if (template_parse_suite_token(tok, out)) {
        return true;
    }
    out = deflt;
    return false;
}

static uint16_t template_mfp_to_rsn_cap_le(const std::string &mfp, bool vendor_extension)
{
    std::string m = mfp;
    for (auto &c : m) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (m == "required") {
        return vendor_extension ? static_cast<uint16_t>(0x0CC0) : static_cast<uint16_t>(0x00C0);
    }
    if (m == "disabled") {
        return 0;
    }
    return vendor_extension ? static_cast<uint16_t>(0x000C) : static_cast<uint16_t>(0x0008);
}

static bool template_security_template_has_rsn_dm_overrides(amxd_object_t *security_template_obj)
{
    static const char *k_children[] = {"RSNE", "RSNOE", "RSNO2E", "RSNXE", "RSNXOE"};
    for (const char *name : k_children) {
        amxd_object_t *o = amxd_object_get_child(security_template_obj, name);
        if (!o) {
            continue;
        }
        if (!get_param_string(o, "GroupDataCipherSuite").empty() ||
            !get_param_string(o, "PairwiseCipherSuite").empty() ||
            !get_param_string(o, "AKMSuite").empty() || !get_param_string(o, "AKMSuiteSelector").empty() ||
            !get_param_string(o, "GroupManagementCipherSuite").empty()) {
            return true;
        }
        const std::string mfp = get_param_string(o, "MFP");
        if (!mfp.empty() && mfp != "Capable") {
            return true;
        }
        if (get_param_bool(o, "SAEH2E")) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Build WPA3-PCM RSN IE chain for tlvRsnParametersConfiguration.
 * @param security_template_obj SecurityTemplate DM object.
 * @param is_6ghz_band True when BSS operating classes include 6 GHz.
 * @param out Output concatenated Security IE octets.
 */
static void template_fill_wpa3_pcm_rsn_ies(amxd_object_t *security_template_obj, bool is_6ghz_band,
                                           std::vector<uint8_t> &out)
{
    static const std::array<uint8_t, 4> k_ccmp     = {{0x00, 0x0F, 0xAC, 0x04}};
    static const std::array<uint8_t, 4> k_psk_akm  = {{0x00, 0x0F, 0xAC, 0x02}};
    static const std::array<uint8_t, 4> k_sae_akm  = {{0x00, 0x0F, 0xAC, 0x08}};
    static const std::array<uint8_t, 4> k_h2e_akm  = {{0x00, 0x0F, 0xAC, 0x18}};
    static const std::array<uint8_t, 4> k_bip_cmac = {{0x00, 0x0F, 0xAC, 0x06}};

    out.clear();
    if (!template_security_template_has_rsn_dm_overrides(security_template_obj)) {
        if (is_6ghz_band) {
            out.insert(out.end(), wpa3_pcm_6g_eht.begin(), wpa3_pcm_6g_eht.end());
        } else {
            out.insert(out.end(), wpa3_pcm_2g_5g_eht.begin(), wpa3_pcm_2g_5g_eht.end());
        }
        return;
    }

    amxd_object_t *rsne   = amxd_object_get_child(security_template_obj, "RSNE");
    amxd_object_t *rsnoe  = amxd_object_get_child(security_template_obj, "RSNOE");
    amxd_object_t *rsno2e = amxd_object_get_child(security_template_obj, "RSNO2E");
    amxd_object_t *rsnxe = amxd_object_get_child(security_template_obj, "RSNXE");

    std::array<uint8_t, 4> group = k_ccmp;
    template_read_one_suite(rsne, "GroupDataCipherSuite", k_ccmp, group);

    std::vector<std::array<uint8_t, 4>> pw_rsne;
    template_read_cipher_list(rsne, "PairwiseCipherSuite", pw_rsne, k_ccmp);

    std::array<uint8_t, 4> gm_rsne = k_bip_cmac;
    template_read_one_suite(rsne, "GroupManagementCipherSuite", k_bip_cmac, gm_rsne);

    const std::string mfp_rsne = rsne ? get_param_string(rsne, "MFP") : std::string("Capable");
    const uint16_t cap_rsne    = template_mfp_to_rsn_cap_le(mfp_rsne.empty() ? "Capable" : mfp_rsne, false);

    std::vector<std::array<uint8_t, 4>> akm_rsne;
    if (is_6ghz_band) {
        akm_rsne.push_back(k_sae_akm);
    } else {
        akm_rsne.push_back(k_psk_akm);
    }

    out.push_back(0x30);
    size_t rsne_len = out.size();
    out.push_back(0);
    template_append_rsne_like_body(out, group, pw_rsne, akm_rsne, cap_rsne, gm_rsne);
    template_finalize_ie_len(out, rsne_len);

    if (!is_6ghz_band) {
        std::array<uint8_t, 4> g2 = group, gm2 = gm_rsne;
        std::vector<std::array<uint8_t, 4>> pw2, ak2;
        template_read_cipher_list(rsnoe, "PairwiseCipherSuite", pw2, k_ccmp);
        template_read_one_suite(rsnoe, "GroupDataCipherSuite", group, g2);
        ak2.push_back(k_sae_akm);
        const std::string mfp2 = rsnoe ? get_param_string(rsnoe, "MFP") : std::string("Capable");
        const uint16_t cap2 =
            template_mfp_to_rsn_cap_le(mfp2.empty() ? "Capable" : mfp2, true);
        template_read_one_suite(rsnoe, "GroupManagementCipherSuite", k_bip_cmac, gm2);

        out.push_back(0xDD);
        size_t l2 = out.size();
        out.push_back(0);
        out.insert(out.end(), {0x50, 0x6F, 0x9A, 0x29});
        template_append_rsne_like_body(out, g2, pw2, ak2, cap2, gm2);
        template_finalize_ie_len(out, l2);
    }

    if (template_rsn_child_has_explicit_nondefault_content(rsno2e)) {
        std::array<uint8_t, 4> g3 = group, gm3 = gm_rsne;
        std::vector<std::array<uint8_t, 4>> pw3, ak3;
        const std::array<uint8_t, 4> k_pw_rsno2_def = {{0x00, 0x0F, 0xAC, 0x09}};
        template_read_cipher_list(rsno2e, "PairwiseCipherSuite", pw3, k_pw_rsno2_def);
        template_read_one_suite(rsno2e, "GroupDataCipherSuite", group, g3);
        ak3.push_back(k_h2e_akm);
        const std::string mfp3 = rsno2e ? get_param_string(rsno2e, "MFP") : std::string("Capable");
        const uint16_t cap3 = template_mfp_to_rsn_cap_le(mfp3.empty() ? "Capable" : mfp3, true);
        template_read_one_suite(rsno2e, "GroupManagementCipherSuite", k_bip_cmac, gm3);

        out.push_back(0xDD);
        size_t l3 = out.size();
        out.push_back(0);
        out.insert(out.end(), {0x50, 0x6F, 0x9A, 0x2A});
        template_append_rsne_like_body(out, g3, pw3, ak3, cap3, gm3);
        template_finalize_ie_len(out, l3);
    }

    if (is_6ghz_band && (!rsnxe || get_param_bool(rsnxe, "SAEH2E"))) {
        out.push_back(0xF4);
        out.push_back(0x01);
        out.push_back(static_cast<uint8_t>(0x20));
    }

    out.push_back(0xDD);
    out.push_back(0x05);
    out.insert(out.end(), {0x50, 0x6F, 0x9A, 0x2B, 0x20});
}

/** BBF: SecurityIEs is hex (optional separators); used verbatim when non-empty. */
static bool template_parse_security_ies_hex(const std::string &in, std::vector<uint8_t> &out)
{
    out.clear();
    std::string hex;
    for (char c : in) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ':' || c == '-' || c == ',') {
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            LOG(WARNING) << "SecurityIEs: non-hex character";
            return false;
        }
        hex += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (hex.empty()) {
        return false;
    }
    if (hex.size() % 2 != 0) {
        LOG(WARNING) << "SecurityIEs: odd number of hex digits";
        return false;
    }
    constexpr size_t k_max_security_ies = 2048;
    if (hex.size() / 2 > k_max_security_ies) {
        LOG(WARNING) << "SecurityIEs: exceeds max length";
        return false;
    }
    for (size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return true;
}

static bool template_security_akm_name_supported_by_agent(const Agent &agent, bool fronthaul,
                                                          bool backhaul, const std::string &name_lc)
{
    if (!agent.security_capabilities.valid_akm_suites) {
        if (name_lc == "psk") {
            return true;
        }
        if (name_lc == "sae" || name_lc == "sae-ext-key") {
            return true;
        }
        return false;
    }
    std::bitset<256> supported;
    if (fronthaul || !backhaul) {
        supported |= agent.security_capabilities.fronthaul_akm_suite_types;
    }
    if (backhaul) {
        supported |= agent.security_capabilities.backhaul_akm_suite_types;
    }
    auto has = [&](uint8_t suite_type) { return supported.test(suite_type); };

    if (name_lc == "psk") {
        return has(0x02);
    }
    if (name_lc == "sae") {
        return has(0x08) || has(0x18);
    }
    if (name_lc == "sae-ext-key") {
        return has(0x18);
    }
    if (name_lc == "suiteb192") {
        return has(0x05) || has(0x06);
    }
    /* Unknown string token do not pretend it matched (TMN: each listed suite must match). */
    return false;
}

static bool template_security_akm_flags_match_agent(const Agent &agent, bool fronthaul, bool backhaul,
                                                   const std::vector<std::string> &akm_flags,
                                                   const std::vector<std::string> &rsne_akm_list)
{
    std::vector<std::string> merged;
    for (const auto &t : akm_flags) {
        merged.push_back(t);
    }
    for (const auto &t : rsne_akm_list) {
        merged.push_back(t);
    }
    for (auto &t : merged) {
        std::transform(t.begin(), t.end(), t.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    merged.erase(std::remove_if(merged.begin(), merged.end(),
                                [](const std::string &s) {
                                    return s.empty() || s == "suiteselector";
                                }),
                  merged.end());
    std::sort(merged.begin(), merged.end());
    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());

    for (const auto &t : merged) {
        if (!template_security_akm_name_supported_by_agent(agent, fronthaul, backhaul, t)) {
            LOG(DEBUG) << "SecurityTemplate AKM token '" << t << "' not supported by agent "
                       << agent.al_mac;
            return false;
        }
    }
    return true;
}

/**
 * @brief Apply SecurityTemplate DM fields to bss_info (WSC auth / encr / additional_auth).
 * BBF: non-empty SecurityIEs overrides structured RSNE/RSNOE/RSNO2E parameters.
 * @return false if SecurityIEs hex is invalid or no supported AKM / selector path applies.
 */
static bool template_apply_security_to_bss_info(amxd_object_t *security_template_obj,
                                                const Agent *agent_for_match,
                                                son::wireless_utils::sBssInfoConf &bss_info,
                                                const std::list<uint8_t> &operating_classes)
{
    bss_info.rsn_security_ies.clear();

    std::string security_ies = get_param_string(security_template_obj, "SecurityIEs");
    security_ies.erase(0, security_ies.find_first_not_of(" \t"));
    security_ies.erase(security_ies.find_last_not_of(" \t") + 1);
    if (!security_ies.empty()) {
        if (!template_parse_security_ies_hex(security_ies, bss_info.rsn_security_ies)) {
            LOG(WARNING) << "SecurityTemplate SecurityIEs present but failed to parse";
            return false;
        }
        bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_RSN;
        bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
        bss_info.additional_auth     = son::wireless_utils::eAdditionalAuth::NONE;
        LOG(DEBUG) << "SecurityTemplate: SecurityIEs override (" << bss_info.rsn_security_ies.size()
                   << " octets), ignoring structured RSN* parameters";
        if (agent_for_match &&
            !template_security_ies_hex_akms_match_agent(*agent_for_match, bss_info.fronthaul,
                                                        bss_info.backhaul, bss_info.rsn_security_ies)) {
            LOG(DEBUG) << "SecurityTemplate SecurityIEs: AKM suites not supported by agent";
            return false;
        }
        return true;
    }

    std::string rsno_support_flag       = get_param_string(security_template_obj, "RSNOSupportFlag");
    std::string supported_akm_flag      = get_param_string(security_template_obj, "SupportedAKMSuiteFlag");
    std::string supported_selector_flag = get_param_string(security_template_obj, "SupportedAKMSuiteSelectorFlag");

    const bool rsno_support = (rsno_support_flag == "true");

    if (agent_for_match) {
        if (rsno_support_flag == "true" && !agent_for_match->rsn_overriding_supported) {
            LOG(DEBUG) << "SecurityTemplate RSNOSupportFlag=true but agent lacks RSNO / RSN override";
            return false;
        }
        if (rsno_support_flag == "false" && agent_for_match->rsn_overriding_supported) {
            LOG(DEBUG) << "SecurityTemplate RSNOSupportFlag=false but agent reports RSNO support";
            return false;
        }
    }

    amxd_object_t *rsne_obj = amxd_object_get_child(security_template_obj, "RSNE");
    std::string rsne_akm;
    std::string rsne_akm_selector_str;
    if (rsne_obj) {
        rsne_akm              = get_param_string(rsne_obj, "AKMSuite");
        rsne_akm_selector_str = get_param_string(rsne_obj, "AKMSuiteSelector");
    }

    std::vector<std::string> akm_flags     = parse_topology_flags(supported_akm_flag);
    std::vector<std::string> rsne_akm_list = parse_topology_flags(rsne_akm);
    template_merge_akm_from_rsn_child_objects(security_template_obj, rsne_akm_list,
                                              rsne_akm_selector_str);
    template_merge_saeh2e_from_rsnx(security_template_obj, rsne_akm_list);

    if (agent_for_match && rsne_obj &&
        !template_security_rsne_cipher_suites_match_agent(*agent_for_match, rsne_obj)) {
        LOG(DEBUG) << "SecurityTemplate RSNE cipher suites not supported by agent " << agent_for_match->al_mac;
        return false;
    }

    auto contains = [](const std::vector<std::string> &vec, const std::string &s) {
        return std::find(vec.begin(), vec.end(), s) != vec.end();
    };

    const bool use_suite_selector =
        contains(akm_flags, "SuiteSelector") || contains(rsne_akm_list, "SuiteSelector");

    if (agent_for_match) {
        if (!template_security_akm_flags_match_agent(*agent_for_match, bss_info.fronthaul,
                                                      bss_info.backhaul, akm_flags, rsne_akm_list)) {
            return false;
        }
    }

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
            if (sel == std::string(DPP_AKM_SELECTOR)) {
                LOG(DEBUG) << "SecurityTemplate: DPP AKM selector excluded";
                return false;
            }
            if (sel == OWE_AKM_SELECTOR) {
                if (!agent_for_match ||
                    !template_agent_akm_suite_type_supported(*agent_for_match, bss_info.fronthaul,
                                                             bss_info.backhaul, 0x12)) {
                    LOG(DEBUG) << "SecurityTemplate: OWE AKM (0x12) not supported by agent";
                    return false;
                }
                bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_OPEN;
                bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
                bss_info.additional_auth     = son::wireless_utils::eAdditionalAuth::NONE;
                LOG(DEBUG) << "SecurityTemplate: OWE (AKM suite selector)";
                return true;
            }
            if (sel.size() == 8 && sel.rfind("000FAC", 0) == 0) {
                if (!agent_for_match) {
                    return false;
                }
                const unsigned long st =
                    std::strtoul(sel.substr(6, 2).c_str(), nullptr, 16);
                const auto suite_type = static_cast<uint8_t>(st);
                if (!template_agent_akm_suite_type_supported(*agent_for_match, bss_info.fronthaul,
                                                               bss_info.backhaul, suite_type)) {
                    LOG(DEBUG) << "SecurityTemplate: selector " << sel << " not supported by agent";
                    return false;
                }
                bss_info.encryption_type = WSC::eWscEncr::WSC_ENCR_AES;
                bss_info.additional_auth = son::wireless_utils::eAdditionalAuth::NONE;
                if (suite_type == 0x02) {
                    bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
                } else if (suite_type == 0x08 || suite_type == 0x18) {
                    bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_SAE;
                } else {
                    LOG(DEBUG) << "SecurityTemplate: unmapped IEEE AKM selector " << sel;
                    return false;
                }
                LOG(DEBUG) << "SecurityTemplate: IEEE AKM selector " << sel;
                return true;
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
            const bool is_6g = template_operating_classes_include_6ghz(operating_classes);
            template_fill_wpa3_pcm_rsn_ies(security_template_obj, is_6g, bss_info.rsn_security_ies);
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
        return true;
    }

    if (contains(akm_flags, "psk") || contains(rsne_akm_list, "psk")) {
        bss_info.authentication_type = WSC::eWscAuth::WSC_AUTH_WPA2PSK;
        bss_info.encryption_type     = WSC::eWscEncr::WSC_ENCR_AES;
        bss_info.additional_auth     = son::wireless_utils::eAdditionalAuth::NONE;
        LOG(DEBUG) << "SecurityTemplate: WPA2-Personal";
        return true;
    }

    LOG(DEBUG) << "SecurityTemplate: no supported AKM combination";
    return false;
}

/** Comma-separated ALID list from Templates.Network / BSSTemplate.IEEE1905ALID. */
static void template_parse_alid_csv(const std::string &csv, std::vector<sMacAddr> &alids)
{
    alids.clear();
    if (csv.empty()) {
        return;
    }
    std::istringstream iss(csv);
    std::string mac_str;
    while (std::getline(iss, mac_str, ',')) {
        mac_str.erase(0, mac_str.find_first_not_of(" \t"));
        mac_str.erase(mac_str.find_last_not_of(" \t") + 1);
        if (mac_str.empty()) {
            continue;
        }
        sMacAddr alid = tlvf::mac_from_string(mac_str);
        if (alid != beerocks::net::network_utils::ZERO_MAC) {
            alids.push_back(alid);
        }
    }
}

static std::string template_tr181_row_path(const char *table_name, amxd_object_t *row)
{
    if (!table_name || !row) {
        return {};
    }
    return std::string("Device.WiFi.Templates") + "." + table_name + "." +
           std::to_string(amxd_object_get_index(row));
}

struct sRadioTemplateRow {
    amxd_object_t *radio_inst = nullptr;
    uint32_t priority         = 0;
    std::list<uint8_t> operating_class;
    std::string operating_generation;
    std::string supported_generation_flag_csv;
    std::string band_flag_csv;
    bool has_transmit_power_limit = false;
    int8_t transmit_power_limit_dbm = 0;
    eTemplateTriState dfs_support_flag = eTemplateTriState::NULL_VALUE;
    eTemplateTriState afc_support_flag = eTemplateTriState::NULL_VALUE;
};

struct sAgentBssIndexState {
    std::unordered_map<std::string, std::set<uint8_t>> used_bss_indexes_by_radio;
};

static std::string template_bss_index_scope_key(const sMacAddr &agent_al_mac, const sMacAddr &radio_uid)
{
    return tlvf::mac_to_string(agent_al_mac) + "|" + tlvf::mac_to_string(radio_uid);
}

static uint8_t template_allocate_stable_bss_index(
    const sMacAddr &agent_al_mac, const sMacAddr &radio_uid,
    const std::list<son::wireless_utils::sBssInfoConf> &previous_bss_configs,
    const son::wireless_utils::sBssInfoConf &bss_info, sAgentBssIndexState &selection_state)
{
    auto radio_key         = template_bss_index_scope_key(agent_al_mac, radio_uid);
    auto &used_bss_indexes = selection_state.used_bss_indexes_by_radio[radio_key];

    for (const auto &previous_bss : previous_bss_configs) {
        if ((previous_bss.target_radio_uid != beerocks::net::network_utils::ZERO_MAC) &&
            (previous_bss.target_radio_uid != radio_uid)) {
            continue;
        }
        if ((previous_bss.bss_template_ref != bss_info.bss_template_ref) ||
            (previous_bss.bss_index == 0)) {
            continue;
        }
        if (used_bss_indexes.insert(previous_bss.bss_index).second) {
            return previous_bss.bss_index;
        }
    }

    for (unsigned candidate = 1; candidate <= std::numeric_limits<uint8_t>::max(); candidate++) {
        auto candidate_index = static_cast<uint8_t>(candidate);
        if (used_bss_indexes.insert(candidate_index).second) {
            return candidate_index;
        }
    }
    return 0;
}

struct sWifiGenToken {
    uint32_t generation = 0;
    bool or_higher      = false;
};

static bool template_trim_parse_wifi_gen_token(std::string token, bool allow_plus,
                                               sWifiGenToken &out)
{
    auto trim = [](std::string &s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
    };

    trim(token);
    if (token.empty()) {
        return false;
    }

    out.or_higher = false;
    if (!token.empty() && token.back() == '+') {
        if (!allow_plus) {
            return false;
        }
        out.or_higher = true;
        token.pop_back();
        trim(token);
    }

    if (token.empty() ||
        !std::all_of(token.begin(), token.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }

    unsigned long v = std::strtoul(token.c_str(), nullptr, 10);
    if (v == 0UL || v > 255UL) {
        return false;
    }
    out.generation = static_cast<uint32_t>(v);
    return true;
}

static bool template_parse_wifi_gen_csv(const std::string &csv, bool allow_plus,
                                         std::vector<sWifiGenToken> &out)
{
    out.clear();
    for (const auto &piece : parse_topology_flags(csv)) {
        sWifiGenToken tok;
        if (!template_trim_parse_wifi_gen_token(piece, allow_plus, tok)) {
            LOG(WARNING) << "Invalid Wi-Fi generation token '" << piece << "'";
            return false;
        }
        out.push_back(tok);
    }
    return true;
}

/** TMN / BBF: per-radio PHY support for Wi-Fi generation integer N (1..255). */
static bool template_radio_phy_supports_generation_number(const Agent::sRadio &radio, uint32_t gen)
{
    if (radio.supported_channels.empty()) {
        return true;
    }

    return radio.max_wifi_generation_supported >= gen;
}

/** TMN MLO rule: selected RadioTemplate OperatingGeneration includes Wi-Fi 7 (or 6+ spanning 7). */
static bool radio_template_includes_wifi7(amxd_object_t *radio_inst_dm)
{
    if (!radio_inst_dm) {
        return false;
    }
    const std::string csv = get_param_string(radio_inst_dm, "OperatingGeneration");
    if (csv.empty()) {
        return false;
    }
    std::vector<sWifiGenToken> toks;
    if (!template_parse_wifi_gen_csv(csv, true, toks)) {
        return false;
    }
    for (const auto &t : toks) {
        const bool spans_wifi7 =
            (t.generation >= 7U) || (t.or_higher && t.generation >= 6U);
        if (spans_wifi7) {
            return true;
        }
    }
    return false;
}

static bool template_radio_template_generation_matches(const Agent::sRadio &radio,
                                                       const sRadioTemplateRow &cand)
{
    if (cand.operating_generation.empty() && cand.supported_generation_flag_csv.empty()) {
        return true;
    }

    const auto check_tokens = [&](const std::vector<sWifiGenToken> &toks) -> bool {
        for (const auto &t : toks) {
            if (!template_radio_phy_supports_generation_number(radio, t.generation)) {
                return false;
            }
        }
        return true;
    };

    if (!cand.supported_generation_flag_csv.empty()) {
        std::vector<sWifiGenToken> toks;
        if (!template_parse_wifi_gen_csv(cand.supported_generation_flag_csv, false, toks)) {
            return false;
        }
        if (!check_tokens(toks)) {
            return false;
        }
    }

    if (!cand.operating_generation.empty()) {
        std::vector<sWifiGenToken> toks;
        if (!template_parse_wifi_gen_csv(cand.operating_generation, true, toks)) {
            return false;
        }
        if (!check_tokens(toks)) {
            return false;
        }
    }

    return true;
}

static sRadioTemplateRow template_select_radio_template(const Agent &agent, const Agent::sRadio &radio,
                                                        const std::vector<sRadioTemplateRow> &candidates)
{
    sRadioTemplateRow selected{};
    for (const auto &cand : candidates) {
        if (!cand.radio_inst || cand.operating_class.empty()) {
            continue;
        }
        if (!template_radio_matches_operating_classes(radio, cand.operating_class)) {
            continue;
        }
        if (!template_radio_bandflag_satisfies_unii_multi_radio_rule(agent, radio.get_band(),
                                                                     cand.band_flag_csv)) {
            continue;
        }
        if (!template_tri_state_satisfied(
                cand.dfs_support_flag,
                template_operating_classes_include_5g(cand.operating_class) &&
                    template_radio_agent_supports_dfs(radio))) {
            continue;
        }
        if (!template_tri_state_satisfied(
                cand.afc_support_flag,
                template_operating_classes_include_6g(cand.operating_class) &&
                    template_radio_agent_supports_afc(radio))) {
            continue;
        }
        if (!template_radio_template_generation_matches(radio, cand)) {
            continue;
        }
        if (!selected.radio_inst) {
            selected = cand;
            continue;
        }
        if (cand.priority > selected.priority) {
            selected = cand;
        } else if (cand.priority == selected.priority) {
            const std::string cand_id = get_param_string(cand.radio_inst, "RadioTemplateID");
            const std::string sel_id  = get_param_string(selected.radio_inst, "RadioTemplateID");
            if (cand_id < sel_id) {
                selected = cand;
            } else if (cand_id == sel_id &&
                       amxd_object_get_index(cand.radio_inst) <
                           amxd_object_get_index(selected.radio_inst)) {
                selected = cand;
            }
        }
    }
    return selected;
}

static bool template_stage_bss_on_radio(
    amxd_object_t *templates_root, amxd_object_t *network_obj,
    const std::vector<std::shared_ptr<Agent>> &connected_agents, const std::shared_ptr<Agent> &agent,
    const sMacAddr &radio_uid, amxd_object_t *radio_inst_dm,
    const std::list<uint8_t> &radio_operating_classes, amxd_object_t *bss_template_obj,
    const std::vector<std::string> &network_topology_flags, const std::vector<sMacAddr> &network_alids,
    sAgentBssIndexState &selection_state,
    const std::list<son::wireless_utils::sBssInfoConf> &previous_for_agent)
{
    if (!g_database || !templates_root || !network_obj || !agent || !radio_inst_dm ||
        !bss_template_obj) {
        return false;
    }

    const uint32_t bss_instance_index = amxd_object_get_index(bss_template_obj);

    std::string network_primary_ssc_ref = get_param_string(network_obj, "PrimarySSCTemplateReference");

    std::string bss_ssid = get_param_string(bss_template_obj, "SSID");
    if (bss_ssid.empty()) {
        return false;
    }

    std::string bss_key_passphrase     = get_param_string(bss_template_obj, "KeyPassphrase");
    bool bss_advertisement_enable      = get_param_bool(bss_template_obj, "AdvertisementEnable");
    std::string bss_topology_flag_str  = get_param_string(bss_template_obj, "TopologyFlag");
    std::string bss_ieee1905_alid_str   = get_param_string(bss_template_obj, "IEEE1905ALID");
    std::string bss_radio_template_ref = get_param_string(bss_template_obj, "RadioTemplateReference");
    std::string bss_ssc_template_ref   = get_param_string(bss_template_obj, "SSCTemplateReference");
    std::string bss_linked_ssc_id      = get_param_string(bss_template_obj, "LinkedSSCTemplateID");
    std::string bss_linked_apmld_id = get_param_string(bss_template_obj, "LinkedAPMLDTemplateID");
    std::string bss_linked_radio_id = get_param_string(bss_template_obj, "LinkedRadioTemplateID");
    std::string bss_security_group_ref = get_param_string(bss_template_obj, "SecurityGroupReference");
    std::string bss_apmld_ref          = get_param_string(bss_template_obj, "APMLDTemplateReference");

    if (bss_radio_template_ref.empty() || bss_ssc_template_ref.empty() ||
        bss_security_group_ref.empty()) {
        return false;
    }

    if (bss_linked_radio_id.empty() || bss_linked_ssc_id.empty()) {
        LOG(DEBUG) << "BSSTemplate[" << bss_instance_index
                   << "] skip: empty LinkedRadioTemplateID or LinkedSSCTemplateID";
        return false;
    }

    if (!bss_apmld_ref.empty() && bss_linked_apmld_id.empty()) {
        LOG(DEBUG) << "BSSTemplate[" << bss_instance_index
                   << "] skip: APMLDTemplateReference set but LinkedAPMLDTemplateID empty";
        return false;
    }

    amxd_object_t *resolved_radio =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s",
                      bss_radio_template_ref.c_str());
    if (resolved_radio != radio_inst_dm) {
        return false;
    }

    if (!network_primary_ssc_ref.empty()) {
        amxd_object_t *pri_ssc =
            amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s",
                          network_primary_ssc_ref.c_str());
        if (!pri_ssc) {
            LOG(WARNING) << "Network.PrimarySSCTemplateReference not found: "
                         << network_primary_ssc_ref;
            return false;
        }
        const std::string primary_id = get_param_string(pri_ssc, "SSCTemplateID");
        if (primary_id.empty()) {
            LOG(WARNING) << "Network.PrimarySSCTemplateReference has empty SSCTemplateID";
            return false;
        }
        if (primary_id != bss_linked_ssc_id) {
            LOG(WARNING) << "BSSTemplate SSC does not match Network.PrimarySSCTemplateReference";
            return false;
        }
        if (bss_ssc_template_ref != network_primary_ssc_ref) {
            LOG(DEBUG) << "BSSTemplate[" << bss_instance_index
                       << "] skip: SSCTemplateReference != Network.PrimarySSCTemplateReference";
            return false;
        }
    }

    std::vector<std::string> bss_topology_flags = parse_topology_flags(bss_topology_flag_str);

    std::vector<sMacAddr> bss_alids;
    template_parse_alid_csv(bss_ieee1905_alid_str, bss_alids);

    auto target_agents = template_filter_target_agents(connected_agents, network_topology_flags, network_alids,
                                               bss_topology_flags, bss_alids);
    if (std::find(target_agents.begin(), target_agents.end(), agent->al_mac) == target_agents.end()) {
        return false;
    }

    const uint32_t max_wlan_hop = get_param_uint32(bss_template_obj, "MaxWLANHopLimitFlag");
    if (max_wlan_hop > 0U) {
        const uint32_t agent_hops = template_agent_wlan_hop_count(*agent);
        if (agent_hops > max_wlan_hop) {
            LOG(DEBUG) << "BSSTemplate[" << bss_instance_index << "] skip: agent hop count "
                       << agent_hops << " > MaxWLANHopLimitFlag " << max_wlan_hop;
            return false;
        }
    }

    const eTemplateTriState secure_equipment_flag =
        template_parse_tri_state_flag(get_param_string(bss_template_obj, "SecureEquipmentFlag"));
    if (!template_tri_state_satisfied(secure_equipment_flag,
                                      template_agent_is_secure_equipment(*agent))) {
        LOG(DEBUG) << "BSSTemplate[" << bss_instance_index
                   << "] skip: SecureEquipmentFlag not satisfied for agent " << agent->al_mac;
        return false;
    }

    son::wireless_utils::sBssInfoConf bss_info{};
    bss_info.ssid             = bss_ssid;
    bss_info.network_key      = bss_key_passphrase;
    bss_info.hidden_ssid      = bss_advertisement_enable ? WSC::eWscVendorExtHiddenSsid::DISABLED
                                                         : WSC::eWscVendorExtHiddenSsid::ENABLED;
    bss_info.operating_class  = radio_operating_classes;
    bss_info.target_radio_uid = radio_uid;
    bss_info.radio_template_ref = template_tr181_row_path("RadioTemplate", radio_inst_dm);
    bss_info.bss_template_ref   = template_tr181_row_path("BSSTemplate", bss_template_obj);

    amxd_object_t *security_group_obj =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s",
                      bss_security_group_ref.c_str());
    if (!security_group_obj) {
        LOG(WARNING) << "SecurityGroup not found: " << bss_security_group_ref;
        return false;
    }

    amxd_object_t *security_inst =
        template_resolve_security_template(templates_root, security_group_obj);
    if (!security_inst) {
        LOG(WARNING) << "No enabled SecurityTemplate for BSSTemplate[" << bss_instance_index << "]";
        return false;
    }

    amxd_object_t *ssc_inst =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s", bss_ssc_template_ref.c_str());
    if (!ssc_inst || !get_param_bool(ssc_inst, "SSCEnable")) {
        LOG(WARNING) << "SSCTemplate missing or SSCEnable=false: " << bss_ssc_template_ref;
        return false;
    }

    std::string haul_type  = get_param_string(ssc_inst, "HaulType");
    bss_info.backhaul     = (haul_type.find("Backhaul") != std::string::npos);
    bss_info.fronthaul    = (haul_type.find("Fronthaul") != std::string::npos);

    if (!bss_info.backhaul && !bss_info.fronthaul) {
        bss_info.fronthaul = true; /* fail-safe default before AKM matching */
    }

    {
        const std::string sec_gen_csv = get_param_string(security_inst, "OperatingGenerationFlag");
        if (!sec_gen_csv.empty()) {
            std::vector<sWifiGenToken> required;
            if (!template_parse_wifi_gen_csv(sec_gen_csv, false, required)) {
                LOG(WARNING) << "SecurityTemplate OperatingGenerationFlag invalid for BSSTemplate["
                             << bss_instance_index << "]";
                return false;
            }

            const std::string radio_gen_csv = get_param_string(radio_inst_dm, "OperatingGeneration");
            std::vector<sWifiGenToken> offered;
            if (!radio_gen_csv.empty()) {
                if (!template_parse_wifi_gen_csv(radio_gen_csv, true, offered)) {
                    LOG(WARNING) << "RadioTemplate OperatingGeneration invalid while matching security";
                    return false;
                }
            }

            auto gen_contains = [](const sWifiGenToken &container, const sWifiGenToken &need) -> bool {
                if (!need.or_higher) {
                    if (container.or_higher) {
                        return container.generation <= need.generation;
                    }
                    return container.generation == need.generation;
                }
                if (!container.or_higher) {
                    return false;
                }
                return container.generation <= need.generation;
            };

            for (const auto &need : required) {
                bool ok = false;
                for (const auto &have : offered) {
                    if (gen_contains(have, need)) {
                        ok = true;
                        break;
                    }
                }
                if (!ok) {
                    LOG(DEBUG) << "SecurityTemplate OperatingGenerationFlag not satisfied by "
                                  "RadioTemplate.OperatingGeneration (BSSTemplate["
                               << bss_instance_index << "])";
                    return false;
                }
            }
            auto phy = agent->radios.get(radio_uid);
            if (!phy) {
                LOG(WARNING) << "No radio object for RUID while matching OperatingGenerationFlag";
                return false;
            }
            for (const auto &need : required) {
                if (!template_radio_phy_supports_generation_number(*phy, need.generation)) {
                    LOG(DEBUG) << "Security OperatingGenerationFlag requires gen " << need.generation
                               << " not supported on radio " << radio_uid;
                    return false;
                }
            }
        }
    }

    if (!template_apply_security_to_bss_info(security_inst, agent.get(), bss_info,
                                               radio_operating_classes)) {
        LOG(WARNING) << "SecurityTemplate not applicable for BSSTemplate[" << bss_instance_index
                     << "] SSID \"" << bss_ssid << "\"";
        return false;
    }

    const uint32_t ssc_vid = get_param_uint32(ssc_inst, "VID");
    if (ssc_vid > 0U && ssc_vid <= 4095U) {
        const uint16_t vid = static_cast<uint16_t>(ssc_vid);

        son::wireless_utils::sTrafficSeparationSsid ts;
        ts.ssid    = bss_ssid;
        ts.vlan_id = vid;
        g_database->add_traffic_separation_configuration(agent->al_mac, ts);
    }

    bss_info.vap_type = wireless_utils::string_to_vap_type(
        get_param_string(bss_template_obj, "X_PRPLWARE-COM_VapType"));
    bss_info.vap_label = get_param_string(bss_template_obj, "X_PRPLWARE-COM_VapLabel");

    if (bss_info.vap_type == eVapType::OTHER &&
        bss_info.backhaul && !bss_info.fronthaul) {
        bss_info.vap_type = eVapType::BACKHAUL;
    }

    if (!bss_apmld_ref.empty()) {
        amxd_object_t *apmld_inst =
            amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s", bss_apmld_ref.c_str());
        if (apmld_inst && get_param_bool(apmld_inst, "MLOEnable")) {
            const std::string mld_key = get_param_string(apmld_inst, "APMLDTemplateID");
            auto phy = agent->radios.get(radio_uid);
            if (!phy || agent->max_num_mlds == 0 || !phy->eht_supported ||
                !radio_template_includes_wifi7(radio_inst_dm)) {
                LOG(WARNING) << "BSSTemplate[" << bss_instance_index
                             << "] MLOEnable requires MLO-capable agent, Wi-Fi 7 in RadioTemplate."
                                "OperatingGeneration, and supported radio.";
                return false;
            }
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
        } else if (apmld_inst && !get_param_bool(apmld_inst, "MLOEnable")) {
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
                LOG(DEBUG) << "BSSTemplate[" << bss_instance_index
                           << "] APMLD single-affiliate MLD (MLOEnable=false) " << mld_key;
            }
        }
    }

    if (bss_info.authentication_type != WSC::eWscAuth::WSC_AUTH_OPEN &&
        bss_info.network_key.empty()) {
        LOG(WARNING) << "BSSTemplate " << bss_ssid << " missing KeyPassphrase for selected security";
    }

    son::wireless_utils::sBssInfoConf per_bss = bss_info;

    auto agent_supports_wpa3_pcm = [&](const Agent &a) -> bool {
        // Fail closed: require explicit capability vectors.
        if (!a.security_capabilities.valid_akm_suites || !a.security_capabilities.valid_cipher_suites) {
            return false;
        }

        const std::bitset<256> fh_or_bh_akm =
            a.security_capabilities.fronthaul_akm_suite_types |
            a.security_capabilities.backhaul_akm_suite_types;
        const bool supports_psk = fh_or_bh_akm.test(0x02);
        const bool supports_sae =
            fh_or_bh_akm.test(0x08) || fh_or_bh_akm.test(0x18);
        const bool supports_ccmp = a.security_capabilities.cipher_suite_types.test(0x04);
        const bool supports_bip  = a.security_capabilities.cipher_suite_types.test(0x06);

        bool has_non_6g = false;
        for (uint8_t oc : per_bss.operating_class) {
            const bool is_6g = (oc >= OPCLASS_6GHZ_USING_CENTER_CHANNEL_FIRST &&
                            oc <= OPCLASS_6GHZ_USING_CENTER_CHANNEL_LAST &&
                            oc != OPCLASS_6GHZ_EXCEPTION);
            if (!is_6g) {
                has_non_6g = true;
                break;
            }
        }
        return supports_sae && supports_ccmp && supports_bip && (!has_non_6g || supports_psk);
    };

    const bool wants_rsn_override =
        per_bss.authentication_type == WSC::eWscAuth::WSC_AUTH_RSN &&
        (!per_bss.rsn_security_ies.empty() ||
         per_bss.additional_auth == son::wireless_utils::eAdditionalAuth::WPA3_PERSONAL_COMPATIBILITY);

    const bool capability_blocks_rsn_override =
        wants_rsn_override && agent->rsn_overriding_supported && !agent_supports_wpa3_pcm(*agent);

    if ((wants_rsn_override && !agent->rsn_overriding_supported) || capability_blocks_rsn_override) {
        bool any_6g = false;
        for (uint8_t oc : per_bss.operating_class) {
            if (oc >= OPCLASS_6GHZ_USING_CENTER_CHANNEL_FIRST &&
                oc <= OPCLASS_6GHZ_USING_CENTER_CHANNEL_LAST && oc != OPCLASS_6GHZ_EXCEPTION) {
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
        per_bss.rsn_security_ies.clear();
        LOG(DEBUG) << "Agent " << agent->al_mac << " lacks RSN override; security downgraded";
    }

    per_bss.bss_index = template_allocate_stable_bss_index(agent->al_mac, radio_uid,
                                                            previous_for_agent, per_bss,
                                                            selection_state);
    if (per_bss.bss_index == 0) {
        LOG(WARNING) << "Unable to allocate BSS_Index for agent " << agent->al_mac << " radio "
                     << radio_uid << " SSID \"" << bss_ssid << "\"";
        return false;
    }

    g_database->add_bss_info_configuration(agent->al_mac, per_bss);
    LOG(DEBUG) << "Staged BSS for agent " << agent->al_mac << " radio " << radio_uid << " SSID \""
               << bss_ssid << "\"";
    return true;
}

static bool template_bss_staging_lists_equal(
    const std::list<son::wireless_utils::sBssInfoConf> &before,
    const std::list<son::wireless_utils::sBssInfoConf> &after)
{
    if (before.size() != after.size()) {
        return false;
    }

    auto it_before = before.begin();
    auto it_after  = after.begin();
    for (; it_before != before.end(); ++it_before, ++it_after) {
        if (it_before->ssid != it_after->ssid || it_before->bssid != it_after->bssid ||
            it_before->bss_index != it_after->bss_index ||
            it_before->target_radio_uid != it_after->target_radio_uid ||
            it_before->fronthaul != it_after->fronthaul ||
            it_before->backhaul != it_after->backhaul ||
            it_before->teardown != it_after->teardown ||
            it_before->authentication_type != it_after->authentication_type ||
            it_before->encryption_type != it_after->encryption_type ||
            it_before->network_key != it_after->network_key ||
            it_before->hidden_ssid != it_after->hidden_ssid ||
            it_before->additional_auth != it_after->additional_auth ||
            it_before->rsn_security_ies != it_after->rsn_security_ies ||
            it_before->mld_id != it_after->mld_id ||
            it_before->vap_type != it_after->vap_type) {
            return false;
        }
    }
    return true;
}

/** Rebuild staged BSS/MLD from templates DM (radio-first, per-radio BSS_Index). */
static bool template_rebuild_staged_configuration(amxd_object_t *templates_root)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return false;
    }
    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(DEBUG) << "template_rebuild_staged_configuration failed: UseDataElementsVapConfigs is false";
        return false;
    }
    if (!templates_root) {
        LOG(ERROR) << "templates_root is null";
        return false;
    }

    amxd_object_t *network_obj = amxd_object_get_child(templates_root, "Network");
    if (!network_obj) {
        LOG(ERROR) << "Network object not found under Templates root";
        return false;
    }

    std::unordered_map<std::string, std::string> apmld_ssid_by_id;
    if (!get_param_bool(network_obj, "Enable")) {
        std::vector<sMacAddr> changed_agents;
        for (const auto &agent : g_database->get_all_connected_agents()) {
            if (agent && !g_database->get_bss_info_configuration(agent->al_mac).empty()) {
                changed_agents.push_back(agent->al_mac);
            }
        }
        g_database->clear_bss_info_configuration();
        g_database->clear_traffic_separation_configurations();
        g_database->clear_mld_info_configuration();
        if (!changed_agents.empty()) {
            LOG(INFO) << "wifi templates: Network.Enable=false, unicast renew to "
                      << changed_agents.size() << " agent(s) with cleared staging";
            template_send_ap_config_renew_message(changed_agents);
        } else {
            LOG(DEBUG) << "Templates.Network.Enable=false: cleared staged BSS/MLD (no AP config renew)";
        }
        template_apply_pending_radio_transmit_power_limits({});
        return true;
    }

    amxd_object_t *bss_table = amxd_object_get_child(templates_root, "BSSTemplate");
    if (!bss_table) {
        LOG(ERROR) << "BSSTemplate table not found";
        return false;
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
                  if (get_param_uint32(a, "Priority") != get_param_uint32(b, "Priority")) {
                      return get_param_uint32(a, "Priority") > get_param_uint32(b, "Priority");
                  }
                  return amxd_object_get_index(a) < amxd_object_get_index(b);
              });

    std::string network_topology_flag_str = get_param_string(network_obj, "TopologyFlag");
    std::string network_ieee1905_alid_str  = get_param_string(network_obj, "IEEE1905ALID");
    std::vector<std::string> network_topology_flags = parse_topology_flags(network_topology_flag_str);

    std::vector<sMacAddr> network_alids;
    template_parse_alid_csv(network_ieee1905_alid_str, network_alids);

    std::vector<sRadioTemplateRow> radio_candidates;
    amxd_object_t *radio_table = amxd_object_get_child(templates_root, "RadioTemplate");
    if (radio_table) {
        amxd_object_for_each(instance, rit, radio_table)
        {
            amxd_object_t *radio_inst = amxc_llist_it_get_data(rit, amxd_object_t, it);
            if (!radio_inst || !get_param_bool(radio_inst, "Enable")) {
                continue;
            }
            sRadioTemplateRow row;
            row.radio_inst = radio_inst;
            row.priority   = get_param_uint32(radio_inst, "Priority");
            son::wireless_utils::sBssInfoConf tmp;
            if (!template_load_radio_operating_classes(radio_inst, tmp) ||
                tmp.operating_class.empty()) {
                continue;
            }
            row.operating_class = std::move(tmp.operating_class);

            row.operating_generation          = get_param_string(radio_inst, "OperatingGeneration");
            row.supported_generation_flag_csv = get_param_string(radio_inst, "SupportedGenerationFlag");
            row.band_flag_csv = get_param_string(radio_inst, "BandFlag");
            row.dfs_support_flag =
                template_parse_tri_state_flag(get_param_string(radio_inst, "DFSSupportFlag"));
            row.afc_support_flag =
                template_parse_tri_state_flag(get_param_string(radio_inst, "AFCSupportFlag"));

            amxd_status_t tpl_tx_status = amxd_status_ok;
            int32_t tpl_tx = amxd_object_get_int32_t(radio_inst, "TransmitPowerLimit", &tpl_tx_status);
            if (tpl_tx_status == amxd_status_ok && tpl_tx != 0) {
                if (tpl_tx < -128) {
                    tpl_tx = -128;
                } else if (tpl_tx > 127) {
                    tpl_tx = 127;
                }
                row.transmit_power_limit_dbm   = static_cast<int8_t>(tpl_tx);
                row.has_transmit_power_limit   = true;
            }

            radio_candidates.push_back(std::move(row));
        }
    }

    const auto connected_agents = g_database->get_all_connected_agents();

    std::unordered_map<std::string, std::list<son::wireless_utils::sBssInfoConf>>
        staging_before_by_agent;
    std::vector<std::tuple<sMacAddr, sMacAddr, int8_t>> template_radio_tx_power_applies;
    for (const auto &agent : connected_agents) {
        if (!agent) {
            continue;
        }
        staging_before_by_agent.emplace(tlvf::mac_to_string(agent->al_mac),
                                        g_database->get_bss_info_configuration(agent->al_mac));
    }

    g_database->clear_bss_info_configuration();
    g_database->clear_traffic_separation_configurations();
    g_database->clear_mld_info_configuration();

    const auto agents_in_network_scope = template_filter_target_agents(
        connected_agents, network_topology_flags, network_alids, {}, {});

    for (const auto &agent : connected_agents) {
        if (!agent) {
            continue;
        }
        if (std::find(agents_in_network_scope.begin(), agents_in_network_scope.end(),
                      agent->al_mac) == agents_in_network_scope.end()) {
            continue;
        }

        std::vector<std::pair<sMacAddr, std::shared_ptr<Agent::sRadio>>> radios_sorted;
        for (const auto &kv : agent->radios) {
            if (kv.second) {
                radios_sorted.emplace_back(kv.first, kv.second);
            }
        }
        std::sort(radios_sorted.begin(), radios_sorted.end(), [](const auto &a, const auto &b) {
            return tlvf::mac_to_string(a.first) < tlvf::mac_to_string(b.first);
        });

        std::vector<std::pair<sMacAddr, sRadioTemplateRow>> selected_per_radio;
        std::set<amxd_object_t *> used_radio_template_rows;

        for (const auto &pr : radios_sorted) {
            const auto &ruid = pr.first;
            const auto &radio = pr.second;
            if (!radio) {
                continue;
            }
            std::vector<sRadioTemplateRow> filtered_candidates;
            filtered_candidates.reserve(radio_candidates.size());
            for (const auto &cand : radio_candidates) {
                if (cand.radio_inst && !used_radio_template_rows.count(cand.radio_inst)) {
                    filtered_candidates.push_back(cand);
                }
            }
            auto row = template_select_radio_template(*agent, *radio, filtered_candidates);
            if (row.radio_inst) {
                selected_per_radio.emplace_back(ruid, row);
                used_radio_template_rows.insert(row.radio_inst);
            }
        }
        std::sort(selected_per_radio.begin(), selected_per_radio.end(),
                  [](const auto &a, const auto &b) {
                      return tlvf::mac_to_string(a.first) < tlvf::mac_to_string(b.first);
                  });

        sAgentBssIndexState selection_state;
        const auto agent_key = tlvf::mac_to_string(agent->al_mac);
        const auto &previous = staging_before_by_agent[agent_key];
        std::set<amxd_object_t *> bss_template_used_on_agent;

        for (const auto &radio_sel : selected_per_radio) {
            const auto &ruid      = radio_sel.first;
            const auto &radio_row = radio_sel.second;

            if (radio_row.has_transmit_power_limit) {
                template_radio_tx_power_applies.emplace_back(agent->al_mac, ruid,
                                                             radio_row.transmit_power_limit_dbm);
            }
            auto phy_radio = agent->radios.get(ruid);
            const uint32_t max_bss =
                (phy_radio && phy_radio->maximum_number_of_bsss_supported > 0)
                    ? phy_radio->maximum_number_of_bsss_supported
                    : std::numeric_limits<uint32_t>::max();
            uint32_t staged_on_radio = 0;

            for (amxd_object_t *bss_inst : bss_to_commit) {
                if (staged_on_radio >= max_bss) {
                    break;
                }
                if (bss_template_used_on_agent.count(bss_inst)) {
                    continue;
                }
                const std::string bss_ssid_check = get_param_string(bss_inst, "SSID");
                if (!template_track_apmld_ssid_consistency(bss_inst, bss_ssid_check,
                                                             apmld_ssid_by_id)) {
                    continue;
                }
                if (template_stage_bss_on_radio(templates_root, network_obj, connected_agents, agent,
                                                ruid, radio_row.radio_inst, radio_row.operating_class,
                                                bss_inst, network_topology_flags, network_alids,
                                                selection_state, previous)) {
                    bss_template_used_on_agent.insert(bss_inst);
                    ++staged_on_radio;
                }
            }
        }
    }

    template_apply_pending_radio_transmit_power_limits(template_radio_tx_power_applies);

    std::vector<sMacAddr> changed_agents;
    for (const auto &agent : connected_agents) {
        if (!agent) {
            continue;
        }
        const auto agent_key = tlvf::mac_to_string(agent->al_mac);
        const auto &before   = staging_before_by_agent[agent_key];
        const auto after     = g_database->get_bss_info_configuration(agent->al_mac);
        if (!template_bss_staging_lists_equal(before, after)) {
            changed_agents.push_back(agent->al_mac);
        }
    }

    if (!changed_agents.empty()) {
        LOG(INFO) << "wifi templates: staging changed, unicast renew to " << changed_agents.size()
                  << " agent(s)";
        template_send_ap_config_renew_message(changed_agents);
    } else {
        LOG(INFO) << "wifi templates: staging unchanged, skip AP renew";
    }
    return true;
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
            bss_info.vap_label = get_param_string(access_point_inst, "X_PRPLWARE-COM_VapLabel");

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
/** First existing master conf: /tmp override, then writable, then install (matches read_config_file). */
static std::string resolve_master_config_file_path()
{
    const std::string name = std::string(BEEROCKS_CONTROLLER) + ".conf";
    const std::array<std::string, 3> candidates = {{
        std::string("/tmp/") + name,
        std::string(CONF_FILES_WRITABLE_PATH) + name,
        mapf::utils::get_install_path() + "config/" + name,
    }};

    for (const auto &path : candidates) {
        if (std::ifstream(path).good()) {
            return path;
        }
    }
    return candidates[1];
}

static void event_use_dataelements_vap_config_changed(const char *const sig_name, const amxc_var_t *const data, void *const priv)
{
    if (!is_templates_dm_initialized()) {
        LOG(DEBUG) << "Ignoring startup event";
        return;
    }

    auto *network = amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    if (!network) {
        LOG(ERROR) << "Failed obtaining object for UseDataElementsVapConfigs";
        return;
    }

    const bool enabled = amxd_object_get_bool(network, "UseDataElementsVapConfigs", nullptr);
    const char *value  = enabled ? "1" : "0";
    LOG(INFO) << "use_dataelements_vap_configs=" << enabled;

    const auto active_path = resolve_master_config_file_path();
    if (!beerocks::config_file::update_section_key(active_path, "controller",
                                                   "use_dataelements_vap_configs", value)) {
        LOG(ERROR) << "Failed persisting UseDataElementsVapConfigs into " << active_path;
        return;
    }

    // Keep durable writable copy in sync when the active file is the /tmp override.
    const auto writable_path =
        std::string(CONF_FILES_WRITABLE_PATH) + BEEROCKS_CONTROLLER + ".conf";
    if (active_path.rfind("/tmp/", 0) == 0 && active_path != writable_path &&
        std::ifstream(writable_path).good()) {
        if (!beerocks::config_file::update_section_key(writable_path, "controller",
                                                       "use_dataelements_vap_configs", value)) {
            LOG(WARNING) << "Updated /tmp conf but failed updating writable " << writable_path;
        }
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
        {"event_network_enable_changed", event_network_enable_changed},
        {"event_use_dataelements_vap_config_changed", event_use_dataelements_vap_config_changed},
        {"event_templates_network_configuration_changed",
         event_templates_network_configuration_changed},
        {"event_bss_template_configuration_changed", event_bss_template_configuration_changed},
        {"event_bss_template_instance_changed", event_bss_template_instance_changed},
        {"event_radio_template_configuration_changed", event_radio_template_configuration_changed},
        {"event_radio_template_instance_changed", event_radio_template_instance_changed},
        {"event_ssc_template_configuration_changed", event_ssc_template_configuration_changed},
        {"event_ssc_template_instance_changed", event_ssc_template_instance_changed},
        {"event_security_template_configuration_changed", event_security_template_configuration_changed},
        {"event_security_template_instance_changed", event_security_template_instance_changed},
        {"event_templates_security_group_configuration_changed", event_templates_security_group_configuration_changed},
        {"event_templates_security_group_instance_changed", event_templates_security_group_instance_changed},
        {"event_apmld_template_configuration_changed", event_apmld_template_configuration_changed},
        {"event_apmld_template_instance_changed", event_apmld_template_instance_changed}};
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

static void templates_commit(void)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return;
    }
    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(ERROR) << "wifi templates: Rebuild Skipped";
        return;
    }
    amxd_object_t *templates_root =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s", "Device.WiFi.Templates");
    if (!templates_root) {
        LOG(WARNING) << "wifi templates: DM root not found (Device.WiFi.Templates)";
        return;
    }
    template_sync_all_linked_ids(templates_root);
    template_rebuild_staged_configuration(templates_root);
}

void templates_commit_request(void)
{
    g_templates_commit_pending = true;
}

void templates_commit_apply_pending(void)
{
    if (g_templates_apply_in_progress) {
        return;
    }
    if (!g_templates_commit_pending && !g_templates_topology_restage_armed) {
        return;
    }

    g_templates_commit_pending         = false;
    g_templates_topology_restage_armed = false;

    LOG(INFO) << "wifi templates: apply_pending";
    g_templates_apply_in_progress = true;
    templates_commit();
    g_templates_apply_in_progress = false;

    if (g_templates_commit_pending || g_templates_topology_restage_armed) {
        templates_schedule_commit_apply();
    }
}

void templates_schedule_commit_apply(void)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return;
    }
    auto controller = g_database->get_controller_ctx();
    if (!controller) {
        LOG(ERROR) << "wifi templates: no controller ctx, apply deferred";
        return;
    }
    controller->schedule_templates_commit_apply();
}

void templates_restage_only(void)
{
    if (!g_database) {
        LOG(ERROR) << "g_database is nullptr";
        return;
    }
    if (!g_database->config.use_dataelements_vap_configs) {
        LOG(ERROR) << "wifi templates: restage skipped (use_dataelements_vap_configs is false)";
        return;
    }

    if (g_templates_topology_restage_armed) {
        LOG(ERROR) << "wifi templates: topology restage armed already, coalescing";
        return;
    }
    g_templates_topology_restage_armed = true;
    templates_commit_request();
}

bool is_templates_dm_initialized()
{
    return g_templates_dm_initialized;
}

void set_templates_dm_initialized(bool initialized)
{
    g_templates_dm_initialized = initialized;
}

} // namespace actions
} // namespace controller
} // namespace prplmesh
