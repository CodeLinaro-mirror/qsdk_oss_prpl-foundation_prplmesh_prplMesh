/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_service_helper.h"

#include "ambiorix.h"
#include "bpl_cfg_status.h"
#include <bpl/bpl_cfg.h>
#include <mapf/common/logger.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

/* ============================================================
 *                        Controller Config
 * ============================================================
 */

namespace beerocks {
namespace bpl {

namespace {

constexpr const char *CONTROLLER_CONFIG_PATH = CONTROLLER_ROOT_DM ".Configuration";

/*
 * Keep Controller configuration local: startup reads happen before the owner event loop starts,
 * while runtime updates run on that loop. A synchronous common WBAPI self-request can block the
 * owner from servicing its request, risking re-entrancy, timeout, or deadlock.
 */
template <typename T> bool set_controller_config_param(const std::string &name, const T &value)
{
    auto dm = BplConfigService::instance().nbapi_dm();
    if (dm && dm->set(CONTROLLER_CONFIG_PATH, name, value)) {
        return true;
    }

    MAPF_ERR("set_controller_config_param: " + name + " | local NBAPI DM write failed");
    return false;
}

template <typename T> bool read_controller_config_param(const std::string &name, T &value)
{
    auto dm = BplConfigService::instance().nbapi_dm();
    if (dm && dm->read_param(CONTROLLER_CONFIG_PATH, name, &value)) {
        return true;
    }

    MAPF_ERR("read_controller_config_param: " + name + " | local NBAPI DM read failed");
    return false;
}

} // namespace

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
    return read_controller_config_param("DiagnosticsMeasurements", diagnostics_measurements);
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

bool cfg_get_band_steering(bool &band_steering)
{
    return read_controller_config_param("BandSteeringEnabled", band_steering);
}

bool cfg_set_band_steering(bool band_steering)
{
    return set_controller_config_param("BandSteeringEnabled", band_steering);
}

bool cfg_get_daisy_chaining_disabled(bool &daisy_chaining_disabled)
{
    return read_controller_config_param("DaisyChainingDisabled", daisy_chaining_disabled);
}

bool cfg_set_daisy_chaining_disabled(bool daisy_chaining_disabled)
{
    return set_controller_config_param("DaisyChainingDisabled", daisy_chaining_disabled);
}

bool cfg_get_client_11k_roaming(bool &eleven_k_roaming)
{
    return read_controller_config_param("Client11kRoamingEnabled", eleven_k_roaming);
}

bool cfg_set_client_11k_roaming(bool eleven_k_roaming)
{
    return set_controller_config_param("Client11kRoamingEnabled", eleven_k_roaming);
}

bool cfg_get_client_roaming(bool &client_roaming)
{
    return read_controller_config_param("ClientRoamingEnabled", client_roaming);
}

bool cfg_set_client_roaming(bool client_roaming)
{
    return set_controller_config_param("ClientRoamingEnabled", client_roaming);
}

bool cfg_get_load_balancing(bool &load_balancing)
{
    return read_controller_config_param("LoadBalancingTaskEnabled", load_balancing);
}

bool cfg_set_load_balancing(bool load_balancing)
{
    return set_controller_config_param("LoadBalancingTaskEnabled", load_balancing);
}

bool cfg_get_channel_select_task(bool &channel_select_task_enabled)
{
    return read_controller_config_param("ChannelSelectionTaskEnabled", channel_select_task_enabled);
}

bool cfg_set_channel_select_task(bool channel_select_task_enabled)
{
    return set_controller_config_param("ChannelSelectionTaskEnabled", channel_select_task_enabled);
}

bool cfg_get_persistent_db_enable(bool &enable)
{
    return read_controller_config_param("PersistentDatabaseEnabled", enable);
}

bool cfg_get_clients_persistent_db_max_size(int &max_size)
{
    return read_controller_config_param("ClientsPersistentDatabaseMaxSize", max_size);
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
    if (!read_controller_config_param("LinkMetricsRequestIntervalSec", interval_sec)) {
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

bool cfg_get_unsuccessful_assoc_max_reporting_rate(
    unsigned int &unsuccessful_assoc_max_reporting_rate)
{
    return read_controller_config_param("UnsuccessfulAssocMaxReportingRate",
                                        unsuccessful_assoc_max_reporting_rate);
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
    return read_controller_config_param("STAReportingRCPIThreshold", sta_reporting_rcpi_threshold);
}

bool cfg_get_sta_reporting_rcpi_hyst_margin_override_threshold(
    unsigned int &sta_reporting_rcpi_hyst_margin_override_threshold)
{
    return read_controller_config_param("STAReportingRCPIHystMarginOverrideThreshold",
                                        sta_reporting_rcpi_hyst_margin_override_threshold);
}

bool cfg_get_ap_reporting_channel_utilization_threshold(
    unsigned int &ap_reporting_channel_utilization_threshold)
{
    return read_controller_config_param("APReportingChannelUtilizationThreshold",
                                        ap_reporting_channel_utilization_threshold);
}

bool cfg_get_assoc_sta_traffic_stats_inclusion_policy(
    bool &assoc_sta_traffic_stats_inclusion_policy)
{
    return read_controller_config_param("AssocSTATrafficStatsInclusionPolicy",
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

int cfg_get_dcs_channel_pool(const BPL_WLAN_IFACE &iface,
                             char channel_pool[BPL_DCS_CHANNEL_POOL_LEN])
{
    static const std::unordered_map<int, std::string> radio_to_param = {
        {eFreqType::FREQ_24G, "DCSChannelPool_24GHz"},
        {eFreqType::FREQ_5G, "DCSChannelPool_5GHz"},
        {eFreqType::FREQ_6G, "DCSChannelPool_6GHz"},
    };

    auto it = radio_to_param.find(iface.freq_type);
    if (it == radio_to_param.end()) {
        MAPF_ERR("cfg_get_dcs_channel_pool: Unknown freq_type");
        return RETURN_ERR;
    }

    std::string config_value = DEFAULT_DCS_CHANNEL_POOL;
    if (!read_controller_config_param(it->second, config_value)) {
        MAPF_ERR("cfg_get_dcs_channel_pool: Failed to read config parameter '" + it->second + "'");
        return RETURN_ERR;
    }

    snprintf(channel_pool, BPL_DCS_CHANNEL_POOL_LEN, "%s", config_value.c_str());
    channel_pool[BPL_DCS_CHANNEL_POOL_LEN - 1] = '\0';

    return RETURN_OK;
}

bool cfg_get_is_traffic_separation_enabled(bool &is_traffic_separation_enabled)
{
    if (!read_controller_config_param("TrafficSeparation.Enable", is_traffic_separation_enabled)) {
        LOG(ERROR) << "failed to read TrafficSeparation.Enable";
        return false;
    }
    return true;
}

bool cfg_get_traffic_separation_private_vid(int &private_vid)
{
    int configured_private_vid = DEFAULT_PRIVATE_VLAN_ID;
    if (!read_controller_config_param("TrafficSeparation.PrivateVID", configured_private_vid)) {
        LOG(ERROR) << "failed to read TrafficSeparation.PrivateVID";
        return false;
    }

    private_vid = configured_private_vid;
    return true;
}

bool cfg_get_traffic_separation_guest_vid(int &guest_vid)
{
    int configured_guest_vid = DEFAULT_GUEST_VLAN_ID;
    if (!read_controller_config_param("TrafficSeparation.GuestVID", configured_guest_vid)) {
        LOG(ERROR) << "failed to read TrafficSeparation.GuestVID";
        return false;
    }

    guest_vid = configured_guest_vid;
    return true;
}
} // namespace bpl
} // namespace beerocks
