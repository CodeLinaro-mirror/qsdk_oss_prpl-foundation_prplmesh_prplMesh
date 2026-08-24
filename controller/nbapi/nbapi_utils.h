/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef NBAPI_UTILS_H
#define NBAPI_UTILS_H

#include <amxd/amxd_object.h>
#include <easylogging++.h>
#include <cstdint>
#include <string>

namespace prplmesh {
namespace controller {
namespace actions {

inline std::string get_param_string(amxd_object_t *object, const char *param_name)
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

inline bool get_param_bool(amxd_object_t *object, const char *param_name)
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

inline uint32_t get_param_uint32(amxd_object_t *object, const char *param_name)
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

} // namespace actions
} // namespace controller
} // namespace prplmesh

#endif // NBAPI_UTILS_H
