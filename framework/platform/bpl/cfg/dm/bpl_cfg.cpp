/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_service_helper.h"
#include "bpl_cfg_status.h"
#include <bcl/beerocks_string_utils.h>
#include <bpl/bpl_cfg.h>
#include <mapf/common/logger.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

namespace beerocks {
namespace bpl {

bool bpl_cfg_get_airties_cloud_credentials(std::string &client_id, std::string &client_secret)
{
    constexpr const char *cloud_comm_path = "X_AIRTIES_Obj.CloudComm.";

    auto cloud_comm_obj = get_object_via_common_socket(cloud_comm_path);
    if (!cloud_comm_obj || !cloud_comm_obj->read_child(client_id, "ClientID") ||
        !cloud_comm_obj->read_child(client_secret, "ClientPassword")) {
        client_id.clear();
        client_secret.clear();
        LOG(ERROR) << "Failed to read AirTies cloud credentials from " << cloud_comm_path;
        return false;
    }

    return true;
}

int cfg_is_master()
{
    switch (cfg_get_management_mode()) {
    case BPL_MGMT_MODE_MULTIAP_CONTROLLER_AGENT:
        return 1;
    case BPL_MGMT_MODE_NONPRPL_CONTROLLER_AGENT:
        return 1;
    case BPL_MGMT_MODE_MULTIAP_CONTROLLER:
        return 1;
    case BPL_MGMT_MODE_MULTIAP_AGENT:
        return 0;
    default:
        return -1;
    }
}

int cfg_is_non_prplmesh_controller()
{
    return cfg_get_management_mode() == BPL_MGMT_MODE_NONPRPL_CONTROLLER_AGENT;
}

int cfg_set_onboarding(int enable) { return 0; }
int cfg_is_onboarding() { return 0; }

int cfg_notify_onboarding_completed(const char ssid[BPL_SSID_LEN], const char pass[BPL_PASS_LEN],
                                    const char sec[BPL_SEC_LEN], const std::string &iface_name,
                                    const int success)
{
    return 0;
}

int cfg_notify_error(int code, const char data[BPL_ERROR_STRING_LEN]) { return 0; }
int cfg_get_administrator_credentials(char pass[BPL_PASS_LEN]) { return 0; }

int cfg_get_backhaul_vaps(char *backhaul_vaps_buf, const int buf_len) { return 0; }

int cfg_get_security_policy()
{
    // mem_only_psk is not supported by pwhm
    return 0;
}

bool get_ruid_chipset_vendor(const sMacAddr &ruid, std::string &chipset_vendor)
{
    (void)ruid;
    chipset_vendor = "prplmesh";
    return true;
}

bool get_max_prioritization_rules(uint32_t &max_prioritization_rules)
{
    // On EasyMesh standard 9.1 it is said that a Multi-AP Agent that implements Profile-3, need to:
    // "Set Max Total Number Service Prioritization Rules to one".
    // This requirement will probably change on future version of the standard.
    max_prioritization_rules = 1;
    return true;
}

bool cfg_get_persistent_db_commit_changes_interval(unsigned int &interval_sec)
{
    interval_sec = DEFAULT_COMMIT_CHANGES_INTERVAL_VALUE_SEC;
    return true;
}

bool cfg_get_steer_history_persistent_db_max_size(size_t &max_size)
{
    max_size = DEFAULT_STEER_HISTORY_PERSISTENT_DB_MAX_SIZE;
    return true;
}

bool cfg_get_channel_utilization_threshold(unsigned int &channel_utilization_threshold)
{
    channel_utilization_threshold = DEFAULT_CHANNEL_UTILIZATION_THRESHOLD;
    return true;
}

bool cfg_get_rcpi_steering_threshold(unsigned int &rcpi_steering_threshold)
{
    rcpi_steering_threshold = DEFAULT_RCPI_STEERING_THRESHOLD;
    return true;
}

bool get_check_connectivity_to_controller_enable(bool &check_connectivity_enable)
{
    check_connectivity_enable = DEFAULT_CHECK_CONNECTIVITY_TO_CONTROLLER_ENABLE;
    return true;
}

bool get_check_indirect_connectivity_to_controller_enable(bool &check_indirect_connectivity_enable)
{
    check_indirect_connectivity_enable = DEFAULT_CHECK_INDIRECT_CONNECTIVITY_TO_CONTROLLER_ENABLE;
    return true;
}

bool get_controller_discovery_timeout_seconds(std::chrono::seconds &timeout_seconds)
{
    timeout_seconds = std::chrono::seconds(DEFAULT_CONTROLLER_DISCOVERY_TIMEOUT_SEC);
    return true;
}

bool get_controller_message_timeout_seconds(std::chrono::seconds &timeout_seconds)
{
    timeout_seconds = std::chrono::seconds(DEFAULT_CONTROLLER_MESSAGE_TIMEOUT_SEC);
    return true;
}

bool get_controller_heartbeat_state_timeout_seconds(std::chrono::seconds &timeout_seconds)
{
    timeout_seconds = std::chrono::seconds(DEFAULT_CONTROLLER_HEARTBEAT_STATE_TIMEOUT_SEC);
    return true;
}

bool bpl_cfg_get_mandatory_interfaces(std::string &mandatory_interfaces)
{
    // For pHWM implementation this feature is not used.
    // This means we will not create son_slaves for currently-not-existing interfaces.
    mandatory_interfaces.clear();

    return true;
}

bool bpl_cfg_get_monitored_BSSs_by_radio_iface(const std::string &iface,
                                               std::set<std::string> &monitored_BSSs)
{
    return true;
}

bool bpl_cfg_get_wpa_supplicant_ctrl_path(const std::string &iface, std::string &wpa_ctrl_path)
{
    wpa_ctrl_path = "/var/run/wpa_supplicant/" + iface;
    return true;
}

bool bpl_cfg_get_hostapd_ctrl_path(const std::string &iface, std::string &hostapd_ctrl_path)
{
    hostapd_ctrl_path = "/var/run/hostapd/" + iface;
    return true;
}

int cfg_get_hostap_iface_steer_vaps(int32_t radio_num,
                                    char hostap_iface_steer_vaps[BPL_LOAD_STEER_ON_VAPS_LEN])
{
    return 0;
}

int cfg_get_load_steer_on_vaps(int num_of_interfaces,
                               char load_steer_on_vaps[BPL_LOAD_STEER_ON_VAPS_LEN])
{
    if (num_of_interfaces < 1) {
        MAPF_ERR("invalid input: max num_of_interfaces value < 1");
        return RETURN_ERR;
    }

    if (!load_steer_on_vaps) {
        MAPF_ERR("invalid input: load_steer_on_vaps is NULL");
        return RETURN_ERR;
    }

    std::string load_steer_on_vaps_str;
    char hostap_iface_steer_vaps[BPL_LOAD_STEER_ON_VAPS_LEN] = {0};
    for (int index = 0; index < num_of_interfaces; index++) {
        if (cfg_get_hostap_iface_steer_vaps(index, hostap_iface_steer_vaps) == RETURN_OK) {
            if (std::string(hostap_iface_steer_vaps).length() > 0) {
                if (!load_steer_on_vaps_str.empty()) {
                    load_steer_on_vaps_str.append(",");
                }
                load_steer_on_vaps_str.append(std::string(hostap_iface_steer_vaps));
                MAPF_DBG("adding interface " << hostap_iface_steer_vaps
                                             << " to the steer on vaps list");
            }
        }
    }

    if (load_steer_on_vaps_str.empty()) {
        MAPF_DBG("steer on vaps list is not configured");
        return RETURN_OK;
    }

    mapf::utils::copy_string(load_steer_on_vaps, load_steer_on_vaps_str.c_str(),
                             BPL_LOAD_STEER_ON_VAPS_LEN);

    return RETURN_OK;
}

bool cfg_commit_changes() { return true; }
} // namespace bpl
} // namespace beerocks
