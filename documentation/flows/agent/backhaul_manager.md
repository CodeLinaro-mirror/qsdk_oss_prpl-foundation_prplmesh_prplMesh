# Backhaul Manager Flow

This document describes the current Backhaul Manager and Agent startup flow as implemented in:

* `agent/src/beerocks/slave/backhaul_manager/backhaul_manager.h`
* `agent/src/beerocks/slave/backhaul_manager/backhaul_manager.cpp`
* `agent/src/beerocks/slave/son_slave_thread.cpp`

It is intended as a baseline for the PPM-4012 backhaul refactoring work.

# Backhaul Manager FSM

The Backhaul Manager FSM runs every 500 ms from the `Backhaul Manager FSM` timer. The timer calls
`backhaul_fsm_main()` repeatedly while a state asks to continue processing immediately.

The current FSM states are defined by `BackhaulManager::EState`.

| State | Meaning |
| --- | --- |
| `INIT` | Resets backhaul-related database fields and prepares to wait for the Agent enable command. |
| `WAIT_ENABLE` | Idle state before the Agent sends `ACTION_BACKHAUL_ENABLE`. |
| `ENABLED` | Selects the backhaul type and prepares required services. Wired is preferred when a configured WAN interface is up and allowed by certification configuration. Otherwise the FSM selects wireless backhaul. |
| `INIT_HAL` | Wireless-only state. Clears old STA HAL event handlers and starts the WPA attach timeout. |
| `WPA_ATTACH` | Wireless-only state. Creates/attaches STA HAL instances for bSTA interfaces and registers HAL event file descriptors. |
| `WAIT_WPS` | Wireless-only state. Waits for WPS/onboarding or an already-connected STA event. Non-gateway agents time out from this state and restart. |
| `INITIATE_SCAN` | Wireless-only state. Starts active scan on eligible bSTA interfaces. After repeated normal scan failures, falls back to hidden SSID association. |
| `WAIT_FOR_SCAN_RESULTS` | Wireless-only state. Waits for scan results from all pending bSTA interfaces. On complete results, selects the best BSSID and proceeds to association. |
| `WIRELESS_CONFIG_4ADDR_MODE` | Wireless-only state. Disconnects the selected bSTA and enables 4-address mode before association. |
| `WIRELESS_ASSOCIATE_4ADDR` | Wireless-only state. Issues the connect or roam command for the selected BSSID/SSID in 4-address mode. |
| `WIRELESS_ASSOCIATE_4ADDR_WAIT` | Wireless-only state. Waits for the HAL connected event after a 4-address association attempt. Handles timeout, retry, hidden SSID fallback, blacklist updates, and roam failure. |
| `WIRELESS_WAIT_FOR_RECONNECT` | Wireless-only state. Entered when the selected wireless backhaul disconnects while connected or operational. Waits for reconnect before scanning again. |
| `CONNECTED` | Backhaul link exists. Sends `ACTION_BACKHAUL_CONNECTED_NOTIFICATION` to the Agent, clears failure counters and AP blacklist, notifies topology task, then moves to `OPERATIONAL`. |
| `OPERATIONAL` | Steady state after the Agent has been notified that backhaul is connected. Dynamic wired/wireless switching code is currently commented out. |
| `RESTART` | Tears down current backhaul runtime state, disconnects unused HALs, sends `ACTION_BACKHAUL_DISCONNECTED_NOTIFICATION` to the Agent, resets pending bSTA interfaces, and moves to `INIT` or `STOPPED`. |
| `STOPPED` | Terminal error state after the configured stop-on-failure attempts are exhausted. |

`_WIRELESS_START_` and `_WIRELESS_END_` are enum sentinels, not runtime states.

# Main Path

The common path starts as:

```text
INIT
  -> WAIT_ENABLE
  -> ENABLED
```

`WAIT_ENABLE -> ENABLED` is not timer-driven by itself. It happens when the Agent sends
`ACTION_BACKHAUL_ENABLE`.

