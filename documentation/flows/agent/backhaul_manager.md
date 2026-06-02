# Backhaul Manager Architecture

This document describes the refactored agent-side Backhaul Manager behavior after the PPM-4012 work. It focuses on wired backhaul candidate handling, wired/wireless fallback, controller discovery, selected backhaul interface reporting, and the behavior that still needs follow-up work.

## Goals

The refactoring keeps the existing wireless onboarding flow but changes the wired side from a single configured WAN interface into a candidate based model.

The intended behavior is:

* Support multiple wired backhaul candidates.
* Support explicit static candidate configuration for certification and controlled test cases.
* Support automatic candidate discovery for normal deployments.
* Prefer wired backhaul when a usable wired candidate exists.
* Fall back to wireless when the selected wired path cannot discover a controller.
* Probe wired candidates while the agent is on wireless, and move back to wired when the controller is proven reachable on a candidate.
* Keep `db->ethernet.wan`, `db->backhaul.selected_iface_name`, CLI status, NBAPI, and link metrics consistent with the selected wired candidate.
* Reject controller discovery on wired interfaces that are not in the candidate list.
* Add loop-risk diagnostics without applying a blocking policy yet.

## Configuration And Data Model

### BackhaulWireDiscoveryMode

`BackhaulWireDiscoveryMode` controls how wired candidates are populated during agent startup:

| Mode | Behavior |
| --- | --- |
| `StaticList` | Use the comma-separated `BackhaulWireInterface` parameter. Empty entries are ignored after trimming. This is mainly used for certification and deterministic tests. |
| `Auto` | Discover Ethernet bridge members automatically and ignore `BackhaulWireInterface`. This is the expected mode for most normal deployments. |

Runtime updates of these parameters are not dynamically applied by the Backhaul Manager. The agent must be restarted for candidate list changes to take effect.

### Candidate Fields

The refactoring uses these database fields:

| Field | Meaning |
| --- | --- |
| `db->ethernet.wan_candidates` | Ordered list of configured or auto-discovered wired backhaul candidates. |
| `db->ethernet.wan` | Compatibility field used by existing code as the currently selected wired backhaul port. It is updated when a wired candidate is selected or detected. |
| `db->backhaul.connection_type` | Current backhaul type: `Wired`, `Wireless`, or `Invalid` for local controller/local gateway cases. |
| `db->backhaul.selected_iface_name` | Current selected backhaul interface name. For wired it is the selected Ethernet port. For wireless it is the selected bSTA interface. |
| `db->statuses.controller_connected` | Controller connectivity state reported through the data model and CLI. This is separate from the Backhaul Manager FSM state. |

### Candidate MAC Resolution

Static and auto candidate discovery may initially provide only interface names. Before a candidate is written to `db->ethernet.wan`, Backhaul Manager resolves its MAC address:

1. If the candidate already has a non-zero MAC, keep it.
2. Otherwise, copy the MAC from the matching `db->ethernet.lan` entry.
3. Otherwise, read it from the system interface.

This is required because several topology, link metrics, BML, and NBAPI paths expect `db->ethernet.wan.mac` to be valid. A selected wired candidate with `00:00:00:00:00:00` causes incorrect topology/link metric output.

## Backhaul Manager FSM

The Backhaul Manager FSM runs periodically from the `Backhaul Manager FSM` timer. The main startup path is:

```text
INIT
  -> WAIT_ENABLE
  -> ENABLED
```

`WAIT_ENABLE -> ENABLED` happens when the Agent sends `ACTION_BACKHAUL_ENABLE`.

From `ENABLED`, the FSM selects one of these paths:

```text
local controller + local gateway
  ENABLED -> CONNECTED -> OPERATIONAL

wired backhaul
  ENABLED -> CONNECTED -> OPERATIONAL

wireless backhaul
  ENABLED -> INIT_HAL -> WPA_ATTACH -> WAIT_WPS
```

The important states are:

| State | Meaning |
| --- | --- |
| `INIT` | Resets runtime backhaul state and waits for enable. |
| `WAIT_ENABLE` | Idle before `ACTION_BACKHAUL_ENABLE`. |
| `ENABLED` | Builds wired runtime candidate state and chooses wired or wireless. |
| `INIT_HAL` | Wireless-only setup before attaching bSTA HALs. |
| `WPA_ATTACH` | Wireless-only state that creates or attaches bSTA HAL instances. |
| `WAIT_WPS` | Wireless-only state waiting for WPS/onboarding or an already connected STA event. |
| `INITIATE_SCAN` and following wireless association states | Existing wireless retry, scan, 4-address configuration, association, and reconnect flow. |
| `CONNECTED` | A backhaul path exists. The Agent is notified and topology/AP setup can continue. |
| `OPERATIONAL` | Steady Backhaul Manager state after the Agent was notified. This is not the same as controller reachability. |
| `RESTART` | Tears down current runtime state, sends disconnect notification, and starts again. |
| `STOPPED` | Terminal failure state after stop-on-failure attempts are exhausted. |

