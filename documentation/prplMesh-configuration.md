# Introduction

prplMesh can be configured through different options.
Depending on the platform, those options are set in different ways (see [Platform-specific files](#platform-specific-files)).

For most platforms, changes in options are only taken into account after prplMesh is restarted.

To change settings at runtime without restarting prplMesh, see also [Configuration via NBAPI](https://gitlab.com/prpl-foundation/prplmesh/prplMesh/-/wikis/prplMesh-Northbound-API#configuration-via-nbapi)

## All options

For each platform, a default configuration file is provided.

Some optional options are not included in default configuration files, and instead have default values within prplMesh.
When a platform-specific configuration file specifies a value for a option, it overrides the default value used by prplMesh.

The tables in the sections below outlines all the existing options, across all platforms.

### UCI Configuration Parameters

Currently, only general configuration parameters that are common for both the Controller and Agent are kept in UCI.

| Option               | Type   | Default Value    | Description                                                   |
| -------------------- | ------ | ---------------- | ------------------------------------------------------------- |
| `management_mode`    | string | Controller+Agent | Multi-AP mode (`Agent`, `Controller`, `Controller+Agent`).    |
| `certification_mode` | int    | 1                | Enables certification-specific features (e.g., UCC listener). |

---

### DataModel Configuration Parameters

There are configuration parameters that are specific for the Controller or Agent.

---

#### Controller Configuration

All Controller config options are located in path:  
`X_PRPLWARE-COM_Controller.Configuration`

| Option                                        | Type   | Default Value | Description                                                                 |
| --------------------------------------------- | ------ | ------------- | --------------------------------------------------------------------------- |
| `PersistentDatabaseEnabled`                   | bool   | false         | Enables persistent database for storing clients. Requires platform support. |
| `ClientsPersistentDatabaseMaxSize`            | uint32 | 256           | Maximum number of clients in persistent database.                           |
| `MaxTimeLifeDelayMinutes`                     | uint32 | 525600        | Maximum lifetime (in minutes) of clients in persistent database.            |
| `UnfriendlyDeviceMaxTimeLifeDelayMinutes`     | uint32 | 1440          | Maximum lifetime (in minutes) of 'unfriendly' clients.                      |
| `PersistentDatabaseAgingIntervalSec`          | uint32 | 3600          | Aging interval for the persistent database in seconds.                      |
| `BandSteeringEnabled`                         | bool   | false         | Enables band steering to optimize STA connection.                           |
| `IRERoamingEnabled`                           | bool   | true          | Enables IRE roaming in the optimal path task.                               |
| `ClientRoamingEnabled`                        | bool   | false         | Enables client roaming in the optimal path task.                            |
| `Client11kRoamingEnabled`                     | bool   | true          | Uses 11k measurements for roaming decisions in the optimal path task.       |
| `SteeringCurrentBonus`                        | uint32 | N/A           | Bonus applied to current BSS selection (`phy_rate` or `rssi`).              |
| `SteeringDisassociationTimerMSec`             | uint32 | 200           | Disassociation timer used in client steering task.                          |
| `LinkMetricsRequestIntervalSec`               | uint32 | 60            | Link metric request interval in seconds.                                    |
| `ChannelSelectionTaskEnabled`                 | bool   | false         | Enables channel selection task.                                             |
| `DynamicChannelSelectionTaskEnabled`          | bool   | false         | Enables dynamic channel selection task.                                     |
| `BackhaulOptimizationEnabled`                 | bool   | false         | Enables IRE network optimization task.                                      |
| `LoadBalancingTaskEnabled`                    | bool   | false         | Enables load balancing via client steering between agents.                  |
| `OptimalPathPreferSignalStrength`             | bool   | false         | Prefer signal strength (`rssi`) over `phy_rate` for optimal path decisions. |
| `HealthCheckTaskEnabled`                      | bool   | false         | Enables health check task.                                                  |
| `StatisticsPollingTaskEnabled`                | bool   | false         | Enables statistics polling task.                                            |
| `StatisticsPollingRateSec`                    | uint32 | 1             | Polling interval (seconds) for statistics collection.                       |
| `DFSReentryEnabled`                           | bool   | false         | Currently not used; placeholder for DFS reentry.                            |
| `DFSTaskEnabled`                              | bool   | false         | Currently not used; placeholder for DFS task.                               |
| `DaisyChainingDisabled`                       | bool   | false         | Prevents sending credentials to extenders if enabled.                       |
| `DiagnosticsMeasurements`                     | bool   | true          | Enables diagnostics measurement via statistics polling.                     |
| `DiagnosticsMeasurementsRate`                 | uint32 | 10            | Measurement request interval (seconds) to known agents.                     |
| `UnsuccessfulAssocReportPolicy`               | bool   | true          | Enables reporting for unsuccessful STA associations.                        |
| `UnsuccessfulAssocMaxReportingRate`           | uint32 | 30            | Max reporting rate for unsuccessful associations (attempts per minute).     |
| `RoamingHysteresisPercentBonus`               | uint32 | 10            | Roaming hysteresis bonus in percent for optimal path decisions.             |
| `RSSIMeasurementsTimeout`                     | uint32 | 10000         | Timeout for RSSI measurement responses (milliseconds).                      |
| `BeaconMeasurementsTimeout`                   | uint32 | 6000          | Timeout for beacon measurement responses (milliseconds).                    |
| `STAReportingRCPIThreshold`                   | uint32 | 0             | RCPI threshold (dB) for STA metrics report triggering.                      |
| `STAReportingRCPIHystMarginOverrideThreshold` | uint32 | 0             | Override for RCPI hysteresis margin (dB).                                   |
| `APReportingChannelUtilizationThreshold`      | uint32 | 0             | Channel utilization threshold (%) for AP metrics reports.                   |
| `SteeringPolicy`                              | uint32 | 0             | Steering policy type.                                                       |
| `AssocSTALinkMetricsInclusionPolicy`          | bool   | false         | Include Associated STA Link Metrics TLV in reports.                         |
| `AssocSTATrafficStatsInclusionPolicy`         | bool   | false         | Include Associated STA Traffic Stats TLV in reports.                        |
| `AssocWiFi6STAStatusReportInclusionPolicy`    | bool   | false         | Include Wi-Fi 6 STA Status Report TLV in reports.                           |

---

#### Agent Configuration

All Agent config options are located in path:  
`X_PRPLWARE-COM_Agent.Configuration`

| Option                     | Type   | Default Value | Description                                                                 |
| -------------------------- | ------ | ------------- | --------------------------------------------------------------------------- |
| `BestChannelRankThreshold` | uint32 | 0             | Threshold for best channel rank in channel selection task.                  |
| `BackhaulBand`             | string | auto          | Preferred band for wireless backhaul (`2.4GHz`, `5GHz`, `auto`).            |
| `BackhaulWireInterface`    | csv_string | wan       | Comma-separated network interfaces used for wired backhaul.                 |
| `StopOnFailureAttempts`    | uint32 | 1             | Retry attempts before stopping agent due to failures. 0 disables retries.   |
| `ZeroWaitDFSFlag`          | uint32 | 0             | Bitwise flags controlling Zero Wait DFS features.                           |
| `ClientsMeasurementMode`   | uint32 | 1             | Client measurement mode: 0-disabled, 1-enabled for all, 2-selected clients. |
| `MandatoryInterfaces`      | string |               | Comma-separated list of wireless interfaces (empty = use all).              |
| `ExcludeHostapInterface`   | bool   | false         | Excludes dummy interfaces from credential propagation.                      |
| `SSID`                     | string | test_ssid     | SSID for network credentials.                                               |
| `Security`                 | string | WPA2-Personal | Security type for network credentials.                                      |
| `Passphrase`               | string | 123456789a    | Passphrase for network credentials.                                         |
| `WEPKey`                   | string | 123456789a    | WEP key for network credentials.                                            |


### A short overview of controller tasks

Thanks to the code modularity, it is possible to dynamically enable/disable certain controller features that are implemented by controller tasks.
The long-term goal is to expose an API that allows performing the same operations by an external entity, and have the controller act as an 1905 adaptation layer.

PPM-2155 implements the API that allows configuring the controller features.
Below is a short overview of controller tasks that are either completely or partially disabled by flags exposed by this API.

##### Association handling task:

On-demand
Uses beerocks messages, fills new station 11k capability by asking for a beacon measurement report, asks the station for a RSSI measurement.
Because of its low complexity, this task is not worth disabling.

Settings consumed by this tasks :
   if (settings_client_11k_roaming) : request 11k beacon measurement report from STA and fill STA 11k capabilities based on content of the response or absence thereof.
   if (settings_client_band_steering && settings_client_optimal_path_roaming ) : request an RSSI measurement from STA with a beerocks_message::cACTION_CONTROL_CLIENT_RX_RSSI_MEASUREMENT_REQUEST.


##### Channel selection task

Periodic
Settings consumed by this task :
    settings_dfs_reentry : consumed by this task; in a dead loop of the FSM : missing the event to enter the loop; this flag controls band steering the clients away from the 5GHz AP in order to perform a DFS clear on that AP, before moving the clients back.
    settings_client_band_steering : second flag, along with dfs_reentry, that enables/disables steering clients away from 5GHz AP.
    settings_ire_roaming : start a ire_network_optimization_task when this flag is set.


##### Client steering task

On-demand
Settings consumed by this task :
    m_steer_restricted : not strictly a setting, a runtime flag for the client steering task; perform the steering but do not update statistics except for steer attempt stats. Only set to true in one call, from channel_selection_task, when steering STAs away from 5GHz, before a DFS clear.


##### Dynamic channel selection task

Periodic
Complex FSM, two state variables with their own FSMs, plus two operation queues, that are used to feed the two FSMs; the two queues are filled by beerocks messages (BML library); some are available in the beerocks_cli, others not.
No settings from database, no task spawn; see PPM-2155 for an attempt to illustrate the FSM of this task


##### IRE Network Optimization

On-demand
Iterates over the Extender hierarchy and spawns optimal_path_tasks for each IRE; (wait 30sec between each spawn).

Launched by Channel selection task if (database.settings_ire_roaming())
Launched by a beerocks_cli command


##### Link metric task

Periodic
Periodic task launched by the controller, hidden behind (database.config.management_mode != BPL_MGMT_MODE_NOT_MULTIAP).

Sends a 1905 Link Metric Query to all agents registered in the database with an interval of (database.config.link_metrics_request_interval_seconds [seconds).

Also, processes the 1905 Link Metric Responses and stores the reported stats in the database.


##### Load balancer task

On-demand
Started by two beerocks messages, ACTION_CLI_LOAD_BALANCER_TASK, sent by beerocks_cli , and ACTION_CONTROL_HOSTAP_LOAD_MEASUREMENT_NOTIFICATION, sent by an Agent.
Eventually (code is commented out under a TODO) would use the client_steering_task to move a STA from a more busy AgentA to a less busy AgentB; will need to check the flag steering_enabled before creating the client_steering_task.

Note: the task is attached to an Agent, identified by its 1905 AL MAC, let's call it AgentOrigin. The ACTION_CONTROL_HOSTAP_LOAD_MEASUREMENT_NOTIFICATION is sent by AgentOrigin when ChannelLoad or StaCount cross a configured threshold,
i.e. the agent is under a big load.
The load_balancer_task finds the busiest AgentA and least busy AgentB among the children of AgentOrigin, i.e., in the subtree rooted on AgentOrigin. It then 'would' attempt to steer a STA of AgentA to AgentB (when the code would be un-commented).
The total number of devices (agents and stations) in the subtree rooted on AgentOrigin stays the same in this case.


##### Network health check task

On-demand
Started by controller under the database.settings_health_check flag.
Removes agents from the DB, that were last seen more than 120sec ago (delta between timestamp of current task iteration and last_seen timestamp in the database).
Removes STAs from the DB that were last seen more than 140sec ago after attempting to update the last_seen with a beerocks_message::cACTION_CONTROL_ARP_QUERY_REQUEST / beerocks_message::ACTION_CONTROL_ARP_QUERY_RESPONSE sequence.


##### Optimal path task

On-demand
If !(settings_client_band_steering && settings_client_optimal_path_roaming && settings_client_11k_roaming || settings_ire_roaming) exits early. First three options are checked if launched for a STA. Last option, ire_roaming, is checked if the task is launched for an Agent.

Launched by channel_selection_task || ire_network_optimization_task || beerocks_cli command | association_handling_task.

In channel_selection_task, it should currently not be triggered since the FSM loop that launches this task is not activated.

Association_handling_task systematically launches this task for a Client that doesn't have the handoff flag set (the flag is set by the steering / btm_request tasks for stations that are currently being steered).

Setting consumed:
    settings_client_optimal_path_roaming_prefer_signal_strength: choosing between "RSSI" or "Estimated Phy Rate" choice for best parent; ("RSSI" is based on an 11k beacon measurement report if the station supports it, on measurements from the Agent APs otherwise).


##### Statistics polling task

Periodic
Launched by controller (settings_diagnostics_measurements) or via a beerocks_cli command.
Every `diagnostics_measurements_polling_rate_sec` seconds sends a beerocks_message::cACTION_CONTROL_HOSTAP_STATS_MEASUREMENT_REQUEST to each agent.


##### Topology task

Periodic
Launched by the controller. Handles the 1905 topology notification and pushes STA_CONNECTED event for DHCP task, client steering task, and btm request task.
Starts the association handling task.

## Platform-specific files

### Linux builds (dummy)

For the "dummy" variant, the prplMesh options are set in the `prplmesh_platform_db` file.

This file is composed of key and values, separated by an equal sign (`=`).
Trailing whitespace and whitespace at the beginning of a line is removed.
When a number sign (`#`) is encountered, everything that follows until the end of the line is ignored (it can thus be used to include comments in the file).

A default version of the file is included in this repository: `framework/platform/bpl/platform_db/prplmesh_platform_db`).

The following variables are specific to Linux builds:
| Option                                    | Type   | Required | Default | Description                                   |
| ----------------------------------------- | ------ | -------- | ------- | --------------------------------------------- |
| hostapd_ctrl_path_<interface_name>        | string | yes      | *none*  | Path to the hostapd's control sockets.        |
| wpa_supplicant_ctrl_path_<interface_name> | string | yes      | *none*  | Path to the wpa_supplicant's control sockets. |

### RDK-B builds

For RDK-B, a `prplmesh_db` file is used.

Unlike for dummy builds, this file is configured at build time using CMAKE options.

The default version is in `framework/platform/bpl/db/uci/prplmesh_db.in`.

The format of this file is the same format as for OpenWrt's UCI options (see prplWrt builds below).

### prplWrt builds

For prplWrt, the [standard UCI system](https://openwrt.org/docs/guide-user/base-system/uci) is used.

A default configuration file also exists, and it's stored [alongside the prplMesh package in the prpl feed](https://gitlab.com/prpl-foundation/prplwrt/feed-prpl/-/blob/prplwrt/prplmesh/files/etc/uci-defaults/prplmesh).
