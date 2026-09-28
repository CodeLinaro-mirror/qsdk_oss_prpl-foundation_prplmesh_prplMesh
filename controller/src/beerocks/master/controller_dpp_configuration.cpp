/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "controller_dpp_configuration.h"

#include "controller_dpp_csign.h"
#include "db/agent.h"

#include <bcl/beerocks_string_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <easylogging++.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace son {
namespace controller_dpp {
namespace {

constexpr char k_dpp_controller_keys_section[] = "dpp_controller_keys";

bool hex_string_to_plain_string(const std::string &hex, std::string &plain)
{
    plain.clear();
    if (hex.empty()) {
        return true;
    }
    if ((hex.size() % 2) != 0 ||
        !std::all_of(hex.begin(), hex.end(),
                     [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        return false;
    }
    plain = beerocks::string_utils::hex_to_bytes<std::string>(hex);
    return true;
}

std::string plain_string_to_hex(const std::string &plain)
{
    return beerocks::string_utils::bytes_to_hex_string(
        reinterpret_cast<const uint8_t *>(plain.data()), plain.size());
}

std::string dpp_akm_from_bss_info(const wireless_utils::sBssInfoConf &bss_info)
{
    switch (bss_info.authentication_type) {
    case WSC::eWscAuth::WSC_AUTH_OPEN:
        return "open";
    case WSC::eWscAuth::WSC_AUTH_WPA2PSK:
    case WSC::eWscAuth::WSC_AUTH_WPAPSK:
        return "psk";
    case WSC::eWscAuth::WSC_AUTH_SAE:
    case WSC::eWscAuth::WSC_AUTH_SAE_AKM24:
        return "sae";
    case WSC::eWscAuth::WSC_AUTH_RSN:
        if (bss_info.additional_auth ==
            wireless_utils::eAdditionalAuth::WPA3_PERSONAL_COMPATIBILITY) {
            return "psk+sae";
        }
        break;
    default:
        break;
    }
    return {};
}

std::vector<std::string> split_dpp_akm_tokens(const std::string &akm)
{
    std::vector<std::string> tokens;
    if (akm.empty()) {
        return tokens;
    }
    std::stringstream ss(akm);
    std::string token;
    while (std::getline(ss, token, '+')) {
        if (token.empty()) {
            continue;
        }
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        tokens.push_back(token);
    }
    return tokens;
}

bool dpp_akm_is_compatible(const std::string &configured_akm, const std::string &requested_akm)
{
    if (configured_akm.empty() || requested_akm.empty()) {
        return true;
    }
    const auto configured_tokens = split_dpp_akm_tokens(configured_akm);
    const auto requested_tokens  = split_dpp_akm_tokens(requested_akm);
    if (configured_tokens.empty() || requested_tokens.empty()) {
        return beerocks::string_utils::case_insensitive_compare(configured_akm, requested_akm);
    }
    for (const auto &configured : configured_tokens) {
        if (std::find(requested_tokens.begin(), requested_tokens.end(), configured) !=
            requested_tokens.end()) {
            return true;
        }
    }
    return false;
}

bool ssid_is_advertised(const wireless_utils::sBssInfoConf &bss_info)
{
    return bss_info.hidden_ssid != WSC::eWscVendorExtHiddenSsid::ENABLED;
}

void fill_psk_or_passphrase(const wireless_utils::sBssInfoConf &selected, const std::string &akm,
                            std::string &passphrase, std::string &psk_hex)
{
    if (selected.network_key.empty()) {
        return;
    }
    const bool is_hex_psk =
        (selected.network_key.size() == 64) &&
        std::all_of(selected.network_key.begin(), selected.network_key.end(),
                    [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (is_hex_psk && (akm.find("psk") != std::string::npos)) {
        psk_hex = selected.network_key;
    } else if (akm.find("psk") != std::string::npos || akm.find("sae") != std::string::npos) {
        passphrase = selected.network_key;
    }
}

void load_optional_controller_key_params(db &database, std::string &pp_key_hex,
                                         std::string &group_id)
{
    pp_key_hex.clear();
    group_id.clear();

    db::DppStoreSections sections;
    if (!database.get_dpp_store_sections(sections)) {
        return;
    }
    auto sec_it = sections.find(k_dpp_controller_keys_section);
    if (sec_it == sections.end()) {
        return;
    }
    auto pp_it = sec_it->second.find("pp_key");
    if (pp_it != sec_it->second.end()) {
        pp_key_hex = pp_it->second;
    }
    auto group_it = sec_it->second.find("group_id");
    if (group_it != sec_it->second.end()) {
        group_id = group_it->second;
    }
}

bool role_requested(const std::vector<std::string> &roles, const std::string &role)
{
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

} // namespace

bool get_dpp_backhaul_sta_configuration(db &database, const std::shared_ptr<Agent> &agent,
                                        const std::string &ssid_hex,
                                        const std::string &requested_backhaul_akm,
                                        DppBackhaulStaConfiguration &configuration,
                                        std::string &error)
{
    configuration = {};
    error.clear();

    if (!agent) {
        error = "Agent entry not found for DPP backhaul STA configuration";
        return false;
    }

    std::string requested_ssid;
    if (!hex_string_to_plain_string(ssid_hex, requested_ssid)) {
        error = "Invalid DPP onboarding SSID hex";
        return false;
    }

    const wireless_utils::sBssInfoConf *selected = nullptr;

    auto select_from_list = [&](const std::list<wireless_utils::sBssInfoConf> &candidates) {
        const wireless_utils::sBssInfoConf *fallback = nullptr;
        for (const auto &candidate : candidates) {
            if (!candidate.backhaul) {
                continue;
            }
            if (!requested_ssid.empty() && !candidate.ssid.empty() &&
                candidate.ssid != requested_ssid) {
                continue;
            }
            if (!fallback) {
                fallback = &candidate;
            }
            if (!candidate.ssid.empty()) {
                selected = &candidate;
                return true;
            }
        }
        if (!selected && fallback) {
            selected = fallback;
            return true;
        }
        return false;
    };

    for (const auto &radio_entry : agent->radios) {
        if (!radio_entry.second) {
            continue;
        }
        if (select_from_list(database.get_configured_bss_info(radio_entry.second->radio_uid))) {
            break;
        }
    }
    if (!selected) {
        select_from_list(database.get_bss_info_configuration(agent->al_mac));
    }
    if (!selected) {
        select_from_list(database.get_bss_info_configuration());
    }

    if (!selected) {
        if (requested_ssid.empty() && requested_backhaul_akm.empty()) {
            error = "No configured backhaul BSS available for DPP mapBackhaulSta object";
            return false;
        }
        configuration.ssid = requested_ssid;
        configuration.akm  = requested_backhaul_akm;
        return true;
    }

    configuration.ssid = !selected->ssid.empty() ? selected->ssid : requested_ssid;
    configuration.akm  = dpp_akm_from_bss_info(*selected);
    if (!dpp_akm_is_compatible(configuration.akm, requested_backhaul_akm)) {
        error = "Configured backhaul AKM is not compatible with DPP Configuration Request";
        return false;
    }
    if (configuration.akm.empty()) {
        configuration.akm = requested_backhaul_akm;
    }

    fill_psk_or_passphrase(*selected, configuration.akm, configuration.passphrase,
                           configuration.psk_hex);
    configuration.ssid_advertisement_valid = true;
    configuration.ssid_advertisement       = ssid_is_advertised(*selected);
    return true;
}

bool get_dpp_sta_configuration(db &database, const std::shared_ptr<Agent> &agent,
                               const std::string &ssid_hex, DppStaConfiguration &configuration,
                               std::string &error)
{
    configuration = {};
    error.clear();

    if (!agent) {
        error = "Agent entry not found for DPP STA configuration";
        return false;
    }

    std::string requested_ssid;
    if (!hex_string_to_plain_string(ssid_hex, requested_ssid)) {
        error = "Invalid DPP onboarding SSID hex";
        return false;
    }

    const wireless_utils::sBssInfoConf *selected = nullptr;

    // M-9: netRole=sta must use fronthaul BSS only (never backhaul credentials).
    auto select_from_list = [&](const std::list<wireless_utils::sBssInfoConf> &candidates) {
        const wireless_utils::sBssInfoConf *fallback = nullptr;
        for (const auto &candidate : candidates) {
            if (!candidate.fronthaul) {
                continue;
            }
            if (!candidate.ssid.empty() && !requested_ssid.empty() &&
                candidate.ssid == requested_ssid) {
                selected = &candidate;
                return true;
            }
            if (!fallback) {
                fallback = &candidate;
            }
        }
        if (!selected && fallback) {
            selected = fallback;
            return true;
        }
        return false;
    };

    for (const auto &radio_entry : agent->radios) {
        if (!radio_entry.second) {
            continue;
        }
        if (select_from_list(database.get_configured_bss_info(radio_entry.second->radio_uid))) {
            break;
        }
    }
    if (!selected) {
        select_from_list(database.get_bss_info_configuration(agent->al_mac));
    }
    if (!selected) {
        select_from_list(database.get_bss_info_configuration());
    }

    if (!selected) {
        error = "No configured fronthaul BSS available for DPP sta object";
        return false;
    }

    configuration.ssid = !selected->ssid.empty() ? selected->ssid : requested_ssid;
    if (configuration.ssid.empty()) {
        error = "Configured fronthaul BSS has empty SSID for DPP sta object";
        return false;
    }

    configuration.akm = dpp_akm_from_bss_info(*selected);
    if (configuration.akm == "open") {
        error = "Configured fronthaul BSS uses open authentication; DPP sta object requires "
                "DPP, PSK, SAE, or PSK+SAE credentials";
        return false;
    }
    if (configuration.akm.empty()) {
        configuration.akm = "dpp";
    }

    fill_psk_or_passphrase(*selected, configuration.akm, configuration.passphrase,
                           configuration.psk_hex);
    configuration.ssid_advertisement_valid = true;
    configuration.ssid_advertisement       = ssid_is_advertised(*selected);
    return true;
}

bool build_dpp_configuration_objects_from_policy(db &database, DppConfiguratorSession &session,
                                                 const sMacAddr &agent_mac,
                                                 const std::string &configuration_request_json,
                                                 std::vector<std::string> &config_objects,
                                                 bool &send_conn_status, std::string &error)
{
    config_objects.clear();
    send_conn_status = false;
    error.clear();

    std::vector<std::string> requested_roles;
    std::string requested_backhaul_akm;
    bool bsta_max_links = false;
    std::unordered_set<std::string> bsta_ruids;
    if (!parse_dpp_config_request_objects(configuration_request_json, requested_roles,
                                          &requested_backhaul_akm, &bsta_max_links, &bsta_ruids)) {
        error = "Failed parsing DPP Configuration Request object";
        return false;
    }
    (void)bsta_max_links;
    (void)bsta_ruids;

    if (requested_roles.empty()) {
        // Fallback when the request JSON is a single object already unwrapped to netRole.
        if (!configuration_request_json.empty()) {
            error = "DPP Configuration Request contained no netRole";
            return false;
        }
        error = "Empty DPP Configuration Request";
        return false;
    }

    DppKeyPtr csign_key;
    if (!load_controller_csign_key(database, csign_key, error)) {
        return false;
    }

    std::string pp_key_hex;
    std::string group_id;
    load_optional_controller_key_params(database, pp_key_hex, group_id);

    auto agent = database.m_agents.get(agent_mac);

    auto build_object = [&](const std::string &net_role, const std::string &ssid_hex,
                            const DppConfigurationObjectOptions &options,
                            std::string &object_json) -> bool {
        std::string connector_payload;
        if (!session.build_connector_payload(net_role, group_id, 0, connector_payload, error)) {
            return false;
        }
        return build_dpp_configuration_object(csign_key.get(), pp_key_hex, connector_payload,
                                              ssid_hex, net_role, group_id, options, object_json,
                                              error);
    };

    const bool want_sta_only =
        role_requested(requested_roles, "sta") && !role_requested(requested_roles, "mapAgent") &&
        !role_requested(requested_roles, "mapBackhaulSta");

    if (want_sta_only) {
        if (!agent) {
            error = "Proxy/Enrollee Agent not found for DPP sta configuration";
            return false;
        }
        DppStaConfiguration sta_config;
        if (!get_dpp_sta_configuration(database, agent, {}, sta_config, error)) {
            return false;
        }

        DppConfigurationObjectOptions sta_options;
        sta_options.include_discovery            = true;
        sta_options.include_net_role             = true;
        sta_options.net_role                     = "sta";
        sta_options.akm                          = !sta_config.akm.empty() ? sta_config.akm : "dpp";
        sta_options.passphrase                   = sta_config.passphrase;
        sta_options.psk_hex                      = sta_config.psk_hex;
        sta_options.security_ies_hex             = sta_config.security_ies_hex;
        sta_options.ssid_advertisement_valid     = sta_config.ssid_advertisement_valid;
        sta_options.ssid_advertisement           = sta_config.ssid_advertisement;

        std::string object_json;
        if (!build_object("sta", plain_string_to_hex(sta_config.ssid), sta_options, object_json)) {
            return false;
        }
        config_objects.push_back(std::move(object_json));
        send_conn_status = false;
        return true;
    }

    // Multi-AP Agent path (M-5): mapAgent (+ mapBackhaulSta when requested).
    if (!role_requested(requested_roles, "mapAgent") &&
        !role_requested(requested_roles, "mapBackhaulSta") &&
        !role_requested(requested_roles, "sta")) {
        error = "Unsupported DPP Configuration Request netRole set";
        return false;
    }

    if (role_requested(requested_roles, "mapAgent") ||
        role_requested(requested_roles, "mapBackhaulSta")) {
        DppConfigurationObjectOptions map_agent_options;
        map_agent_options.include_discovery            = false;
        map_agent_options.include_net_role             = true;
        map_agent_options.net_role                     = "mapAgent";
        map_agent_options.include_df_counter_threshold = true;
        map_agent_options.df_counter_threshold         = 3;

        std::string map_agent_object;
        if (!build_object("mapAgent", {}, map_agent_options, map_agent_object)) {
            return false;
        }
        config_objects.push_back(std::move(map_agent_object));
    }

    const bool want_backhaul_sta = role_requested(requested_roles, "mapBackhaulSta") ||
                                   role_requested(requested_roles, "sta");
    if (want_backhaul_sta) {
        if (!agent) {
            error = "Proxy/Enrollee Agent not found for DPP mapBackhaulSta configuration";
            return false;
        }
        DppBackhaulStaConfiguration backhaul_config;
        if (!get_dpp_backhaul_sta_configuration(database, agent, {}, requested_backhaul_akm,
                                                backhaul_config, error)) {
            return false;
        }

        DppConfigurationObjectOptions bsta_options;
        bsta_options.include_discovery        = true;
        bsta_options.include_net_role         = true;
        bsta_options.net_role                 = "mapBackhaulSta";
        bsta_options.akm =
            !backhaul_config.akm.empty() ? backhaul_config.akm : requested_backhaul_akm;
        bsta_options.passphrase               = backhaul_config.passphrase;
        bsta_options.psk_hex                  = backhaul_config.psk_hex;
        bsta_options.security_ies_hex         = backhaul_config.security_ies_hex;
        bsta_options.ssid_advertisement_valid = backhaul_config.ssid_advertisement_valid;
        bsta_options.ssid_advertisement       = backhaul_config.ssid_advertisement;

        std::string bsta_object;
        if (!build_object("mapBackhaulSta", plain_string_to_hex(backhaul_config.ssid), bsta_options,
                          bsta_object)) {
            return false;
        }
        config_objects.push_back(std::move(bsta_object));
        send_conn_status = true;
    }

    if (config_objects.empty()) {
        error = "No DPP Configuration Objects built from Controller BSS policy";
        return false;
    }
    return true;
}

} // namespace controller_dpp
} // namespace son
