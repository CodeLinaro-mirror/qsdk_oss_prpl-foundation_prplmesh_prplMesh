/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_amx_helper.h"

#include <bpl/bpl_cfg.h>
#include <mapf/common/logger.h>

#include "bpl_cfg_pwhm.h"

#define PROCESS_MANAGER_DM_PATH "X_PRPLWARE-COM_ProcessManager.PrplMesh"

namespace beerocks {
namespace bpl {

int cfg_get_management_mode()
{
    int management_mode{BPL_MGMT_MODE_MULTIAP_CONTROLLER_AGENT}; // Controller+Agent by default

    std::string management_mode_str{};
    if (cfg_get_management_mode(management_mode_str)) {
        MAPF_ERR("cfg_get_management_mode(): failed to read management_mode, using default");
        return management_mode;
    }

    if (management_mode_str == "Multi-AP-Controller-and-Agent") {
        management_mode = BPL_MGMT_MODE_MULTIAP_CONTROLLER_AGENT;
    } else if (management_mode_str == "Non-Prpl-Controller-and-Agent") {
        management_mode = BPL_MGMT_MODE_NONPRPL_CONTROLLER_AGENT;
    } else if (management_mode_str == "Multi-AP-Controller") {
        management_mode = BPL_MGMT_MODE_MULTIAP_CONTROLLER;
    } else if (management_mode_str == "Multi-AP-Agent") {
        management_mode = BPL_MGMT_MODE_MULTIAP_AGENT;
    } else if (management_mode_str == "Not-Multi-AP") {
        management_mode = BPL_MGMT_MODE_NOT_MULTIAP;
    } else {
        MAPF_ERR("cfg_get_management_mode: unexpected management_mode");
    }

    return management_mode;
}

int cfg_get_management_mode_process_manager(std::string &mode)
{
    auto config_obj = m_ambiorix_cl_ubus.get_object(PROCESS_MANAGER_DM_PATH ".");
    if (!config_obj)
        return RETURN_ERR;
    else if (!config_obj->read_child(mode, "ManagementMode"))
        return RETURN_ERR;

    return RETURN_OK;
}

int cfg_get_management_mode_agent(std::string &mode)
{
    return read_agent_config_param("ManagementMode", mode) ? RETURN_OK : RETURN_ERR;
}

int cfg_get_management_mode(std::string &mode)
{
    int ret = cfg_get_management_mode_process_manager(mode);
    if (ret == RETURN_ERR) {
        MAPF_WARN("cfg_get_management_mode(): failed to read management_mode from process manager "
                  "DM, fallback to agent configuration DM");
        ret = cfg_get_management_mode_agent(mode);
    }

    return ret;
}

int cfg_get_certification_mode_process_manager(int &certification_mode)
{
    auto pm_obj = m_ambiorix_cl_ubus.get_object(PROCESS_MANAGER_DM_PATH ".");
    if (!pm_obj)
        return RETURN_ERR;
    else if (!pm_obj->read_child(certification_mode, "CertificationMode"))
        return RETURN_ERR;

    return RETURN_OK;
}

int cfg_get_certification_mode_agent(int &certification_mode)
{
    bool value = false;
    bool ret   = read_agent_config_param("CertificationMode", value);

    certification_mode = value ? 1 : 0;
    return ret ? RETURN_OK : RETURN_ERR;
}

int cfg_get_certification_mode()
{
    int certification_mode{0}; // off by default

    int ret = cfg_get_certification_mode_process_manager(certification_mode);
    if (ret == RETURN_ERR) {
        MAPF_WARN("cfg_get_certification_mode(): failed to read certification_mode from process "
                  "manager DM, fallback to agent configuration DM");
        ret = cfg_get_certification_mode_agent(certification_mode);
    }

    if (ret == RETURN_ERR)
        MAPF_ERR(
            "cfg_get_certification_mode(): failed to read certification_mode, using default value");

    return certification_mode;
}

} // namespace bpl
} // namespace beerocks
