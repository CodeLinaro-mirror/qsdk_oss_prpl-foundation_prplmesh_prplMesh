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
#include "dpp/dpp_akm.h"

#include <bcl/beerocks_string_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <easylogging++.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace son {
namespace controller_dpp {
namespace {

// std::string (not char[]) so map lookups do not build a temporary key on every call.
const std::string k_dpp_controller_keys_section = "dpp_controller_keys";
const std::string k_dpp_pp_key                  = "pp_key";
const std::string k_dpp_group_id                = "group_id";

bool hex_string_to_plain_string(const std::string &hex, std::string &plain)
{
    plain.clear();
    if (hex.empty()) {
        return true;
    }
    if ((hex.size() % 2) != 0 || !std::all_of(hex.begin(), hex.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        })) {
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
    // INVALID is 0xffff, not a supported combination of PSK and SAE bits.
    if (bss_info.authentication_type == WSC::eWscAuth::WSC_AUTH_INVALID) {
        return {};
    }
    // WSC auth is a bitmask (e.g. WPA3-Personal-Transition = WPA2PSK|SAE). Do not
    // switch on the combined value — that misses transition and falls through empty.
    const auto auth          = static_cast<uint16_t>(bss_info.authentication_type);
    constexpr uint16_t k_psk = static_cast<uint16_t>(WSC::eWscAuth::WSC_AUTH_WPA2PSK) |
                               static_cast<uint16_t>(WSC::eWscAuth::WSC_AUTH_WPAPSK);
    constexpr uint16_t k_sae = static_cast<uint16_t>(WSC::eWscAuth::WSC_AUTH_SAE) |
                               static_cast<uint16_t>(WSC::eWscAuth::WSC_AUTH_SAE_AKM24);
    constexpr uint16_t k_open = static_cast<uint16_t>(WSC::eWscAuth::WSC_AUTH_OPEN);
    constexpr uint16_t k_rsn  = static_cast<uint16_t>(WSC::eWscAuth::WSC_AUTH_RSN);

    const bool has_psk = (auth & k_psk) != 0;
    const bool has_sae = (auth & k_sae) != 0;
    const bool wpa3_compat =
        (auth & k_rsn) != 0 &&
        bss_info.additional_auth == wireless_utils::eAdditionalAuth::WPA3_PERSONAL_COMPATIBILITY;

    if ((has_psk && has_sae) || wpa3_compat) {
        return "psk+sae";
    }
    if (has_sae) {
        return "sae";
    }
    if (has_psk) {
        return "psk";
    }
    if ((auth & k_open) != 0 && (auth & ~(k_open)) == 0) {
        return "open";
    }
    // Pure DPP / unknown — leave empty; callers must not invent "dpp" over a PSK key.
    return {};
}

// bSTAList akm may be an AKM suite selector list (Wi-Fi Easy Connect v3.0
// section 4.3.5). Publish the symbolic form that fill_psk_or_passphrase()
// and protocol version 1 Enrollees understand.
std::string config_object_akm_from_request(const std::string &requested_akm)
{
    auto symbolic = son::controller_dpp::dpp_akm_to_symbolic(requested_akm);
    return symbolic.empty() ? requested_akm : symbolic;
}

bool ssid_is_advertised(const wireless_utils::sBssInfoConf &bss_info)
{
    return bss_info.hidden_ssid != WSC::eWscVendorExtHiddenSsid::ENABLED;
}

std::string drop_akm_token(const std::string &akm, const std::string &token)
{
    std::string out;
    std::string part;
    std::istringstream in(akm);
    while (std::getline(in, part, '+')) {
        if (part.empty() || part == token) {
            continue;
        }
        if (!out.empty()) {
            out += '+';
        }
        out += part;
    }
    return out;
}

// Raw 64-hex PSK goes out only as psk_hex, but hostapd needs "pass" for SAE: PSK+SAE drops
// "sae" from the AKM; SAE without PSK is rejected (the hex is not a known SAE password).
bool fill_psk_or_passphrase(const wireless_utils::sBssInfoConf &selected, std::string &akm,
                            std::string &passphrase, std::string &psk_hex, std::string &error)
{
    if (selected.network_key.empty()) {
        return true;
    }
    const bool is_hex_psk = (selected.network_key.size() == 64) &&
                            std::all_of(selected.network_key.begin(), selected.network_key.end(),
                                        [](unsigned char c) { return std::isxdigit(c) != 0; });
    const bool has_psk = akm.find("psk") != std::string::npos;
    const bool has_sae = akm.find("sae") != std::string::npos;
    if (is_hex_psk && has_sae) {
        if (!has_psk) {
            error = "Configured BSS key is a raw 64-hex PSK but AKM '" + akm +
                    "' requires an SAE passphrase";
            return false;
        }
        const auto downgraded = drop_akm_token(akm, "sae");
        LOG(WARNING) << "DPP: raw 64-hex PSK cannot be used for SAE; publishing akm '" << downgraded
                     << "' instead of '" << akm << "'";
        akm = downgraded;
    }
    if (is_hex_psk && has_psk) {
        psk_hex = selected.network_key;
    } else if (has_psk || has_sae) {
        passphrase = selected.network_key;
    }
    return true;
}

void load_optional_controller_key_params(const db &database, std::string &pp_key_hex,
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
    auto pp_it = sec_it->second.find(k_dpp_pp_key);
    if (pp_it != sec_it->second.end()) {
        pp_key_hex = pp_it->second;
    }
    auto group_it = sec_it->second.find(k_dpp_group_id);
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

    // Return a pointer (no out-parameter side effects) so static analysis can see
    // that selected is non-null after the failure returns below.
    auto pick_backhaul_bss = [&](const std::list<wireless_utils::sBssInfoConf> &candidates)
        -> const wireless_utils::sBssInfoConf * {
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
                return &candidate;
            }
        }
        return fallback;
    };

