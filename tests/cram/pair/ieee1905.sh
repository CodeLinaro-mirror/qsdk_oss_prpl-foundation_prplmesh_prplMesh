#!/usr/bin/env bash

PAIR_IEEE1905_WAIT_TIMEOUT_SEC=${PAIR_IEEE1905_WAIT_TIMEOUT_SEC:-300}
PAIR_IEEE1905_LINK_TIMEOUT_SEC=${PAIR_IEEE1905_LINK_TIMEOUT_SEC:-150}
PAIR_IEEE1905_UPDATE_TIMEOUT_SEC=${PAIR_IEEE1905_UPDATE_TIMEOUT_SEC:-120}
PAIR_IEEE1905_SETTLE_SEC=${PAIR_IEEE1905_SETTLE_SEC:-90}
PAIR_IEEE1905_HL_INTERVAL_SEC=${PAIR_IEEE1905_HL_INTERVAL_SEC:-10}
PAIR_IEEE1905_EXTRA_IPV4=${PAIR_IEEE1905_EXTRA_IPV4:-192.0.2.19}
PAIR_IEEE1905_AGENT_DOWN_SEC=${PAIR_IEEE1905_AGENT_DOWN_SEC:-300}

# Select the backhaul that the shared IEEE1905 checks expect.
# Input: $1 is eth or wifi.
# Output: sets PAIR_IEEE1905_MEDIA to the MediaType pattern of the neighbor
# interfaces, or prints the reason and returns failure.
pair_ieee1905_use_backhaul() {
    case "$1" in
        eth) PAIR_IEEE1905_MEDIA='^IEEE_802_3' ;;
        wifi) PAIR_IEEE1905_MEDIA='^IEEE_802_11' ;;
        *) printf 'Unknown IEEE1905 backhaul: %s\n' "$1" >&2; return 2 ;;
    esac
}

# Read one data-model parameter value from a pair device.
# Inputs: $1 is controller or agent; $2 is the parameter path without "?".
# Output: prints the raw value (nothing for an empty string).
pair_remote_value() {
    pair_remote_script "$1" "$2" <<'REMOTE_VALUE'
set -eu
ba-cli -l "$1?" | sed '/^$/d'
REMOTE_VALUE
}

pair_lower() {
    printf '%s\n' "$1" | tr '[:upper:]' '[:lower:]'
}

# Read the br-lan MAC address, which prplMesh uses as the AL MAC address.
# Input: $1 is controller or agent.
# Output: prints the lowercase MAC address.
pair_bridge_mac() {
    pair_remote "$1" 'cat /sys/class/net/br-lan/address' |
        tr -d '\r' | tr '[:upper:]' '[:lower:]'
}

# Print "ok" when the observed value equals the expected one.
# Inputs: $1 describes the value; $2 is observed and $3 is expected.
# Output: prints "ok", or both values and returns failure.
pair_check_equal() {
    local description="$1" actual="$2" expected="$3"

    if [[ "$actual" == "$expected" ]]; then
        echo ok
        return 0
    fi
    printf '%s mismatch: actual=%s expected=%s\n' \
        "$description" "$actual" "$expected"
    return 1
}

# Capture the controller IEEE1905 subtree so later checks read one consistent
# state. The "Device." prefix is dropped from parameter paths.
# Output: sets PAIR_IEEE1905_DM to "path=value" lines.
pair_ieee1905_snapshot() {
    local dump

    dump=$(pair_remote controller "ba-cli 'IEEE1905.Network.?'") || return
    PAIR_IEEE1905_DM=$(sed -n -e 's/\r$//' -e 's/^Device\.//' \
        -e '/^IEEE1905\.Network\.[^=]*=/p' <<<"$dump")
}

