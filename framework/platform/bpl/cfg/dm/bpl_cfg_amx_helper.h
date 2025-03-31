/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_CFG_AMX_HELPER_H_
#define _BPL_CFG_AMX_HELPER_H_

#include <string>

#include <mapf/common/logger.h>

#include "bpl_amb_ptr.h"

namespace beerocks {
namespace bpl {

constexpr const char *CONTROLLER_CONFIG_PATH = CONTROLLER_ROOT_DM ".Configuration";
constexpr const char *AGENT_CONFIG_PATH      = AGENT_ROOT_DM ".Configuration";

template <typename T> bool set_controller_config_param(const std::string &name, const T &value)
{
    if (!amb_ptr) {
        MAPF_ERR("set_controller_config_param: " + name + " | controller DM does not exist");
        return false;
    }
    return amb_ptr->set(CONTROLLER_CONFIG_PATH, name, value);
}

template <typename T> bool set_agent_config_param(const std::string &name, const T &value)
{
    if (!amb_ptr) {
        MAPF_ERR("set_agent_config_param: " + name + " | controller DM does not exist");
        return false;
    }
    return amb_ptr->set(AGENT_CONFIG_PATH, name, value);
}

template <typename T> bool read_controller_config_param(const std::string &name, T &value)
{
    if (!amb_ptr) {
        MAPF_ERR("read_controller_config_param: " + name + " | agent DM does not exist");
        return false;
    }
    return amb_ptr->read_param(CONTROLLER_CONFIG_PATH, name, &value);
}

template <typename T> bool read_agent_config_param(const std::string &name, T &value)
{
    if (!amb_ptr) {
        MAPF_ERR("read_agent_config_param: " + name + " | agent DM does not exist");
        return false;
    }
    return amb_ptr->read_param(AGENT_CONFIG_PATH, name, &value);
}

} // namespace bpl
} // namespace beerocks

#endif // _BPL_CFG_AMX_HELPER_H_
