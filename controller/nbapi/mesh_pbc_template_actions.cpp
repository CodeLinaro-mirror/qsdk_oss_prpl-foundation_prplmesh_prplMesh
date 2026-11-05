/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_template_actions.h"
#include "nbapi_utils.h"
#include "templates_actions.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <sstream>

namespace prplmesh {
namespace controller {
namespace actions {

namespace {
constexpr char MESH_PBC_UUID_E_PARAM[] = "X_PRPLWARE-COM_MeshPBCUUID_E";
} // namespace

/**
 * @brief Runs the default type validation, then gets the new value from the arguments.
 */
static amxd_status_t mesh_pbc_validate_get_string(amxd_object_t *object, amxd_param_t *param,
                                                  amxd_action_t reason,
                                                  const amxc_var_t *const args,
                                                  amxc_var_t *const retval, void *priv,
                                                  std::string &value)
{
    amxd_status_t status = amxd_action_param_validate(object, param, reason, args, retval, priv);
    if (status != amxd_status_ok) {
        return status;
    }

    char *new_value = amxc_var_dyncast(cstring_t, args);
    value.assign(new_value ? new_value : "");
    free(new_value);
    return amxd_status_ok;
}

/**
 * @return true if the value is a UUID in the canonical 8-4-4-4-12 hexadecimal form; else
 * false, with the cause in error.
 */
static bool mesh_pbc_check_uuid_e(const std::string &value, std::string &error)
{
    constexpr size_t uuid_length = 36;
    bool valid                   = (value.size() == uuid_length);
    for (size_t i = 0; valid && i < uuid_length; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            valid = (value[i] == '-');
        } else {
            valid = (std::isxdigit(static_cast<unsigned char>(value[i])) != 0);
        }
    }
    if (!valid) {
        error = "\"" + value + "\" is not a canonical UUID";
    }
    return valid;
}

/**
 * @brief Validate Templates.Network.X_PRPLWARE-COM_MeshPBCUUID_E.
 *
 * Same rule as the pwhm validator of WiFi.AccessPoint.{i}.WPS.UUID: accept the current value,
 * otherwise only a UUID in the canonical 8-4-4-4-12 hexadecimal form. So a write of "" fails
 * once a value is set. The current value is accepted so that the default "" loads: the parser
 * validates each value at the end of its definition. The value is also checked while the
 * saved data model loads: a saved value that is not a UUID stops the load.
 */
static amxd_status_t validate_mesh_pbc_uuid_e(amxd_object_t *object, amxd_param_t *param,
                                              amxd_action_t reason, const amxc_var_t *const args,
                                              amxc_var_t *const retval, void *priv)
{
    std::string value;
    amxd_status_t status =
        mesh_pbc_validate_get_string(object, param, reason, args, retval, priv, value);
    if (status != amxd_status_ok) {
        return status;
    }

    char *current        = amxc_var_dyncast(cstring_t, &param->value);
    const bool unchanged = (value == (current ? current : ""));
    free(current);
    std::string error;
    if (unchanged || mesh_pbc_check_uuid_e(value, error)) {
        return amxd_status_ok;
    }
    LOG(WARNING) << MESH_PBC_UUID_E_PARAM << " rejected: " << error;
    return amxd_status_invalid_value;
}

std::vector<beerocks::nbapi::sActionsCallback> get_mesh_pbc_template_actions_list(void)
{
    return {
        {"validate_mesh_pbc_uuid_e", validate_mesh_pbc_uuid_e},
    };
}

} // namespace actions
} // namespace controller
} // namespace prplmesh