# Print one parameter value from the IEEE1905 snapshot without quotes.
# Input: $1 is the parameter path, e.g. IEEE1905.Network.AL.1.IEEE1905Id.
# Output: prints the value; returns failure when the parameter is absent.
pair_dm_value() {
    awk -v key="$1=" '
        index($0, key) == 1 {
            value = substr($0, length(key) + 1)
            sub(/^"/, "", value)
            sub(/"$/, "", value)
            print value
            found = 1
            exit
        }
        END { exit !found }' <<<"$PAIR_IEEE1905_DM"
}

# List the instances of one multi-instance object in the IEEE1905 snapshot.
# Input: $1 is the object path without a trailing dot, e.g. IEEE1905.Network.AL.
# Output: prints one instance path per line in data-model order.
pair_dm_instances() {
    awk -v prefix="$1." '
        index($0, prefix) == 1 {
            rest = substr($0, length(prefix) + 1)
            if (match(rest, /^[0-9]+\./)) {
                instance = prefix substr(rest, 1, RLENGTH - 1)
                if (!(instance in seen)) {
                    seen[instance] = 1
                    print instance
                }
            }
        }' <<<"$PAIR_IEEE1905_DM"
}

# Normalize a data-model reference to the snapshot path format.
# Input: $1 is a reference such as "Device.IEEE1905.Network.AL.2.".
# Output: prints the path without the "Device." prefix and trailing dot.
pair_dm_path() {
    local path="${1#Device.}"

    printf '%s\n' "${path%.}"
}

# Find the AL instance whose IEEE1905Id equals a MAC address.
# Input: $1 is the AL MAC address.
# Output: prints the AL path; returns failure when no AL matches.
pair_ieee1905_find_al() {
    local mac al id

    mac=$(pair_lower "$1")
    for al in $(pair_dm_instances IEEE1905.Network.AL); do
        id=$(pair_dm_value "$al.IEEE1905Id") || continue
        if [[ "$(pair_lower "$id")" == "$mac" ]]; then
            printf '%s\n' "$al"
            return 0
        fi
    done
    return 1
}

# Find the IEEE1905 neighbor entries of one AL that point to another AL MAC.
# Inputs: $1 is the AL path; $2 is the neighbor AL MAC address.
# Output: prints "<interface path> <neighbor path>" for each match.
pair_ieee1905_neighbors() {
    local al="$1" mac iface neighbor id

    mac=$(pair_lower "$2")
    for iface in $(pair_dm_instances "$al.Interface"); do
        for neighbor in $(pair_dm_instances "$iface.IEEE1905Neighbor"); do
            id=$(pair_dm_value "$neighbor.NeighborDeviceId") || continue
            if [[ "$(pair_lower "$id")" == "$mac" ]]; then
                printf '%s %s\n' "$iface" "$neighbor"
            fi
        done
    done
}

# Count the interfaces of one AL that report at least one IEEE1905 neighbor.
# Input: $1 is the AL path.
# Output: prints the number of interfaces.
pair_ieee1905_neighbor_iface_count() {
    local al="$1" iface entries count=0

    for iface in $(pair_dm_instances "$al.Interface"); do
        entries=$(pair_dm_value "$iface.IEEE1905NeighborNumberOfEntries") ||
            continue
        if [[ "$entries" =~ ^[0-9]+$ && "$entries" -gt 0 ]]; then
            count=$((count + 1))
        fi
    done
    printf '%s\n' "$count"
}

# Find the Link entry of one interface that points to a neighbor AL MAC.
# Inputs: $1 is the interface path; $2 is the neighbor AL MAC address.
# Output: prints the Link path; returns failure when no Link matches.
pair_ieee1905_link() {
    local iface="$1" mac link id

    mac=$(pair_lower "$2")
    for link in $(pair_dm_instances "$iface.Link"); do
        id=$(pair_dm_value "$link.IEEE1905Id") || continue
        if [[ "$(pair_lower "$id")" == "$mac" ]]; then
            printf '%s\n' "$link"
            return 0
        fi
    done
    return 1
}

# Resolve both pair ALs and the neighbor entries that connect them in the snapshot.
# Input: PAIR_CONTROLLER_MAC and PAIR_AGENT_MAC.
# Output: sets PAIR_{CONTROLLER,AGENT}_{AL,IFACE,NEIGHBOR}; prints the
# missing part and returns failure when the topology is incomplete.
pair_ieee1905_resolve() {
    PAIR_CONTROLLER_AL=$(pair_ieee1905_find_al "$PAIR_CONTROLLER_MAC") || {
        printf 'No IEEE1905 AL for controller %s\n' "$PAIR_CONTROLLER_MAC" >&2
        return 1
    }
    PAIR_AGENT_AL=$(pair_ieee1905_find_al "$PAIR_AGENT_MAC") || {
        printf 'No IEEE1905 AL for agent %s\n' "$PAIR_AGENT_MAC" >&2
        return 1
    }
    read -r PAIR_CONTROLLER_IFACE PAIR_CONTROLLER_NEIGHBOR < <(
        pair_ieee1905_neighbors "$PAIR_CONTROLLER_AL" "$PAIR_AGENT_MAC") || {
        printf 'No IEEE1905 neighbor %s under %s\n' \
            "$PAIR_AGENT_MAC" "$PAIR_CONTROLLER_AL" >&2
        return 1
    }
    read -r PAIR_AGENT_IFACE PAIR_AGENT_NEIGHBOR < <(
        pair_ieee1905_neighbors "$PAIR_AGENT_AL" "$PAIR_CONTROLLER_MAC") || {
        printf 'No IEEE1905 neighbor %s under %s\n' \
            "$PAIR_CONTROLLER_MAC" "$PAIR_AGENT_AL" >&2
        return 1
    }
}

# Check that the controller IEEE1905 model contains both pair devices.
# Input: PAIR_CONTROLLER_MAC and PAIR_AGENT_MAC; optional $1 is a MediaType
# pattern. With it, each AL must have exactly one interface with IEEE1905
# neighbors, and that interface must match the pattern (no stale backhaul).
# Output: refreshes the snapshot and resolved paths; prints the reason and
# returns failure when discovery is incomplete.
pair_ieee1905_ready() {
    local media="${1:-}" status count al iface

    pair_ieee1905_snapshot || return
    status=$(pair_dm_value IEEE1905.Network.Status)
    count=$(pair_dm_value IEEE1905.Network.ALNumberOfEntries)
    [[ "$status" == Available && "$count" == 2 ]] || {
        printf 'IEEE1905 Status=%s ALNumberOfEntries=%s\n' \
            "$status" "$count" >&2
        return 1
    }
    pair_ieee1905_resolve || return
    [[ -n "$(pair_dm_value "$PAIR_AGENT_AL.Version")" ]] || {
        printf '%s has no Version yet\n' "$PAIR_AGENT_AL" >&2
        return 1
    }
    [[ -n "$(pair_dm_value "$PAIR_CONTROLLER_NEIGHBOR.IEEE1905DeviceRef")" &&
        -n "$(pair_dm_value "$PAIR_AGENT_NEIGHBOR.IEEE1905DeviceRef")" ]] || {
        echo 'IEEE1905DeviceRef is not resolved yet' >&2
        return 1
    }
    [[ -n "$media" ]] || return 0
    for al in "$PAIR_CONTROLLER_AL" "$PAIR_AGENT_AL"; do
        count=$(pair_ieee1905_neighbor_iface_count "$al")
        [[ "$count" == 1 ]] || {
            printf '%s has %s interfaces with IEEE1905 neighbors\n' \
                "$al" "$count" >&2
            return 1
        }
    done
    for iface in "$PAIR_CONTROLLER_IFACE" "$PAIR_AGENT_IFACE"; do
        [[ "$(pair_dm_value "$iface.MediaType")" =~ $media ]] || {
            printf '%s.MediaType=%s does not match %s\n' "$iface" \
                "$(pair_dm_value "$iface.MediaType")" "$media" >&2
            return 1
        }
    done
}

pair_wait_ieee1905() {
    pair_wait "$PAIR_IEEE1905_WAIT_TIMEOUT_SEC" \
        'IEEE1905 model with both pair devices' pair_ieee1905_ready "$@"
}

# Check that link metrics created Link entries between both pair devices.
# Input: PAIR_CONTROLLER_MAC and PAIR_AGENT_MAC; optional $1 is passed to
# pair_ieee1905_ready as the neighbor interface MediaType pattern.
# Output: sets PAIR_CONTROLLER_LINK and PAIR_AGENT_LINK, or prints the
# missing direction and returns failure.
pair_ieee1905_links_ready() {
    pair_ieee1905_ready "$@" || return
    PAIR_CONTROLLER_LINK=$(pair_ieee1905_link "$PAIR_CONTROLLER_IFACE" \
        "$PAIR_AGENT_MAC") || {
        printf 'No Link to %s under %s\n' "$PAIR_AGENT_MAC" \
            "$PAIR_CONTROLLER_IFACE" >&2
        return 1
    }
    PAIR_AGENT_LINK=$(pair_ieee1905_link "$PAIR_AGENT_IFACE" \
        "$PAIR_CONTROLLER_MAC") || {
        printf 'No Link to %s under %s\n' "$PAIR_CONTROLLER_MAC" \
            "$PAIR_AGENT_IFACE" >&2
        return 1
    }
}

pair_wait_ieee1905_links() {
    pair_wait "$PAIR_IEEE1905_LINK_TIMEOUT_SEC" \
        'IEEE1905 links between pair devices' pair_ieee1905_links_ready "$@"
}

# Check that the agent prplMesh state machine reached the operational state.
# Output: prints the current state and returns failure when not operational.
pair_agent_operational() {
    local state

    state=$(pair_remote_value agent 'X_PRPLWARE-COM_Agent.Info.CurrentState')
    case "$state" in
        OPERATIONAL*) ;;
        *) printf 'Agent state: %s\n' "$state" >&2; return 1 ;;
    esac
}