From `ENABLED`, the FSM chooses one of these paths:

```text
local controller + local gateway
  ENABLED -> CONNECTED

wired backhaul
  ENABLED -> CONNECTED

wireless backhaul
  ENABLED -> INIT_HAL
```

The local-controller/local-gateway path is treated as already connected. It clears the selected
backhaul interface and leaves `connection_type` as `Invalid`.

# Wired Path

The wired path is selected in `ENABLED` when:

* the device is not a local gateway,
* a WAN interface is configured,
* `wan_monitor::initialize()` reports the WAN link as up,
* certification-selected backhaul is empty or `eth`,
* and the WAN interface is part of the bridge.

After that, the Backhaul Manager sets:

* `db->backhaul.connection_type = Wired`
* `db->backhaul.selected_iface_name = db->ethernet.wan.iface_name`

Then it moves directly to:

```text
CONNECTED -> OPERATIONAL
```

# Wireless Path

The normal wireless path is:

```text
ENABLED
  -> INIT_HAL
  -> WPA_ATTACH
  -> WAIT_WPS
```

From `WAIT_WPS`, a HAL `Connected` event sets the selected bSTA interface, marks the connection as
wireless, asks the Agent to enable APs, and moves to:

```text
WAIT_WPS --HAL Connected--> CONNECTED -> OPERATIONAL
```

The scan/association sub-flow is implemented as:

```text
INITIATE_SCAN
  -> WAIT_FOR_SCAN_RESULTS
  -> WIRELESS_CONFIG_4ADDR_MODE
  -> WIRELESS_ASSOCIATE_4ADDR
  -> WIRELESS_ASSOCIATE_4ADDR_WAIT
  --HAL Connected--> CONNECTED -> OPERATIONAL
```

In the current code, `WPA_ATTACH` moves to `WAIT_WPS`; there is no direct timer transition from
`WAIT_WPS` to `INITIATE_SCAN`. The scan/association states are used by retry/recovery and steering
related paths rather than being the normal first step after WPA attach.

Important wireless recovery paths:

* `WAIT_FOR_SCAN_RESULTS` times out back to `INITIATE_SCAN`.
* `WAIT_FOR_SCAN_RESULTS` moves to `INITIATE_SCAN` when no suitable BSSID is selected.
* `WIRELESS_ASSOCIATE_4ADDR` moves to `INITIATE_SCAN` if `connect()` fails.
* `WIRELESS_ASSOCIATE_4ADDR_WAIT` can retry hidden SSID association, return to `INITIATE_SCAN`, or restart after roam failure.
* `OPERATIONAL` or `CONNECTED` moves to `WIRELESS_WAIT_FOR_RECONNECT` when the selected wireless backhaul disconnects.
* `WIRELESS_WAIT_FOR_RECONNECT` moves to `OPERATIONAL` on reconnect, or to `CONNECTED` for local-controller/non-local-gateway mode.
* `WIRELESS_WAIT_FOR_RECONNECT` moves to `INITIATE_SCAN` when reconnect times out.

# External Events And Messages

The Backhaul Manager FSM is affected by both CMDU messages from the Agent and HAL events.

| Source | Event/message | Main effect |
| --- | --- | --- |
| Agent | `ACTION_BACKHAUL_REGISTER_REQUEST` | Backhaul Manager stores the Agent socket and replies with `ACTION_BACKHAUL_REGISTER_RESPONSE`. |
| Agent | `ACTION_BACKHAUL_ENABLE` | Backhaul Manager refreshes radio information, notifies channel scan task, normalizes backhaul config, and moves to `ENABLED`. If already `OPERATIONAL`, it moves back to `CONNECTED` to notify the Agent again. |
| Agent | `ACTION_BACKHAUL_UPDATE_STOP_ON_FAILURE_ATTEMPTS_REQUEST` | Updates configured failure-attempt limit. |
| STA HAL | `Connected` | Handles WPS/association/reconnect success, updates selected bSTA and traffic separation data, sends VLAN policy request to Agent, optionally sends AP enable requests, and advances the FSM. |
| STA HAL | `Disconnected` | Ignores disconnects in `WAIT_WPS`; otherwise handles association failure, operational disconnect, reconnect wait, restart, and platform error notification. |
| STA HAL | scan results | In `WAIT_FOR_SCAN_RESULTS`, removes the reporting iface from the pending set. When all results are in, selects BSSID and continues to 4-address association or retries scan. |