    const wireless_utils::sBssInfoConf *selected = nullptr;
    for (const auto &radio_entry : agent->radios) {
        if (!radio_entry.second) {
            continue;
        }
        selected =
            pick_backhaul_bss(database.get_configured_bss_info(radio_entry.second->radio_uid));
        if (selected) {
            break;
        }
    }
    if (!selected) {
        selected = pick_backhaul_bss(database.get_bss_info_configuration(agent->al_mac));
    }
    if (!selected) {
        selected = pick_backhaul_bss(database.get_bss_info_configuration());
    }
    if (!selected) {
        if (requested_ssid.empty() && requested_backhaul_akm.empty()) {
            error = "No configured backhaul BSS available for DPP mapBackhaulSta object";
            return false;
        }
        configuration.ssid = std::move(requested_ssid);
        configuration.akm  = config_object_akm_from_request(requested_backhaul_akm);
        return true;
    }

    if (!selected->ssid.empty()) {
        configuration.ssid = selected->ssid;
    } else {
        configuration.ssid = std::move(requested_ssid);
    }
    configuration.akm = dpp_akm_from_bss_info(*selected);
    if (!son::controller_dpp::dpp_akm_is_compatible(configuration.akm, requested_backhaul_akm)) {
        error = "Configured backhaul AKM is not compatible with DPP Configuration Request";
        return false;
    }
    if (configuration.akm.empty()) {
        configuration.akm = config_object_akm_from_request(requested_backhaul_akm);
    }
    if (configuration.akm.empty()) {
        // BSS has a passphrase but no mappable WSC AKM - do not publish akm "dpp".
        if (!selected->network_key.empty()) {
            error = "Configured backhaul BSS authentication cannot be mapped for DPP "
                    "mapBackhaulSta (expected PSK, SAE, or PSK+SAE)";
            return false;
        }
        configuration.akm = "dpp";
    }

    const auto configured_akm = configuration.akm;
    if (!fill_psk_or_passphrase(*selected, configuration.akm, configuration.passphrase,
                                configuration.psk_hex, error)) {
        return false;
    }
    // Credential filling can narrow the AKM (e.g. a raw PSK drops SAE); recheck what is published.
    if (!son::controller_dpp::dpp_akm_is_compatible(configuration.akm, requested_backhaul_akm)) {
        error = "Effective backhaul AKM '" + configuration.akm + "' (configured '" +
                configured_akm + "') is not compatible with DPP Configuration Request";
        return false;
    }
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