pair_wait_agent_operational() {
    pair_wait "$PAIR_WAIT_TIMEOUT_SEC" 'agent to become operational' \
        pair_agent_operational
}

# Check that RegistrarFreqBand is empty or lists only the TR-181 band names.
# Input: $1 is the RegistrarFreqBand value.
# Output: prints "ok", or the unexpected item and returns failure.
pair_check_freq_bands() {
    local band
    local -a bands

    IFS=, read -r -a bands <<<"$1"
    for band in "${bands[@]}"; do
        band="${band#"${band%%[![:space:]]*}"}"
        band="${band%"${band##*[![:space:]]}"}"
        case "$band" in
            '802.11 2.4 GHz' | '802.11 5 GHz' | '802.11 6 GHz' | '802.11 60 GHz') ;;
            *)
                printf 'Unexpected RegistrarFreqBand item: %s\n' "$band"
                return 1
                ;;
        esac
    done
    echo ok
}

# Check that one AL reports an IPv4 address bound to a MAC address.
# Inputs: $1 is the AL path; $2 is the IPv4 address; $3 is the MAC address.
# Output: prints "ok", or the reported addresses and returns failure.
pair_check_ipv4() {
    local al="$1" address="$2" mac entry

    mac=$(pair_lower "$3")
    for entry in $(pair_dm_instances "$al.IPv4Address"); do
        if [[ "$(pair_dm_value "$entry.IPv4Address")" == "$address" &&
            "$(pair_lower "$(pair_dm_value "$entry.MACAddress")")" == "$mac" ]]; then
            echo ok
            return 0
        fi
    done
    printf 'No %s IPv4Address entry with %s/%s; reported:\n' \
        "$al" "$address" "$mac"
    grep -F "$al.IPv4Address." <<<"$PAIR_IEEE1905_DM"
    return 1
}

