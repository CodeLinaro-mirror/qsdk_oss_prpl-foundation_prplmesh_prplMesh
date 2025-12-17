/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "on_action.h"

using namespace beerocks;
namespace prplmesh {
namespace agent {
namespace actions {

static WpsAutoCb g_wps_auto_cb = nullptr;
static WpsAutoCb g_connect_cb  = nullptr;

void set_wps_callback(WpsAutoCb auto_cb) { g_wps_auto_cb = std::move(auto_cb); }

void set_connect_callback(WpsAutoCb cb) { g_connect_cb = std::move(cb); }

// Actions

/**
 * @brief WPS Push Button Connect (PBC) NBAPI action handler.
 *
 * Example:
 *   ubus call X_PRPLWARE-COM_Agent.WPS InitiateWPSPBC '{}'
 */
amxd_status_t initiate_wps_pbc(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args,
                               amxc_var_t *ret)
{
    if (!g_wps_auto_cb) {
        LOG(ERROR) << "WPS PBC: callback not set";
        return amxd_status_not_supported;
    }
    const bool ok = g_wps_auto_cb();
    return ok ? amxd_status_ok : amxd_status_unknown_error;
}

static void event_configuration_changed(const char *const sig_name, const amxc_var_t *const data,
                                        void *const priv)
{
    amxd_object_t *configuration =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!configuration) {
        LOG(WARNING) << "Failed to get object " AGENT_ROOT_DM ".Configuration";
        return;
    }

    auto db         = AgentDB::get();
    bool bh_changed = false;

    auto ssid = amxd_object_get_cstring_t(configuration, "SSID", nullptr);
    if (ssid) {
        LOG(DEBUG) << "Updating backhaul SSID: " << db->device_conf.back_radio.ssid << " -> "
                   << ssid;
        bh_changed |= (db->device_conf.back_radio.ssid != ssid);
        db->device_conf.back_radio.ssid = ssid;
    }
    free(ssid);

    auto pass = amxd_object_get_cstring_t(configuration, "Passphrase", nullptr);
    if (pass) {
        LOG(DEBUG) << "Updating backhaul Passphrase: " << db->device_conf.back_radio.pass << " -> "
                   << pass;
        bh_changed |= (db->device_conf.back_radio.pass != pass);
        db->device_conf.back_radio.pass = pass;
    }
    free(pass);

    auto security = amxd_object_get_cstring_t(configuration, "Security", nullptr);
    if (security) {
        const auto security_type = bwl::wifi_sec_from_c_str(security);
        LOG(DEBUG) << "Updating backhaul Security: "
                   << bwl::wifi_sec_to_c_str(db->device_conf.back_radio.security_type) << " -> "
                   << bwl::wifi_sec_to_c_str(security_type);
        bh_changed |= (db->device_conf.back_radio.security_type != security_type);
        db->device_conf.back_radio.security_type = security_type;
    }
    free(security);

    if (bh_changed) {
        if (!g_connect_cb) {
            LOG(ERROR) << "Configuration changed: connect callback not set";
            return;
        }
        if (!g_connect_cb()) {
            LOG(ERROR) << "Configuration changed: failed to connect to backhaul AP";
        }
    }
}

std::vector<beerocks::nbapi::sEvents> get_events_list(void)
{
    const std::vector<beerocks::nbapi::sEvents> events_list = {
        {"event_configuration_changed", event_configuration_changed},
    };
    return events_list;
}

std::vector<beerocks::nbapi::sFunctions> get_func_list(void)
{
    const std::vector<beerocks::nbapi::sFunctions> functions_list = {
        {"initiate_wps_pbc", AGENT_ROOT_DM ".WPS.InitiateWPSPBC", initiate_wps_pbc}};
    return functions_list;
}

} // namespace actions
} // namespace agent
} // namespace prplmesh