    // netRole=sta must use fronthaul BSS only (never backhaul credentials).
    // Return a pointer so selected is clearly non-null after the failure return below.
    auto pick_fronthaul_bss = [&](const std::list<wireless_utils::sBssInfoConf> &candidates)
        -> const wireless_utils::sBssInfoConf * {
        const wireless_utils::sBssInfoConf *fallback = nullptr;
        for (const auto &candidate : candidates) {
            if (!candidate.fronthaul) {
                continue;
            }
            if (!candidate.ssid.empty() && !requested_ssid.empty() &&
                candidate.ssid == requested_ssid) {
                return &candidate;
            }
            if (!fallback) {
                fallback = &candidate;
            }
        }
        return fallback;
    };

    const wireless_utils::sBssInfoConf *selected = nullptr;
    for (const auto &radio_entry : agent->radios) {
        if (!radio_entry.second) {
            continue;
        }
        selected =
            pick_fronthaul_bss(database.get_configured_bss_info(radio_entry.second->radio_uid));
        if (selected) {
            break;
        }
    }
    if (!selected) {
        selected = pick_fronthaul_bss(database.get_bss_info_configuration(agent->al_mac));
    }
    if (!selected) {
        selected = pick_fronthaul_bss(database.get_bss_info_configuration());
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
        // Transition / unknown enums used to fall through here and become "dpp" while
        // the AP still had a PSK/SAE passphrase — Enrollee then never associated.
        if (!selected->network_key.empty()) {
            error = "Configured fronthaul BSS authentication cannot be mapped for DPP sta "
                    "(expected PSK, SAE, or PSK+SAE)";
            return false;
        }
        configuration.akm = "dpp";
    }

    if (!fill_psk_or_passphrase(*selected, configuration.akm, configuration.passphrase,
                                configuration.psk_hex, error)) {
        return false;
    }
    configuration.ssid_advertisement_valid = true;
    configuration.ssid_advertisement       = ssid_is_advertised(*selected);
    return true;
}

namespace {

/**
 * Parse the Enrollee Configuration Request into the requested netRoles and bSTAList AKM.
 * Malformed or role-less requests fail here, before any keystore access.
 */
bool parse_policy_request(const std::string &configuration_request_json,
                          std::vector<std::string> &requested_roles,
                          std::string &requested_backhaul_akm, std::string &error)
{
    // bSTA_Maximum_Links / RadioList are not requested: only single-link backhaul is supported.
    if (!parse_dpp_config_request_objects(configuration_request_json, requested_roles,
                                          &requested_backhaul_akm, nullptr, nullptr)) {
        error = "Failed parsing DPP Configuration Request object";
        return false;
    }

    std::ostringstream parsed_roles;
    for (size_t i = 0; i < requested_roles.size(); ++i) {
        if (i != 0) {
            parsed_roles << ',';
        }
        parsed_roles << requested_roles[i];
    }
    LOG(INFO) << "DPP Configuration Request parsed roles=" << parsed_roles.str()
              << " bSTAList akm=" << requested_backhaul_akm;
    if (son::controller_dpp::dpp_akm_has_easymesh_decimal_alias(requested_backhaul_akm)) {
        LOG(WARNING) << "DPP: bSTAList akm uses unassigned selector 000FAC24, treated as "
                        "SAE-EXT-KEY (000FAC18) as in Wi-Fi EasyMesh v6.1 Figure 9";
    }

    if (requested_roles.empty()) {
        // Fallback when the request JSON is a single object already unwrapped to netRole.
        if (!configuration_request_json.empty()) {
            error = "DPP Configuration Request contained no netRole";
            return false;
        }
        error = "Empty DPP Configuration Request";
        return false;
    }
    return true;
}

/** Build the Configuration Objects for the parsed roles with already-loaded signing keys. */
bool build_policy_objects(db &database, DppConfiguratorSession &session, const sMacAddr &agent_mac,
                          const std::vector<std::string> &requested_roles,
                          const std::string &requested_backhaul_akm, DppKey *csign_key,
                          const std::string &pp_key_hex, const std::string &group_id,
                          std::vector<std::string> &config_objects, bool &send_conn_status,
                          std::string &error)
{
    auto agent = database.m_agents.get(agent_mac);

    auto build_object = [&](const std::string &net_role, const std::string &ssid_hex,
                            const DppConfigurationObjectOptions &options,
                            std::string &object_json) -> bool {
        std::string connector_payload;
        if (!session.build_connector_payload(net_role, group_id, 0, connector_payload, error)) {
            return false;
        }
        return build_dpp_configuration_object(csign_key, pp_key_hex, connector_payload, ssid_hex,
                                              net_role, group_id, options, object_json, error);
    };

    const bool want_sta_only = role_requested(requested_roles, "sta") &&
                               !role_requested(requested_roles, "mapAgent") &&
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
        sta_options.include_discovery        = true;
        sta_options.include_net_role         = true;
        sta_options.net_role                 = "sta";
        sta_options.akm                      = !sta_config.akm.empty() ? sta_config.akm : "dpp";
        sta_options.passphrase               = sta_config.passphrase;
        sta_options.psk_hex                  = sta_config.psk_hex;
        sta_options.security_ies_hex         = sta_config.security_ies_hex;
        sta_options.ssid_advertisement_valid = sta_config.ssid_advertisement_valid;
        sta_options.ssid_advertisement       = sta_config.ssid_advertisement;

        std::string object_json;
        if (!build_object("sta", plain_string_to_hex(sta_config.ssid), sta_options, object_json)) {
            return false;
        }
        config_objects.push_back(std::move(object_json));
        send_conn_status = false;
        return true;
    }

