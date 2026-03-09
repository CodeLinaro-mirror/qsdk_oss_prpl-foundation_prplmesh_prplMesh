# prplMesh DM Access Matrix (NBAPI / WBAPI-common / WBAPI-pwhm)

## Purpose

Define one enforceable rule-set for how each prplMesh process accesses data models (DMs), to prevent cross-process DM misuse and transport ambiguity.

## Definitions

- `NBAPI`: local Ambiorix datamodel object hosted by the same process (`AmbiorixImpl`).
- `WBAPI-common`: Ambiorix client over ubus transport (`mod-amxb-ubus.so`, `ubus:/var/run/ubus/ubus.sock`).
- `WBAPI-pwhm`: Ambiorix client over USP transport (`mod-amxb-usp.so`, `usp:/var/run/pwhm_usp.sock`).

Note: `WBAPI-pwhm` is not a separate API surface. It is WBAPI with a different backend/URI.

## Global Rules

1. A process may use all three connection types in parallel (`NBAPI`, `WBAPI-common`, `WBAPI-pwhm`).
2. `NBAPI` is local-process DM only. It must not be used for DM owned by another process.
3. `Device.WiFi*` access should use `WBAPI-pwhm` when direct pwhm path is required.
4. Non-`Device.WiFi*` platform/system DMs should use `WBAPI-common`.
5. Call sites must make transport intent explicit (no ambiguous generic client reuse).
6. Silent fallback from `WBAPI-pwhm` to `WBAPI-common` is not allowed in strict mode (or must be explicitly logged and approved in compatibility mode).

## Access Matrix

| Process | DM domain | Read | Write | Subscribe | Allowed API/transport |
|---|---|---|---|---|---|
| Controller | `CONTROLLER_ROOT_DM` | Yes | Yes | Yes | `NBAPI` |
| Controller | `DATAELEMENTS_ROOT_DM` | Yes | Yes | Yes | `NBAPI` |
| Controller | `AGENT_ROOT_DM` (remote) | Yes | Conditional | Conditional | `WBAPI-common` (short time) |
| Controller | `Device.WiFi*` | Yes | Yes | Yes | `WBAPI-pwhm` |
| Controller | Other platform DMs (`Device.IP`, `Device.DeviceInfo`, process manager, etc.) | Yes | Conditional | Conditional | `WBAPI-common` |
| Agent | `AGENT_ROOT_DM` | Yes | Yes | Yes | `NBAPI` |
| Agent | `CONTROLLER_ROOT_DM` (remote) | Yes | No (default) | Conditional | `WBAPI-common` (short time) |
| Agent | `Device.WiFi*` | Yes | Yes | Yes | `WBAPI-pwhm` |
| Agent | Other platform DMs | Yes | Conditional | Conditional | `WBAPI-common` |
| Fronthaul/WHM HAL components | `Device.WiFi*` | Yes | Yes | Yes | `WBAPI-pwhm` |
| Platform Manager | process-manager/system DMs | Yes | Conditional | Conditional | `WBAPI-common` |

## Forbidden Patterns

1. Controller reading `AGENT_ROOT_DM` via local `NBAPI` object.
2. Agent reading `CONTROLLER_ROOT_DM` via local `NBAPI` object.
3. Generic helper functions that hide owner/transport and may route to wrong DM.
4. Implicit transport switching that changes behavior without explicit logs/config.

## Required Implementation Pattern

Use explicit clients per transport in the same process when needed.

Example pattern:
- `amb_ubus.connect(AMBIORIX_WBAPI_BACKEND_PATH, AMBIORIX_WBAPI_BUS_URI)`
- `amb_usp.connect(AMBIORIX_USP_BACKEND_PATH, AMBIORIX_PWHM_USP_BACKEND_URI)`

Routing rule:
- `Device.WiFi*` -> `amb_usp`
- Other external DMs -> `amb_ubus`
- Local process root (`*_ROOT_DM`) -> local `NBAPI`

## Migration Acceptance Criteria

1. No cross-process DM access via local `NBAPI` remains.
2. `Device.WiFi*` call sites are explicitly routed to `WBAPI-pwhm` where required.
3. Fallback behavior is either strict-disabled or explicitly configured and logged.
4. Unit/integration tests cover positive and negative boundary cases.

## Open Decisions

1. Final policy for `WBAPI-pwhm` fallback (`strict` vs `compatibility`).
2. Whether controller/agent reads/writes to remote `AGENT_ROOT_DM`/`CONTROLLER_ROOT_DM` are allowed in selected flows.
3. Exact ownership of any shared/bridged objects exposed by platform integrations.

## Traceability (code anchors)

- Controller local NBAPI init: `controller/src/beerocks/master/beerocks_master_main.cpp`
- Agent local NBAPI init: `agent/src/beerocks/slave/beerocks_agent_main.cpp`
- WBAPI client/transport: `framework/platform/wbapi/*`
- USP constants: `framework/platform/wbapi/include/ambiorix_connection.h`
- USP->ubus fallback behavior: `framework/platform/wbapi/ambiorix_connection_manager.cpp`
- Fronthaul HAL explicit USP usage: `common/beerocks/bwl/whm/base_wlan_hal_whm.cpp`
- Shared BPL helper boundary risk: `framework/platform/bpl/cfg/dm/bpl_cfg_amx_helper.h`
