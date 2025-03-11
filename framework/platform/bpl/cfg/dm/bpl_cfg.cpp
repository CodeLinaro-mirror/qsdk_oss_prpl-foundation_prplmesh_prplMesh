/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg.h"

#include "bpl_cfg_amx_wrapper.h"

namespace beerocks {
namespace bpl {

int cfg_get_hostap_iface_steer_vaps(int32_t radio_num,
                                    char hostap_iface_steer_vaps[BPL_LOAD_STEER_ON_VAPS_LEN])
{
    return 0;
}

int cfg_is_enabled() { return 0; }

int cfg_is_master() { return 0; }

int cfg_get_management_mode()
{
    int management_mode{BPL_MGMT_MODE_MULTIAP_AGENT}; // Agent by default

    auto agent_dm = get_agent_dm();
    if (!agent_dm) {
        MAPF_ERR("cfg_get_management_mode: agent_dm does not exist");
    }

    std::string management_mode_str{};
    agent_dm->read_child(management_mode_str, "ManagementMode");

    if (management_mode_str == "Controller+Agent") {
        management_mode = BPL_MGMT_MODE_MULTIAP_CONTROLLER_AGENT;
    } else if (management_mode_str == "Controller") {
        management_mode = BPL_MGMT_MODE_MULTIAP_CONTROLLER;
    } else if (management_mode_str == "Agent") {
        management_mode = BPL_MGMT_MODE_MULTIAP_AGENT;
    } else {
        MAPF_ERR("cfg_get_management_mode: unexpected management_mode");
    }

    return management_mode;
}

int cfg_get_management_mode(std::string &mode) { return 0; }

int cfg_get_operating_mode() { return 0; }

int cfg_get_certification_mode()
{
    int certification_mode{1}; // on by default

    auto agent_dm = get_agent_dm();
    if (!agent_dm) {
        MAPF_ERR("cfg_get_certification_mode: agent_dm does not exist");
    }

    agent_dm->read_child(certification_mode, "CertificationMode");

    return management_mode;
}

int cfg_get_load_steer_on_vaps(int num_of_interfaces,
                               char load_steer_on_vaps[BPL_LOAD_STEER_ON_VAPS_LEN])
{
    return 0;
}

int cfg_get_dcs_channel_pool(int radio_num, char channel_pool[BPL_DCS_CHANNEL_POOL_LEN])
{
    return 0;
}

int cfg_get_stop_on_failure_attempts() { return 0; }

int cfg_is_onboarding() { return 0; }

int cfg_get_rdkb_extensions() { return 0; }

bool cfg_get_band_steering(bool &band_steering) { return false; }

bool cfg_set_band_steering(bool band_steering) { return false; }

bool cfg_get_daisy_chaining_disabled(bool &daisy_chaining_disabled) { return false; }

bool cfg_set_daisy_chaining_disabled(bool daisy_chaining_disabled) { return false; }

bool cfg_get_client_11k_roaming(bool &eleven_k_roaming) { return false; }

bool cfg_set_client_11k_roaming(bool eleven_k_roaming) { return false; }

bool cfg_get_client_roaming(bool &client_roaming) { return false; }

bool cfg_set_client_roaming(bool client_roaming) { return false; }

bool cfg_get_load_balancing(bool &load_balancing) { return false; }

bool cfg_set_load_balancing(bool load_balancing) { return false; }

bool cfg_get_channel_select_task(bool &channel_select_task_enabled) { return false; }

bool cfg_set_channel_select_task(bool channel_select_task_enabled) { return false; }

bool cfg_get_dfs_reentry(bool &dfs_reentry_enabled) { return false; }

bool cfg_set_dfs_reentry(bool dfs_reentry_enabled) { return false; }

bool cfg_get_dfs_task(bool &dfs_task_enabled) { return false; }

bool cfg_set_dfs_task(bool dfs_task_enabled) { return false; }

bool cfg_get_health_check(bool &health_check_enabled) { return false; }

bool cfg_set_health_check(bool health_check_enabled) { return false; }

bool cfg_get_ire_roaming(bool &ire_roaming) { return false; }

bool cfg_set_ire_roaming(bool ire_roaming) { return false; }

bool cfg_get_optimal_path_prefer_signal_strenght(bool &optimal_path_prefer_signal_strenght)
{
    return false;
}

bool cfg_set_optimal_path_prefer_signal_strenght(bool optimal_path_prefer_signal_strenght)
{
    return false;
}

bool cfg_get_diagnostics_measurements(bool &diagnostics_measurements) { return false; }

bool cfg_set_diagnostics_measurements(bool diagnostics_measurements) { return false; }

bool cfg_get_diagnostics_measurements_polling_rate_sec(
    int &diagnostics_measurements_polling_rate_sec)
{
    return false;
}

bool cfg_set_diagnostics_measurements_polling_rate_sec(
    const int &diagnostics_measurements_polling_rate_sec)
{
    return false;
}

int cfg_get_backhaul_params(int *max_vaps, int *network_enabled, int *preferred_radio_band)
{
    return 0;
}

int cfg_get_backhaul_vaps(char *backhaul_vaps_buf, const int buf_len) { return 0; }

int cfg_get_beerocks_credentials(const int radio_dir, char ssid[BPL_SSID_LEN],
                                 char pass[BPL_PASS_LEN], char sec[BPL_SEC_LEN])
{
    return 0;
}

int cfg_get_security_policy() { return 0; }

int cfg_notify_onboarding_completed(const char ssid[BPL_SSID_LEN], const char pass[BPL_PASS_LEN],
                                    const char sec[BPL_SEC_LEN],
                                    const char iface_name[BPL_IFNAME_LEN], const int success)
{
    return 0;
}

int cfg_notify_error(int code, const char data[BPL_ERROR_STRING_LEN]) { return 0; }

int cfg_get_administrator_credentials(char pass[BPL_PASS_LEN]) { return 0; }

bool cfg_get_zwdfs_flag(int &flag) { return false; }

bool cfg_get_best_channel_rank_threshold(uint32_t &threshold) { return false; }

bool cfg_get_persistent_db_enable(bool &enable) { return false; }

bool cfg_get_persistent_db_commit_changes_interval(unsigned int &interval_sec) { return false; }

bool cfg_get_clients_persistent_db_max_size(int &max_size) { return false; }

bool cfg_get_steer_history_persistent_db_max_size(size_t &max_size) { return false; }

bool cfg_get_max_timelife_delay_minutes(int &max_timelife_delay_minutes) { return false; }

bool cfg_get_unfriendly_device_max_timelife_delay_minutes(
    int &unfriendly_device_max_timelife_delay_minutes)
{
    return false;
}

bool cfg_get_persistent_db_aging_interval(int &persistent_db_aging_interval_sec) { return false; }

bool cfg_get_link_metrics_request_interval(std::chrono::seconds &link_metrics_request_interval_sec)
{
    return false;
}

bool cfg_set_link_metrics_request_interval(std::chrono::seconds &link_metrics_request_interval_sec)
{
    return false;
}

bool cfg_get_unsuccessful_assoc_report_policy(bool &unsuccessful_assoc_report_policy)
{
    return false;
}

bool cfg_set_unsuccessful_assoc_report_policy(bool &unsuccessful_assoc_report_policy)
{
    return false;
}

bool cfg_get_unsuccessful_assoc_max_reporting_rate(
    unsigned int &unsuccessful_assoc_max_reporting_rate)
{
    return false;
}

bool cfg_set_unsuccessful_assoc_max_reporting_rate(int &unsuccessful_assoc_max_reporting_rate)
{
    return false;
}

bool bpl_get_lan_interfaces(std::vector<std::string> &lan_iface_list) { return false; }

bool bpl_cfg_get_backhaul_wire_iface(std::string &iface) { return false; }

bool cfg_get_roaming_hysteresis_percent_bonus(int &roaming_hysteresis_percent_bonus)
{
    return false;
}

bool cfg_set_roaming_hysteresis_percent_bonus(int roaming_hysteresis_percent_bonus)
{
    return false;
}

bool cfg_get_steering_disassoc_timer_msec(std::chrono::milliseconds &steering_disassoc_timer_msec)
{
    return false;
}

bool cfg_set_steering_disassoc_timer_msec(std::chrono::milliseconds &steering_disassoc_timer_msec)
{
    return false;
}

bool cfg_get_clients_measurement_mode(eClientsMeasurementMode &clients_measurement_mode)
{
    return false;
}

bool cfg_get_radio_stats_enable(bool &radio_stats_enable) { return false; }

bool cfg_get_rssi_measurements_timeout(int &rssi_measurements_timeout_msec) { return false; }

bool cfg_get_beacon_measurements_timeout(int &beacon_measurements_timeout_msec) { return false; }

bool cfg_get_sta_reporting_rcpi_threshold(unsigned int &sta_reporting_rcpi_threshold)
{
    return false;
}

bool cfg_get_sta_reporting_rcpi_hyst_margin_override_threshold(
    unsigned int &sta_reporting_rcpi_hyst_margin_override_threshold)
{
    return false;
}

bool cfg_get_ap_reporting_channel_utilization_threshold(
    unsigned int &ap_reporting_channel_utilization_threshold)
{
    return false;
}

bool cfg_get_assoc_sta_traffic_stats_inclusion_policy(
    bool &assoc_sta_traffic_stats_inclusion_policy)
{
    return false;
}

bool cfg_get_assoc_sta_link_metrics_inclusion_policy(bool &assoc_sta_link_metrics_inclusion_policy)
{
    return false;
}

bool cfg_get_assoc_wifi6_sta_status_report_inclusion_policy(
    bool &assoc_wifi6_sta_status_report_inclusion_policy)
{
    return false;
}

bool cfg_get_steering_policy(unsigned int &steering_policy) { return false; }

bool cfg_get_channel_utilization_threshold(unsigned int &channel_utilization_threshold)
{
    return false;
}

bool cfg_get_rcpi_steering_threshold(unsigned int &rcpi_steering_threshold) { return false; }

bool get_check_connectivity_to_controller_enable(bool &check_connectivity_enable) { return false; }

bool get_check_indirect_connectivity_to_controller_enable(bool &check_indirect_connectivity_enable)
{
    return false;
}

bool get_controller_discovery_timeout_seconds(std::chrono::seconds &timeout_seconds)
{
    return false;
}

bool get_controller_message_timeout_seconds(std::chrono::seconds &timeout_seconds) { return false; }

bool get_controller_heartbeat_state_timeout_seconds(std::chrono::seconds &timeout_seconds)
{
    return false;
}

bool cfg_get_clients_unicast_measurements(bool &client_unicast_measurements) { return false; }

bool cfg_commit_changes() { return false; }

} // namespace bpl
} // namespace beerocks
