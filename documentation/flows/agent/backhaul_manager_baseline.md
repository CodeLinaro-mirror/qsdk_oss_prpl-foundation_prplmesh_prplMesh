# Backhaul Manager Current Behavior Baseline

## Purpose

This document records the current Backhaul Manager behavior before the backhaul refactor.

It is a baseline for review and migration planning, not a description of the target architecture.

## Main Files

- `agent/src/beerocks/slave/backhaul_manager/backhaul_manager.cpp`
- `agent/src/beerocks/slave/backhaul_manager/backhaul_manager.h`
- `agent/src/beerocks/slave/backhaul_manager/wan_monitor.cpp`
- `common/beerocks/bwl/whm/sta_wlan_hal_whm.cpp`

## Main FSM States

Main backhaul states:

- `INIT`
- `WAIT_ENABLE`
- `ENABLED`
- `CONNECTED`
- `OPERATIONAL`
- `RESTART`
- `STOPPED`

Wireless-specific states:

- `INIT_HAL`
- `WPA_ATTACH`
- `WAIT_WPS`
- `INITIATE_SCAN`
- `WAIT_FOR_SCAN_RESULTS`
- `WIRELESS_CONFIG_4ADDR_MODE`
- `WIRELESS_ASSOCIATE_4ADDR`
- `WIRELESS_ASSOCIATE_4ADDR_WAIT`
- `WIRELESS_WAIT_FOR_RECONNECT`

## Current Wired Onboarding

At `ENABLED`, Backhaul Manager:

1. Reads the list of interfaces in the bridge.
2. Initializes `wan_monitor` on `db->ethernet.wan.iface_name`.
3. If the configured wired interface is up and no certification override selects wireless, it
   prefers wired backhaul.
4. It then requires the configured wired interface to already be part of the bridge.
5. On success, it sets:
   - `db->backhaul.connection_type = Wired`
   - `db->backhaul.selected_iface_name = db->ethernet.wan.iface_name`

Current behavior is based on one configured wired interface only.

## Current Wireless Onboarding

If wired is not selected, Backhaul Manager:

1. Selects wireless backhaul.
2. Creates and attaches STA HAL instances for available backhaul STA interfaces.
3. Waits for WPS or initiates scanning.
4. Collects scan results and selects a BSSID.
5. Configures 4-address mode and connects.
6. On `Connected`, it updates backhaul state, applies VLAN policy, enables APs, and moves to
   `CONNECTED`.

## Current Wireless Reconnect

If the selected wireless backhaul disconnects while operational:

1. Backhaul Manager moves to `WIRELESS_WAIT_FOR_RECONNECT`.
2. It waits for a reconnect event for a fixed timeout.
3. If reconnect happens, it returns to `CONNECTED` or `OPERATIONAL`.
4. If reconnect does not happen, it falls back to scanning.

## Current Certification Overrides

Certification flows use:

- `dev_reset_default`
- `dev_set_config`

`dev_set_config` can force:

- `eth`
- a specific wireless radio RUID

These overrides affect the onboarding decision at `ENABLED`.

## Current Side Effects

On backhaul connect, Backhaul Manager may:

- detach unused wireless STA HAL instances
- update backhaul BSSID
- send `ACTION_BACKHAUL_CONNECTED_NOTIFICATION`
- trigger topology discovery through `TopologyTask::AGENT_DEVICE_INITIALIZED`

On wireless connect, Backhaul Manager may:

- add the wireless STA interface to the bridge
- apply VLAN policy
- enable APs

On disconnect/restart, Backhaul Manager may:

- send `ACTION_BACKHAUL_DISCONNECTED_NOTIFICATION`
- clear handlers and HAL instances
- reset onboarding state

## Known Gaps

- Single configured wired backhaul interface
- Ethernet-first onboarding is hardcoded
- No complete runtime wired/wireless switching
- No explicit loop handling for simultaneous wired and wireless backhaul
- Partial use of pwhm Endpoint state
- No PreferredBackhauls policy implementation
- Current active-link model is still single-link oriented
