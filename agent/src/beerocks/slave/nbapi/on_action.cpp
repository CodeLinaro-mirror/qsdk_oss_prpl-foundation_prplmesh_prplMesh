/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "on_action.h"
#include <mapf/common/amx_mutex.h>

using namespace beerocks;
namespace prplmesh {
namespace agent {
namespace actions {

static WpsAutoCb g_wps_auto_cb = nullptr;

void set_wps_callback(WpsAutoCb auto_cb)
{
    AmxGuard guard;
    g_wps_auto_cb = std::move(auto_cb);
}

static DppOnboardingCb g_dpp_onboarding_cb = nullptr;

void set_dpp_onboarding_callback(DppOnboardingCb cb) { g_dpp_onboarding_cb = std::move(cb); }

// Events

/**
 * @brief Handler of "dm:object-changed" on Configuration when EnableDPPOnboarding changed.
 *
 * Example:
 *   ubus-cli "X_PRPLWARE-COM_Agent.Configuration.EnableDPPOnboarding=0"
 */
static void event_dpp_onboarding_changed(const char *const sig_name, const amxc_var_t *const data,
                                         void *const priv)
{
    const amxc_var_t *new_value =
        amxc_var_get_path(data, "parameters.EnableDPPOnboarding.to", AMXC_VAR_FLAG_DEFAULT);
    if (!new_value) {
        LOG(WARNING) << "DPP: EnableDPPOnboarding change event without new value";
        return;
    }
    const bool enable = amxc_var_dyncast(bool, new_value);
    LOG(INFO) << "DPP: Configuration.EnableDPPOnboarding set to " << enable;
    if (!g_dpp_onboarding_cb) {
        LOG(WARNING) << "DPP: EnableDPPOnboarding callback not set; applied at next Agent start";
        return;
    }
    g_dpp_onboarding_cb(enable);
}

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
    AmxGuard guard;
    if (!g_wps_auto_cb) {
        LOG(ERROR) << "WPS PBC: callback not set";
        return amxd_status_not_supported;
    }
    const bool ok = g_wps_auto_cb();
    return ok ? amxd_status_ok : amxd_status_unknown_error;
}

std::vector<beerocks::nbapi::sEvents> get_events_list(void)
{
    const std::vector<beerocks::nbapi::sEvents> events_list = {
        {"event_dpp_onboarding_changed", event_dpp_onboarding_changed}};
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
