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

void set_wps_callback(WpsAutoCb auto_cb) { g_wps_auto_cb = std::move(auto_cb); }

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

std::vector<beerocks::nbapi::sFunctions> get_func_list(void)
{
    const std::vector<beerocks::nbapi::sFunctions> functions_list = {
        {"initiate_wps_pbc", AGENT_ROOT_DM ".WPS.InitiateWPSPBC", initiate_wps_pbc}};
    return functions_list;
}

} // namespace actions
} // namespace agent
} // namespace prplmesh