# Check that one AL reports bridging tuples whose interfaces belong to it.
# Input: $1 is the AL path.
# Output: prints "ok", or the offending tuple and returns failure.
pair_check_bridging_tuples() {
    local al="$1" tuple list item path items=0
    local -a interfaces

    for tuple in $(pair_dm_instances "$al.BridgingTuple"); do
        list=$(pair_dm_value "$tuple.InterfaceList")
        IFS=, read -r -a interfaces <<<"$list"
        for item in "${interfaces[@]}"; do
            path=$(pair_dm_path "$item")
            if [[ "$path" != "$al".Interface.* ]] ||
                ! pair_dm_instances "$al.Interface" | grep -qxF "$path"; then
                printf '%s.InterfaceList has a foreign interface: %s\n' \
                    "$tuple" "$item"
                return 1
            fi
            items=$((items + 1))
        done
    done
    if [[ "$items" -eq 0 ]]; then
        printf '%s reports no bridged interfaces\n' "$al"
        return 1
    fi
    echo ok
}

# Set one string data-model parameter on a pair device.
# Inputs: $1 is controller or agent; $2 is the parameter path; $3 is the value.
# Output: discards the ba-cli reply and returns the remote status.
pair_set_remote_string() {
    pair_remote_script "$1" "$2" "$3" <<'SET_STRING' >/dev/null
set -eu
ba-cli "$1=\"$2\""
SET_STRING
}

# Count the ALs whose IEEE1905Id equals a MAC address in the snapshot.
# Input: $1 is the AL MAC address.
# Output: prints the number of matching AL instances.
pair_ieee1905_al_count() {
    local mac al id count=0

    mac=$(pair_lower "$1")
    for al in $(pair_dm_instances IEEE1905.Network.AL); do
        id=$(pair_dm_value "$al.IEEE1905Id") || continue
        [[ "$(pair_lower "$id")" != "$mac" ]] || count=$((count + 1))
    done
    printf '%s\n' "$count"
}

# Check that the agent AL reports an expected parameter value.
# Inputs: $1 is the parameter name under the agent AL; $2 is the expected value.
# Output: refreshes the snapshot; prints the current value and returns failure
# while it differs.
pair_ieee1905_agent_value_is() {
    local value

    pair_ieee1905_ready || return
    value=$(pair_dm_value "$PAIR_AGENT_AL.$1")
    [[ "$value" == "$2" ]] || {
        printf '%s.%s=%s, waiting for %s\n' "$PAIR_AGENT_AL" "$1" "$value" "$2" >&2
        return 1
    }
}