## Wired Candidate Selection

In `ENABLED`, Backhaul Manager builds runtime state for every entry in `db->ethernet.wan_candidates`.

For each candidate it checks:

* the interface name is not empty,
* the interface is up and running,
* the interface is a member of the configured bridge,
* the candidate MAC is resolved before it can become the active `db->ethernet.wan`.

The selection order is the order in `db->ethernet.wan_candidates`. Backhaul Manager deliberately does not iterate the runtime candidate map because map ordering is not stable.

When a candidate is selected, Backhaul Manager sets:

```text
db->ethernet.wan = selected candidate with resolved MAC
db->backhaul.connection_type = Wired
db->backhaul.selected_iface_name = selected candidate iface name
```

Then the FSM moves to:

```text
CONNECTED -> OPERATIONAL
```

Initial wired selection is based only on local link and bridge state. It cannot require proven controller traffic, because the regular AP autoconfiguration and transport flow needs Backhaul Manager to enter the wired path first.

## Wired Controller Discovery

Wired selection only proves that a candidate bridge port is usable locally. Controller reachability is confirmed by AP-Autoconfiguration Response messages.

There are two paths that can report a wired controller response:

* `ap_autoconfiguration_task` receives an AP-Autoconfiguration Response during the normal autoconfiguration flow and sends `WIRED_CONTROLLER_DETECTED` to Backhaul Manager.
* Backhaul Manager receives a broker CMDU directly and handles `AP_AUTOCONFIGURATION_RESPONSE_MESSAGE` before forwarding generic CMDUs.

Both paths call the same Backhaul Manager decision code with the ingress interface index.

Backhaul Manager then:

1. Maps the interface index to an interface name.
2. Verifies that the interface is in `db->ethernet.wan_candidates`.
3. Verifies that it is still up and bridged.
4. Updates selected wired metadata or restarts into wired onboarding.

If the agent is already on wired and the response is received on another wired candidate, Backhaul Manager updates:

```text
db->ethernet.wan
db->backhaul.selected_iface_name
```

It does not restart only to correct the metadata. This makes status and logs reflect the port that actually received the controller response.

If the agent is on wireless and a wired AP-Autoconfiguration Response is received on a candidate, Backhaul Manager remembers that interface as the preferred wired candidate, clears the previous wired-failure guard, and restarts. On the next `ENABLED` pass it tries that preferred candidate instead of simply selecting the first configured candidate.

## Non-Candidate Wired Responses

Static candidate mode is used when the device must ignore wired ports outside the configured candidate list. For that reason, AP-Autoconfiguration Response handling rejects controller responses received on known LAN Ethernet interfaces that are not wired backhaul candidates.

The response is ignored before the generic `CONTROLLER_DISCOVERED` event is sent. This prevents the agent from becoming partially connected through a non-candidate Ethernet path.

This policy does not block wireless or local-bus responses.

## Wired Failure And Wireless Fallback

When Backhaul Manager selects wired, the Agent starts AP autoconfiguration. If controller discovery keeps timing out while the current backhaul type is wired, `ap_autoconfiguration_task` sends `WIRED_ONBOARDING_FAILED`.

`son_slave_thread` forwards this as `ACTION_BACKHAUL_WIRED_ONBOARDING_FAILED`.

Backhaul Manager handles it by:

* logging that wired onboarding failed,
* setting `m_skip_wired_backhaul`,
* restarting if the FSM is already `CONNECTED` or `OPERATIONAL`.

`m_skip_wired_backhaul` is intentionally preserved across the restart. Without it, the next `ENABLED` pass would immediately select the same locally-up wired candidate again and loop forever without trying wireless.

With the guard set, Backhaul Manager falls through to the wireless path:

```text
RESTART -> INIT -> WAIT_ENABLE -> ENABLED -> INIT_HAL -> WPA_ATTACH -> WAIT_WPS
```

## Wired Probe While On Wireless

When the current backhaul type is wireless, Backhaul Manager periodically probes wired candidates with AP-Autoconfiguration Search messages.

The probe is sent only when:

* the device is not controller-only,
* the current backhaul type is `Wireless`,
* the probe interval expired,
* at least one wired candidate is currently up and bridged.

The probe sends one AP-Autoconfiguration Search per radio band. It is only a discovery mechanism; it does not complete onboarding by itself.

If a controller responds on a wired candidate, the normal wired-controller-detected flow restarts Backhaul Manager and prefers the responding candidate.

## Controller Connectivity State

Backhaul Manager `OPERATIONAL` means that Backhaul Manager has selected a local backhaul path and notified the Agent. It does not by itself mean that controller connectivity is still alive.

