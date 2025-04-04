/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_amx_helper.h"

#include <bcl/beerocks_string_utils.h>
#include <bpl/bpl_cfg.h>
#include <mapf/common/logger.h>

#include <bcl/beerocks_os_utils.h>

#include "bpl_cfg_pwhm.h"

namespace beerocks {
namespace bpl {

/* ============================================================
 *                        Agent Config
 * ============================================================
 */
int cfg_is_master()
{
    switch (cfg_get_management_mode()) {
    case BPL_MGMT_MODE_MULTIAP_CONTROLLER_AGENT:
        return 1;
    case BPL_MGMT_MODE_MULTIAP_CONTROLLER:
        return 1;
    case BPL_MGMT_MODE_MULTIAP_AGENT:
        return 0;
    default:
        return -1;
    }
}

#ifndef KEEP_UCI_GENERAL_OPTIONS

int cfg_get_management_mode()
{
    int management_mode{BPL_MGMT_MODE_MULTIAP_AGENT}; // Agent by default

    std::string management_mode_str{};
    cfg_get_management_mode(management_mode_str);

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

int cfg_get_management_mode(std::string &mode)
{
    return read_agent_config_param("ManagementMode", mode);
}

int cfg_get_certification_mode()
{
    int certification_mode{1}; // on by default
    read_agent_config_param(certification_mode, "CertificationMode");
    return certification_mode;
}

#endif

int cfg_get_stop_on_failure_attempts()
{
    int stop_on_failure_attempts{0};
    read_agent_config_param("StopOnFailureAttempts", stop_on_failure_attempts);
    return stop_on_failure_attempts;
}

int cfg_is_enabled() { return 1; }

int cfg_set_onboarding(int enable) { return 0; }
int cfg_is_onboarding() { return 0; }

bool cfg_get_band_steering(bool &band_steering)
{
    return read_agent_config_param("BandSteeringEnabled", band_steering);
}

bool cfg_set_band_steering(bool band_steering)
{
    return set_agent_config_param("BandSteeringEnabled", band_steering);
}

bool cfg_get_daisy_chaining_disabled(bool &daisy_chaining_disabled)
{
    return read_agent_config_param("DaisyChainingDisabled", daisy_chaining_disabled);
}

bool cfg_set_daisy_chaining_disabled(bool daisy_chaining_disabled)
{
    return set_agent_config_param("DaisyChainingDisabled", daisy_chaining_disabled);
}

bool cfg_get_client_11k_roaming(bool &eleven_k_roaming)
{
    return read_agent_config_param("Client11kRoamingEnabled", eleven_k_roaming);
}

bool cfg_set_client_11k_roaming(bool eleven_k_roaming)
{
    return set_agent_config_param("Client11kRoamingEnabled", eleven_k_roaming);
}

bool cfg_get_client_roaming(bool &client_roaming)
{
    return read_agent_config_param("ClientRoamingEnabled", client_roaming);
}

bool cfg_set_client_roaming(bool client_roaming)
{
    return set_agent_config_param("ClientRoamingEnabled", client_roaming);
}

bool cfg_get_load_balancing(bool &load_balancing)
{
    return read_agent_config_param("LoadBalancingTaskEnabled", load_balancing);
}

bool cfg_set_load_balancing(bool load_balancing)
{
    return set_agent_config_param("LoadBalancingTaskEnabled", load_balancing);
}

bool cfg_get_channel_select_task(bool &channel_select_task_enabled)
{
    return read_agent_config_param("ChannelSelectionTaskEnabled", channel_select_task_enabled);
}

bool cfg_set_channel_select_task(bool channel_select_task_enabled)
{
    return set_agent_config_param("ChannelSelectionTaskEnabled", channel_select_task_enabled);
}

int cfg_notify_onboarding_completed(const char ssid[BPL_SSID_LEN], const char pass[BPL_PASS_LEN],
                                    const char sec[BPL_SEC_LEN],
                                    const char iface_name[BPL_IFNAME_LEN], const int success)
{
    return 0;
}

int cfg_notify_error(int code, const char data[BPL_ERROR_STRING_LEN]) { return 0; }
int cfg_get_administrator_credentials(char pass[BPL_PASS_LEN]) { return 0; }

bool cfg_get_zwdfs_flag(int &flag) { return read_agent_config_param("ZeroWaitDFSFlag", flag); }

bool cfg_get_best_channel_rank_threshold(uint32_t &threshold)
{
    return read_agent_config_param("BestChannelRankThreshold", threshold);
}

bool bpl_cfg_get_backhaul_wire_iface(std::string &iface)
{
    return read_agent_config_param("BackhaulWireInterface", iface);
}

int cfg_get_backhaul_params(int *max_vaps, int *network_enabled, int *preferred_radio_band)
{
    if (max_vaps) {
        //get max_vaps
    }

    if (network_enabled) {
        //get network_enabled
    }

    if (preferred_radio_band) {
        std::string preferred_bh_band{};
        read_agent_config_param("BackhaulBand", preferred_bh_band);

        if (preferred_bh_band.compare("2.4GHz") == 0) {
            *preferred_radio_band = BPL_RADIO_BAND_2G;
        } else if (preferred_bh_band.compare("5GHz") == 0) {
            *preferred_radio_band = BPL_RADIO_BAND_5G;
        } else if (preferred_bh_band.compare("6GHz") == 0) {
            *preferred_radio_band = BPL_RADIO_BAND_6G;
        } else if (preferred_bh_band.compare("auto") == 0) {
            *preferred_radio_band = BPL_RADIO_BAND_AUTO;
        } else {
            MAPF_ERR("cfg_get_backhaul_params: unknown backhaul_band parameter value\n");
            return RETURN_ERR;
        }
    }

    return RETURN_OK;
}

bool cfg_get_clients_measurement_mode(eClientsMeasurementMode &clients_measurement_mode)
{
    int mode_value = 0;
    if (read_agent_config_param("ClientsMeasurementMode", mode_value)) {
        return false;
    }
    if (mode_value < static_cast<int>(eClientsMeasurementMode::DISABLE_ALL) ||
        mode_value >
            static_cast<int>(eClientsMeasurementMode::ONLY_CLIENTS_SELECTED_FOR_STEERING)) {
        MAPF_ERR("cfg_get_clients_measurement_mode: unknown ClientsMeasurementMode value\n");
        return false;
    }

    clients_measurement_mode = static_cast<eClientsMeasurementMode>(mode_value);
    return true;
}

/* ============================================================
 *                        Controller Config
 * ============================================================
 */
bool cfg_get_dfs_reentry(bool &dfs_reentry_enabled)
{
    return read_controller_config_param("DFSReentryEnabled", dfs_reentry_enabled);
}

bool cfg_set_dfs_reentry(bool dfs_reentry_enabled)
{
    return set_controller_config_param("DFSReentryEnabled", dfs_reentry_enabled);
}

bool cfg_get_dfs_task(bool &dfs_task_enabled)
{
    return read_controller_config_param("DFSTaskEnabled", dfs_task_enabled);
}

bool cfg_set_dfs_task(bool dfs_task_enabled)
{
    return set_controller_config_param("DFSTaskEnabled", dfs_task_enabled);
}

bool cfg_get_health_check(bool &health_check_enabled)
{
    return read_controller_config_param("HealthCheckTaskEnabled", health_check_enabled);
}

bool cfg_set_health_check(bool health_check_enabled)
{
    return set_controller_config_param("HealthCheckTaskEnabled", health_check_enabled);
}

bool cfg_get_ire_roaming(bool &ire_roaming)
{
    return read_controller_config_param("IRERoamingEnabled", ire_roaming);
}

bool cfg_set_ire_roaming(bool ire_roaming)
{
    return set_controller_config_param("IRERoamingEnabled", ire_roaming);
}

bool cfg_get_optimal_path_prefer_signal_strenght(bool &optimal_path_prefer_signal_strenght)
{
    return read_controller_config_param("OptimalPathPreferSignalStrength",
                                        optimal_path_prefer_signal_strenght);
}

bool cfg_set_optimal_path_prefer_signal_strenght(bool optimal_path_prefer_signal_strenght)
{
    return set_controller_config_param("OptimalPathPreferSignalStrength",
                                       optimal_path_prefer_signal_strenght);
}

bool cfg_get_diagnostics_measurements(bool &diagnostics_measurements)
{
    return read_controller_config_param("OptimalPathPreferSignalStrenght",
                                        diagnostics_measurements);
}

bool cfg_set_diagnostics_measurements(bool diagnostics_measurements)
{
    return set_controller_config_param("DiagnosticsMeasurements", diagnostics_measurements);
}

bool cfg_get_diagnostics_measurements_polling_rate_sec(
    int &diagnostics_measurements_polling_rate_sec)
{
    return read_controller_config_param("DiagnosticsMeasurementsRate",
                                        diagnostics_measurements_polling_rate_sec);
}

bool cfg_set_diagnostics_measurements_polling_rate_sec(
    const int &diagnostics_measurements_polling_rate_sec)
{
    return set_controller_config_param("DiagnosticsMeasurementsRate",
                                       diagnostics_measurements_polling_rate_sec);
}

int cfg_get_backhaul_vaps(char *backhaul_vaps_buf, const int buf_len) { return 0; }

int cfg_get_security_policy()
{
    // mem_only_psk is not supported by pwhm
    return 0;
}

bool cfg_get_persistent_db_enable(bool &enable)
{
    return read_controller_config_param("PersistentDatabaseEnabled", enable);
}

bool cfg_get_persistent_db_commit_changes_interval(unsigned int &interval_sec)
{
    interval_sec = DEFAULT_COMMIT_CHANGES_INTERVAL_VALUE_SEC;
    return true;
}

bool cfg_get_clients_persistent_db_max_size(int &max_size)
{
    return read_controller_config_param("ClientsPersistentDatabaseMaxSize", max_size);
}

bool cfg_get_steer_history_persistent_db_max_size(size_t &max_size)
{
    max_size = DEFAULT_STEER_HISTORY_PERSISTENT_DB_MAX_SIZE;
    return true;
}

bool cfg_get_max_timelife_delay_minutes(int &max_timelife_delay_minutes)
{
    return read_controller_config_param("MaxTimeLifeDelayMinutes", max_timelife_delay_minutes);
}

bool cfg_get_unfriendly_device_max_timelife_delay_minutes(
    int &unfriendly_device_max_timelife_delay_minutes)
{
    return read_controller_config_param("UnfriendlyDeviceMaxTimeLifeDelayMinutes",
                                        unfriendly_device_max_timelife_delay_minutes);
}

bool cfg_get_persistent_db_aging_interval(int &persistent_db_aging_interval_sec)
{
    return read_controller_config_param("PersistentDatabaseAgingIntervalSec",
                                        persistent_db_aging_interval_sec);
}

bool cfg_set_link_metrics_request_interval(std::chrono::seconds &link_metrics_request_interval_sec)
{
    return set_controller_config_param("LinkMetricsRequestIntervalSec",
                                       link_metrics_request_interval_sec.count());
}

bool cfg_get_link_metrics_request_interval(std::chrono::seconds &link_metrics_request_interval_sec)
{
    int64_t interval_sec = 0;
    if (read_controller_config_param("LinkMetricsRequestIntervalSec", interval_sec)) {
        return false;
    }

    link_metrics_request_interval_sec = std::chrono::seconds(interval_sec);
    return true;
}

bool cfg_get_unsuccessful_assoc_report_policy(bool &unsuccessful_assoc_report_policy)
{
    return read_controller_config_param("UnsuccessfulAssocReportPolicy",
                                        unsuccessful_assoc_report_policy);
}

bool cfg_set_unsuccessful_assoc_report_policy(bool &unsuccessful_assoc_report_policy)
{
    return set_controller_config_param("UnsuccessfulAssocReportPolicy",
                                       unsuccessful_assoc_report_policy);
}

bool cfg_get_unsuccessful_assoc_max_reporting_rate(
    unsigned int &unsuccessful_assoc_max_reporting_rate)
{
    return read_controller_config_param("UnsuccessfulAssocMaxReportingRate",
                                        unsuccessful_assoc_max_reporting_rate);
}

bool cfg_set_unsuccessful_assoc_max_reporting_rate(int unsuccessful_assoc_max_reporting_rate)
{
    return set_controller_config_param("UnsuccessfulAssocMaxReportingRate",
                                       unsuccessful_assoc_max_reporting_rate);
}

bool bpl_get_lan_interfaces(std::vector<std::string> &lan_iface_list)
{
    lan_iface_list =
        beerocks::string_utils::str_split(std::string(DEFAULT_DM_LAN_INTERFACE_NAMES), ' ');
    return true;
}

bool cfg_get_roaming_hysteresis_percent_bonus(int &roaming_hysteresis_percent_bonus)
{
    return read_controller_config_param("RoamingHysteresisPercentBonus",
                                        roaming_hysteresis_percent_bonus);
}

bool cfg_set_roaming_hysteresis_percent_bonus(int roaming_hysteresis_percent_bonus)
{
    return set_controller_config_param("RoamingHysteresisPercentBonus",
                                       roaming_hysteresis_percent_bonus);
}

bool cfg_set_steering_disassoc_timer_msec(std::chrono::milliseconds steering_disassoc_timer_msec)
{
    // Convert std::chrono::milliseconds to int64_t before passing
    int64_t timer_msec = steering_disassoc_timer_msec.count();

    return set_controller_config_param("SteeringDisassociationTimerMSec", timer_msec);
}

bool cfg_get_steering_disassoc_timer_msec(std::chrono::milliseconds &steering_disassoc_timer_msec)
{
    int64_t timer_msec = 0;
    read_controller_config_param("SteeringDisassociationTimerMSec", timer_msec);

    steering_disassoc_timer_msec = std::chrono::milliseconds(timer_msec);
    return true;
}

bool cfg_get_radio_stats_enable(bool &radio_stats_enable)
{
    return read_agent_config_param("RadioStatsEnable", radio_stats_enable);
}

bool cfg_get_rssi_measurements_timeout(int &rssi_measurements_timeout_msec)
{
    return read_controller_config_param("RSSIMeasurementsTimeout", rssi_measurements_timeout_msec);
}

bool cfg_get_beacon_measurements_timeout(int &beacon_measurements_timeout_msec)
{
    return read_controller_config_param("BeaconMeasurementsTimeout",
                                        beacon_measurements_timeout_msec);
}

bool cfg_get_sta_reporting_rcpi_threshold(unsigned int &sta_reporting_rcpi_threshold)
{
    return read_agent_config_param("STAReportingRCPIThreshold", sta_reporting_rcpi_threshold);
}

bool cfg_get_sta_reporting_rcpi_hyst_margin_override_threshold(
    unsigned int &sta_reporting_rcpi_hyst_margin_override_threshold)
{
    return read_agent_config_param("STAReportingRCPIHystMarginOverrideThreshold",
                                   sta_reporting_rcpi_hyst_margin_override_threshold);
}

bool cfg_get_ap_reporting_channel_utilization_threshold(
    unsigned int &ap_reporting_channel_utilization_threshold)
{
    return read_agent_config_param("APReportingChannelUtilizationThreshold",
                                   ap_reporting_channel_utilization_threshold);
}

bool cfg_get_assoc_sta_traffic_stats_inclusion_policy(
    bool &assoc_sta_traffic_stats_inclusion_policy)
{
    return read_agent_config_param("AssocSTATrafficStatsInclusionPolicy",
                                   assoc_sta_traffic_stats_inclusion_policy);
}

bool cfg_get_assoc_sta_link_metrics_inclusion_policy(bool &assoc_sta_link_metrics_inclusion_policy)
{
    return read_controller_config_param("AssocSTALinkMetricsInclusionPolicy",
                                        assoc_sta_link_metrics_inclusion_policy);
}

bool cfg_get_assoc_wifi6_sta_status_report_inclusion_policy(
    bool &assoc_wifi6_sta_status_report_inclusion_policy)
{
    return read_controller_config_param("AssocWiFi6STAStatusReportInclusionPolicy",
                                        assoc_wifi6_sta_status_report_inclusion_policy);
}

bool cfg_get_steering_policy(unsigned int &steering_policy)
{
    return read_controller_config_param("SteeringPolicy", steering_policy);
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

bool cfg_get_clients_unicast_measurements(bool &client_unicast_measurements)
{
    client_unicast_measurements = false;
    return true;
}

int cfg_get_dcs_channel_pool(int radio_num, char channel_pool[BPL_DCS_CHANNEL_POOL_LEN])
{
    return 0;
}

int cfg_get_beerocks_credentials(const int radio_dir, char ssid[BPL_SSID_LEN],
                                 char pass[BPL_PASS_LEN], char sec[BPL_SEC_LEN])
{
    bool result = 0;
    std::string tmp{};

    result |= read_agent_config_param("SSID", tmp);
    strncpy(ssid, tmp.c_str(), BPL_SSID_LEN);

    result |= read_agent_config_param("Security", tmp);
    strncpy(sec, tmp.c_str(), BPL_SEC_LEN);

    if (tmp == "WEP-64" || tmp == "WEP-128") {
        result |= read_agent_config_param("WEPKey", tmp);
    } else {
        result |= read_agent_config_param("Passphrase", tmp);
    }
    strncpy(pass, tmp.c_str(), BPL_PASS_LEN);

    return result ? RETURN_OK : RETURN_ERR;
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

#ifndef KEEP_UCI_GENERAL_OPTIONS
bool cfg_commit_changes() { return true; }
#endif

} // namespace bpl
} // namespace beerocks