pair_wait_ieee1905_agent_value() {
    pair_wait "$PAIR_IEEE1905_UPDATE_TIMEOUT_SEC" "agent AL $1 == $2" \
        pair_ieee1905_agent_value_is "$1" "$2"
}

# Check whether the agent AL reports an IPv4 address on its br-lan MAC.
# Inputs: $1 is present or absent; $2 is the IPv4 address.
# Output: refreshes the snapshot; returns failure until the state matches.
pair_ieee1905_agent_ipv4_is() {
    local state="$1" address="$2"

    pair_ieee1905_ready || return
    if pair_check_ipv4 "$PAIR_AGENT_AL" "$address" "$PAIR_AGENT_MAC" >/dev/null; then
        [[ "$state" == present ]]
    else
        [[ "$state" == absent ]]
    fi
}

pair_wait_ieee1905_agent_ipv4() {
    pair_wait "$PAIR_IEEE1905_UPDATE_TIMEOUT_SEC" "agent IPv4 $2 $1" \
        pair_ieee1905_agent_ipv4_is "$1" "$2"
}

# Check that only the controller AL is left after the agent disappeared.
# Input: PAIR_CONTROLLER_MAC and PAIR_AGENT_MAC.
# Output: refreshes the snapshot and PAIR_CONTROLLER_AL; prints the reason and
# returns failure while the agent is still present.
pair_ieee1905_agent_gone() {
    local count

    pair_ieee1905_snapshot || return
    count=$(pair_dm_value IEEE1905.Network.ALNumberOfEntries)
    [[ "$count" == 1 ]] || {
        printf 'IEEE1905 ALNumberOfEntries=%s\n' "$count" >&2
        return 1
    }
    PAIR_CONTROLLER_AL=$(pair_ieee1905_find_al "$PAIR_CONTROLLER_MAC") || {
        printf 'No IEEE1905 AL for controller %s\n' "$PAIR_CONTROLLER_MAC" >&2
        return 1
    }
    [[ "$(pair_ieee1905_al_count "$PAIR_AGENT_MAC")" == 0 ]] || {
        printf 'IEEE1905 AL for agent %s is still present\n' "$PAIR_AGENT_MAC" >&2
        return 1
    }
    [[ "$(pair_ieee1905_neighbor_iface_count "$PAIR_CONTROLLER_AL")" == 0 ]] || {
        printf '%s still reports IEEE1905 neighbors\n' "$PAIR_CONTROLLER_AL" >&2
        return 1
    }
}

pair_wait_ieee1905_agent_gone() {
    pair_wait "${1:-$PAIR_IEEE1905_WAIT_TIMEOUT_SEC}" 'agent AL to be removed' \
        pair_ieee1905_agent_gone
}

# Stop prplMesh on the agent for a while and start it again from a detached
# remote script, so the restart also happens when SSH runs over the backhaul.
# Input: $1 is the number of seconds prplMesh stays stopped.
# Output: returns once the remote script is launched.
pair_agent_prplmesh_bounce() {
    pair_remote_stdin agent 'cat > /tmp/cram_pair_prplmesh_bounce.sh' \
        <<'PRPLMESH_BOUNCE' || return
#!/bin/sh
set -u
rm -f /tmp/cram_pair_prplmesh_bounce.done
ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'
sleep "$1"
ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1'
touch /tmp/cram_pair_prplmesh_bounce.done
PRPLMESH_BOUNCE
    pair_remote agent "rm -f /tmp/cram_pair_prplmesh_bounce.done; chmod +x /tmp/cram_pair_prplmesh_bounce.sh; setsid /tmp/cram_pair_prplmesh_bounce.sh $(pair_quote_argument "$1") >/tmp/cram_pair_prplmesh_bounce.log 2>&1 </dev/null &"
}

pair_agent_prplmesh_bounce_done() {
    pair_remote agent 'test -f /tmp/cram_pair_prplmesh_bounce.done'
}

# Wait until a pair_agent_prplmesh_bounce run started prplMesh again and the
# agent is operational.
# Input: $1 is the stop duration passed to pair_agent_prplmesh_bounce.
# Output: returns failure when the agent does not come back in time.
pair_wait_agent_prplmesh_bounce() {
    pair_wait $(($1 + PAIR_WAIT_TIMEOUT_SEC)) 'agent prplMesh restart' \
        pair_agent_prplmesh_bounce_done || return
    pair_wait_role_ready agent Multi-AP-Agent && pair_wait_agent_operational
}