Controller reachability is tracked by `controller_connectivity_task`:

* It sets `db->statuses.controller_connected = true` when `CONTROLLER_DISCOVERED` is received.
* It sets `db->statuses.controller_connected = false` on initialization, backhaul reconnect, and disconnect paths.
* It reports the value through the data model as `Agent.Info.ControllerConnected`.
* `prplmesh_cli -c status` prints `controller connected: true/false`.

When controller heartbeat monitoring times out, the controller connectivity task sends a reconnect command to Backhaul Manager. Backhaul Manager then restarts the relevant backhaul path.

## Loop-Risk Diagnostics

The transport can report duplicate CMDUs to the broker. Backhaul Manager subscribes to those notifications and classifies the involved interfaces.

The current implementation logs a loop mitigation proposal only. It does not disable an interface, block an endpoint, or change bridge configuration.

This is intentional for the current phase. prplOS is expected to use STP for loop prevention, and any active loop-mitigation policy should be implemented under a separate task after the Backhaul Manager behavior is stable.

## Scenario Behavior

### Boot With Only Wired Available

Expected flow:

```text
INIT -> WAIT_ENABLE -> ENABLED -> CONNECTED -> OPERATIONAL
```

Backhaul Manager selects the first candidate that is up and bridged. AP autoconfiguration then discovers the controller over wired. The controller connectivity task marks `controller_connected=true`.

Useful logs:

```text
Wired backhaul candidate iface=<iface>, up_and_running=1, bridge_member=1
Selected wired backhaul iface=<iface>
Controller discovery response received on wired candidate <iface>
controller_discovered on <band> band
```

### Boot With Wired Link Up But No Controller Reachable

Backhaul Manager initially selects wired because local link and bridge state are valid.

If AP autoconfiguration cannot discover the controller on the wired path, the Agent reports wired onboarding failure. Backhaul Manager restarts with `m_skip_wired_backhaul=true` and falls back to wireless.

Useful logs:

```text
Selected wired backhaul iface=<iface>
Wired onboarding failed, falling back to wireless backhaul
FSM: OPERATIONAL -> RESTART
FSM: ENABLED -> INIT_HAL
```

### Boot With No Usable Wired Candidate

If no candidate is up and bridged, Backhaul Manager does not select wired. It sets the connection type to wireless and starts the bSTA path.

Useful logs:

```text
Wired backhaul candidate <iface> is not up and running
Wired backhaul candidate <iface> is not on bridge <bridge>
FSM: ENABLED -> INIT_HAL
```

### Wireless Onboarding, Then Wired Becomes Available

Backhaul Manager keeps probing wired candidates while the current backhaul is wireless. When a wired AP-Autoconfiguration Response is received on a configured candidate, Backhaul Manager restarts and prefers the responding candidate.

Useful logs:

```text
Sending wired controller probe AP-Autoconfiguration Search, radio_iface=<radio>
Controller AP-Autoconfiguration Response received on wired candidate <iface>.
Restarting backhaul manager for wired onboarding.
Trying preferred wired backhaul candidate <iface> because controller discovery was received on it
Selected wired backhaul iface=<iface>
```

### Wired And Wireless Both Available At Boot

Wired has priority. If at least one wired candidate is up and bridged, Backhaul Manager selects wired first.

Wireless is used only if:

* no wired candidate is usable,
* wired onboarding fails and the failure guard is set,
* certification-selected backhaul forces a wireless path,
* the selected wired candidate becomes unusable before selection.

### Cable Moved Between Candidate Ethernet Ports

If the controller response is later received on a different configured wired candidate while the agent is already on wired, Backhaul Manager updates the selected wired metadata:

```text
Updating selected wired backhaul iface from <old> to <new> based on AP-Autoconfiguration Response
```

No forced restart is performed only to update the selected interface.

Current limitation: live link events are not yet used as the primary trigger for immediate reselection. If the active cable is unplugged, Backhaul Manager may rely on controller connectivity timeout or later controller discovery rather than immediately reacting to the link down event.

### Cable Plugged Into A Non-Candidate Ethernet Port

If `BackhaulWireDiscoveryMode=StaticList` and the controller is reachable on a wired LAN interface that is not in `BackhaulWireInterface`, the AP-Autoconfiguration Response is ignored.

Useful logs:

```text
Ignoring AP-Autoconfiguration Response from controller on non-candidate wired interface <iface>
Controller detected on non-candidate wired interface <iface>
```

The agent should not mark the controller as discovered based on that non-candidate wired response.

### Controller Board Switched Off

Backhaul Manager may remain in `OPERATIONAL` because it still has a selected local backhaul path. Controller loss is detected by `controller_connectivity_task` through heartbeat timeout.