# Agent FSM States Around Backhaul Manager

The Agent state machine is in `slave_thread::agent_fsm()`. The Backhaul Manager related startup
sequence is:

```text
STATE_CONNECT_TO_BACKHAUL_MANAGER
  -> STATE_WAIT_FOR_BACKHAUL_MANAGER_REGISTER_RESPONSE
  -> STATE_JOIN_INIT
  -> STATE_WAIT_FOR_FRONTHAUL_THREADS_JOINED
  -> STATE_BACKHAUL_ENABLE
  -> STATE_SEND_BACKHAUL_MANAGER_ENABLE
  -> STATE_WAIT_FOR_BACKHAUL_MANAGER_CONNECTED_NOTIFICATION
  -> STATE_BACKHAUL_MANAGER_CONNECTED
  -> STATE_WAIT_FOR_AUTO_CONFIGURATION_COMPLETE
  -> STATE_OPERATIONAL
```

| Agent state | Meaning |
| --- | --- |
| `STATE_CONNECT_TO_BACKHAUL_MANAGER` | Creates the CMDU client connection to the Backhaul Manager UDS and sends `ACTION_BACKHAUL_REGISTER_REQUEST`. |
| `STATE_WAIT_RETRY_CONNECT_TO_BACKHAUL_MANAGER` | Retry delay before another Backhaul Manager connection attempt. This state exists in the enum, but no current assignment to this state was found in `son_slave_thread.cpp`. |
| `STATE_WAIT_FOR_BACKHAUL_MANAGER_REGISTER_RESPONSE` | Waits for `ACTION_BACKHAUL_REGISTER_RESPONSE`. The handler moves the Agent to `STATE_JOIN_INIT`. |
| `STATE_JOIN_INIT` | Starts enabled fronthaul processes unless controller-only mode or all radios are disabled. |
| `STATE_WAIT_FOR_FRONTHAUL_THREADS_JOINED` | Waits for AP manager and monitor sockets for each enabled fronthaul radio. On timeout, restarts fronthaul startup. |
| `STATE_BACKHAUL_ENABLE` | Validates that there is a usable wired or wireless backhaul interface unless the device is a local gateway. |
| `STATE_SEND_BACKHAUL_MANAGER_ENABLE` | Sends `ACTION_BACKHAUL_ENABLE` to the Backhaul Manager and waits for connected notification. |
| `STATE_WAIT_FOR_BACKHAUL_MANAGER_CONNECTED_NOTIFICATION` | Waits for `ACTION_BACKHAUL_CONNECTED_NOTIFICATION`. |
| `STATE_BACKHAUL_MANAGER_CONNECTED` | Notifies Platform Manager that backhaul connection is complete, configures transport AL MAC/interfaces, and starts AP autoconfiguration. |

When the Agent receives `ACTION_BACKHAUL_CONNECTED_NOTIFICATION`, it moves to
`STATE_BACKHAUL_MANAGER_CONNECTED` even if it was not in
`STATE_WAIT_FOR_BACKHAUL_MANAGER_CONNECTED_NOTIFICATION`; this is logged as unexpected but accepted.

When the Agent receives `ACTION_BACKHAUL_DISCONNECTED_NOTIFICATION` after `STATE_JOIN_INIT`, it marks
backhaul as disconnected, records the stopped flag from the notification, calls `agent_reset()`, and
notifies the controller connectivity task.
