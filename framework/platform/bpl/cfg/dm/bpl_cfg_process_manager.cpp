/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_service_helper.h"
#include "bpl_cfg_status.h"

#include <bpl/bpl_cfg.h>
#include <mapf/common/logger.h>

namespace beerocks {
namespace bpl {

namespace {

constexpr const char *PROCESS_MANAGER_PATH = "X_PRPLWARE-COM_ProcessManager.PrplMesh.";

template <typename T> bool read_process_manager_param(const std::string &name, T &value)
{
    return read_param_via_common_socket(PROCESS_MANAGER_PATH, name, value);
}

} // namespace

int cfg_get_management_mode_process_manager(std::string &mode)
{
    return read_process_manager_param("ManagementMode", mode) ? RETURN_OK : RETURN_ERR;
}

int cfg_get_management_mode(std::string &mode)
{
    int ret = cfg_get_management_mode_process_manager(mode);
    if (ret == RETURN_ERR) {
        MAPF_WARN("cfg_get_management_mode(): failed to read management_mode from process manager "
                  "DM, fallback to agent info DM");
        ret = cfg_get_management_mode_agent_info(mode);
    }

    return ret;
}

int cfg_get_management_mode()
{
    int management_mode{BPL_MGMT_MODE_MULTIAP_CONTROLLER_AGENT}; // Controller+Agent by default

    std::string management_mode_str{};
    if (cfg_get_management_mode(management_mode_str)) {
        MAPF_WARN("cfg_get_management_mode(): management_mode unavailable, using default "
                  "BPL_MGMT_MODE_MULTIAP_CONTROLLER_AGENT");
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

int cfg_get_certification_mode_process_manager(int &certification_mode)
{
    return read_process_manager_param("CertificationMode", certification_mode) ? RETURN_OK
                                                                               : RETURN_ERR;
}

int cfg_get_certification_mode()
{
    int certification_mode{BPL_CERTIFICATION_MODE_OFF};

    if (cfg_get_certification_mode_process_manager(certification_mode) == RETURN_ERR) {
        MAPF_ERR(
            "cfg_get_certification_mode(): failed to read certification_mode, using default value");
    }

    return certification_mode;
}

} // namespace bpl
} // namespace beerocks