    // Multi-AP Agent path: mapAgent (+ mapBackhaulSta when requested).
    if (!role_requested(requested_roles, "mapAgent") &&
        !role_requested(requested_roles, "mapBackhaulSta") &&
        !role_requested(requested_roles, "sta")) {
        error = "Unsupported DPP Configuration Request netRole set";
        return false;
    }

    // Emit mapAgent only when the Enrollee requested it (not for bSTAList-only).
    if (role_requested(requested_roles, "mapAgent")) {
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

    const bool want_backhaul_sta =
        role_requested(requested_roles, "mapBackhaulSta") || role_requested(requested_roles, "sta");
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
        bsta_options.include_discovery = true;
        bsta_options.include_net_role  = true;
        bsta_options.net_role          = "mapBackhaulSta";
        // Wi-Fi bSTA enrollee (no mapAgent): wpa_supplicant expects wi-fi_tech=infra.
        if (!role_requested(requested_roles, "mapAgent")) {
            bsta_options.wifi_tech = "infra";
        }
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

/** Failed builds never leave partial objects (e.g. a mapAgent object before a bSTA error). */
bool finish_policy_build(bool ok, std::vector<std::string> &config_objects, bool &send_conn_status)
{
    if (!ok) {
        config_objects.clear();
        send_conn_status = false;
    }
    return ok;
}

} // namespace

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
    if (!parse_policy_request(configuration_request_json, requested_roles, requested_backhaul_akm,
                              error)) {
        return false;
    }

    DppKeyPtr csign_key;
    if (!load_dpp_configurator_csign_key(database, csign_key, error)) {
        return false;
    }

    std::string pp_key_hex;
    std::string group_id;
    load_optional_controller_key_params(database, pp_key_hex, group_id);

    return finish_policy_build(build_policy_objects(database, session, agent_mac, requested_roles,
                                                    requested_backhaul_akm, csign_key.get(),
                                                    pp_key_hex, group_id, config_objects,
                                                    send_conn_status, error),
                               config_objects, send_conn_status);
}

bool build_dpp_configuration_objects_from_policy(db &database, DppConfiguratorSession &session,
                                                 const sMacAddr &agent_mac,
                                                 const std::string &configuration_request_json,
                                                 DppKey *csign_key, const std::string &pp_key_hex,
                                                 const std::string &group_id,
                                                 std::vector<std::string> &config_objects,
                                                 bool &send_conn_status, std::string &error)
{
    config_objects.clear();
    send_conn_status = false;
    error.clear();

    std::vector<std::string> requested_roles;
    std::string requested_backhaul_akm;
    if (!parse_policy_request(configuration_request_json, requested_roles, requested_backhaul_akm,
                              error)) {
        return false;
    }
    if (!csign_key) {
        error = "Missing DPP Configurator C-sign key";
        return false;
    }

    return finish_policy_build(build_policy_objects(database, session, agent_mac, requested_roles,
                                                    requested_backhaul_akm, csign_key, pp_key_hex,
                                                    group_id, config_objects, send_conn_status,
                                                    error),
                               config_objects, send_conn_status);
}

} // namespace controller_dpp
} // namespace son