Useful logs:

```text
CONTROLLER_CONNECTIVITY  FSM: CONTROLLER_MONITORING -> WAIT_RESPONSE_FROM_CONTROLLER
Sending heartbeat to Controller with HLE for <n> times
CONTROLLER_CONNECTIVITY  FSM: WAIT_RESPONSE_FROM_CONTROLLER -> CONNECTION_TIMEOUT
Sending ACTION_BACKHAUL_RECONNECT_COMMAND to BH manager
```

`prplmesh_cli -c status` should be checked for:

```text
controller connected: false
```

### WPS PBC While Backhaul Is Not Controller-Connected

WPS PBC auto mode uses the current backhaul/controller state. If the agent is no longer controller-connected, it should use the bSTA path instead of assuming that the previously operational wired path is still valid.

Useful logs:

```text
WPS PBC auto: using bSTA path, backhaul_operational=false, ap_configured=false
```

## Operational Log Checklist

These log patterns are useful when debugging wired backhaul behavior:

| Log pattern | Meaning |
| --- | --- |
| `Wired backhaul iface candidate=<iface>` | Auto mode found a candidate at startup. |
| `Selected auto wired backhaul iface=<iface>` | Compatibility initial value for `db->ethernet.wan` was set from the first auto candidate. This is not necessarily the final active path. |
| `Wired backhaul candidate iface=<iface>, up_and_running=<0/1>, bridge_member=<0/1>` | Backhaul Manager evaluated the candidate in `ENABLED`. |
| `Selected wired backhaul iface=<iface>` | Backhaul Manager selected the active wired candidate. |
| `Controller discovery response received on wired candidate <iface>` | AP autoconfiguration saw controller response on a candidate and notified Backhaul Manager. |
| `Controller AP-Autoconfiguration Response received on wired candidate <iface>` | Backhaul Manager saw proof that the controller is reachable on that candidate. |
| `Updating selected wired backhaul iface from <old> to <new>` | Active wired metadata was corrected based on the response ingress interface. |
| `Wired onboarding failed, falling back to wireless backhaul` | Wired local link was usable, but controller discovery failed; wireless fallback starts. |
| `Ignoring AP-Autoconfiguration Response from controller on non-candidate wired interface <iface>` | Candidate policy rejected a wired response outside the configured list. |
| `controller connected: true/false` | CLI/data-model controller connectivity status. |
| `Loop mitigation proposal (not applied)` | Duplicate CMDU diagnostics detected a possible loop, but no policy action was applied. |

## Current Limitations And Future Work

### Runtime Candidate List Updates

`BackhaulWireDiscoveryMode` and `BackhaulWireInterface` are read at agent startup. Dynamic runtime updates are not handled by Backhaul Manager.

This is acceptable for the current design because production should normally use `Auto`, while `StaticList` is mainly for certification and deterministic wired/wireless switching tests. If this requirement changes, candidate list reload must update `db->ethernet.wan_candidates`, Backhaul Manager runtime candidate state, and any active selection safely.

### Live Link Event Handling

`wan_monitor` is initialized for wired candidates, but live link events are not yet fully used for policy decisions.

Future work should use link events to:

* immediately detect selected wired candidate down,
* restart or reselect without waiting for the controller connectivity timeout,
* prefer another already-up candidate when available,
* keep candidate `up_and_running` and `bridge_member` state fresh.

### Cable Move Handling

The current implementation can update selected wired metadata when a controller response proves that another candidate is carrying controller traffic. It does not actively force a re-onboard only because a cable moved while the old path still appears usable.

Future work can make this more deterministic by combining link events, controller discovery responses, and transport traffic observations.

### Active Backhaul Interface Semantics

For wired, the strongest current signal is the ingress interface of an accepted AP-Autoconfiguration Response. That signal updates `db->ethernet.wan` and `db->backhaul.selected_iface_name`.

However, Ethernet bridge ports can all see multicast topology traffic. Logs that show topology discovery sent or received on several Ethernet interfaces do not necessarily mean those interfaces are all the selected backhaul. The selected backhaul field is the Backhaul Manager policy result, not a guarantee that no other bridge port can observe 1905 traffic.

Future work may add a stronger "active wired traffic owner" signal using transport statistics or validated controller traffic over time.

### Loop Mitigation

The current duplicate-CMDU handling is diagnostic only. It classifies the involved paths and logs a proposal, but it does not disable any interface.

Future work can add a policy layer if STP is not enough or if certification requires deterministic loop mitigation inside prplMesh.

### Candidate Selection Policy

Selection is currently first usable candidate, or the preferred candidate when a controller response identified one. This keeps behavior deterministic.

Future policy could consider speed, configured priority, previous success, or port role. Such a policy should be explicit and logged because it changes which cable wins when several candidates are up at the same time.
