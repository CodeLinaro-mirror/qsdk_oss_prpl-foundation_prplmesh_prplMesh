#!/usr/bin/env bash

# Discover the controller's unique 5 GHz home, guest, and backhaul VAPs.
# Input: optional $1 controls guest discovery (1 by default); remote data comes
# from the configured controller.
# Output: sets PAIR_* AP/alias/SSID-reference globals, prints the layout, or
# returns failure.
pair_resolve_controller_layout() {
    local include_guest="${1:-1}" layout

    layout=$(pair_remote_script controller "$include_guest" <<'RESOLVE_LAYOUT'
set -eu
include_guest=$1
wanted_band=5GHz
count=$(ba-cli -l 'WiFi.AccessPointNumberOfEntries?' 2>/dev/null |
    sed '/^$/d' || true)
case "$count" in
    '' | *[!0-9]*) count=64 ;;
esac
fh_ap=
fh_alias=
fh_ssid=
guest_ap=
guest_alias=
guest_ssid=
bh_ap=
bh_alias=
bh_ssid=
ap=1
while [ "$ap" -le "$count" ]; do
    band=$(ba-cli -l \
        "WiFi.AccessPoint.${ap}.RadioReference+.OperatingFrequencyBand?" \
        2>/dev/null | sed '/^$/d' || true)
    if [ "$band" != "$wanted_band" ]; then
        ap=$((ap + 1))
        continue
    fi
    ssid=$(ba-cli -l "WiFi.AccessPoint.${ap}.SSIDReference?" \
        2>/dev/null | sed -e '/^$/d' -e 's/^Device\.//' -e 's/\.$//' || true)
    [ -n "$ssid" ] || {
        printf 'AP%s has no SSID reference\n' "$ap" >&2
        exit 1
    }
    custom_alias=$(ba-cli -l \
        "WiFi.AccessPoint.${ap}.CustomAlias?" 2>/dev/null |
        sed '/^$/d' || true)
    case "$custom_alias" in
        *home*)
            [ -z "$fh_ap" ] || {
                printf 'Multiple %s home VAPs: AP%s and AP%s\n' \
                    "$wanted_band" "$fh_ap" "$ap" >&2
                exit 1
            }
            fh_ap=$ap
            fh_alias=$custom_alias
            fh_ssid=$ssid
            ;;
        *guest*)
            [ -z "$guest_ap" ] || {
                printf 'Multiple %s guest VAPs: AP%s and AP%s\n' \
                    "$wanted_band" "$guest_ap" "$ap" >&2
                exit 1
            }
            guest_ap=$ap
            guest_alias=$custom_alias
            guest_ssid=$ssid
            ;;
        *backhaul*)
            [ -z "$bh_ap" ] || {
                printf 'Multiple %s backhaul VAPs: AP%s and AP%s\n' \
                    "$wanted_band" "$bh_ap" "$ap" >&2
                exit 1
            }
            bh_ap=$ap
            bh_alias=$custom_alias
            bh_ssid=$ssid
            ;;
    esac
    ap=$((ap + 1))
done
[ -n "$fh_ap" ] && [ -n "$fh_ssid" ] && \
    [ -n "$bh_ap" ] && [ -n "$bh_ssid" ] || {
    printf 'Could not resolve %s home and backhaul VAPs by CustomAlias\n' \
        "$wanted_band" >&2
    exit 1
}
[ "$fh_ap" != "$bh_ap" ] || exit 1
if [ "$include_guest" = 1 ]; then
    [ -n "$guest_ap" ] && [ "$guest_ap" != "$fh_ap" ] && \
        [ "$guest_ap" != "$bh_ap" ] || {
        printf 'Could not resolve a distinct %s guest VAP by CustomAlias\n' \
            "$wanted_band" >&2
        exit 1
    }
else
    guest_ap=0
    guest_alias=none
    guest_ssid=none
fi
printf '%s:%s:%s:%s:%s:%s:%s:%s:%s\n' \
    "$fh_ap" "$fh_alias" "$fh_ssid" \
    "$guest_ap" "$guest_alias" "$guest_ssid" \
    "$bh_ap" "$bh_alias" "$bh_ssid"
RESOLVE_LAYOUT
    ) || return
    IFS=: read -r PAIR_FH_AP PAIR_FH_ALIAS PAIR_FH_SSID \
        PAIR_GUEST_AP PAIR_GUEST_ALIAS PAIR_GUEST_SSID \
        PAIR_BH_AP PAIR_BH_ALIAS PAIR_BH_SSID <<<"$layout"
    [[ "$PAIR_FH_AP:$PAIR_GUEST_AP:$PAIR_BH_AP" != *[!0-9:]* ]] || return 1
    [[ "$PAIR_FH_SSID" == WiFi.SSID.* &&
        "$PAIR_BH_SSID" == WiFi.SSID.* ]] || return 1
    if [[ "$include_guest" -eq 1 ]]; then
        [[ "$PAIR_GUEST_SSID" == WiFi.SSID.* ]] || return 1
    fi
    if [[ "$include_guest" -eq 1 ]]; then
        printf 'home=AP%s/%s guest=AP%s/%s backhaul=AP%s/%s\n' \
            "$PAIR_FH_AP" "$PAIR_FH_ALIAS" \
            "$PAIR_GUEST_AP" "$PAIR_GUEST_ALIAS" \
            "$PAIR_BH_AP" "$PAIR_BH_ALIAS"
    else
        printf 'home=AP%s/%s backhaul=AP%s/%s\n' \
            "$PAIR_FH_AP" "$PAIR_FH_ALIAS" \
            "$PAIR_BH_AP" "$PAIR_BH_ALIAS"
    fi
}

# Resolve and validate the agent endpoint's 5 GHz radio reference.
# Input: PAIR_AGENT_ENDPOINT and PAIR_AGENT_PLATFORM from the pair environment.
# Output: sets PAIR_AGENT_RADIO, prints the mapping, or returns failure.
pair_resolve_agent_radio() {
    local radio band

    radio=$(pair_remote_script agent "$PAIR_AGENT_ENDPOINT" <<'AGENT_RADIO'
set -eu
ba-cli -l "WiFi.EndPoint.${1}.RadioReference?" |
    sed -e '/^$/d' -e 's/^Device\.//' -e 's/\.$//'
AGENT_RADIO
    ) || return
    [[ -n "$radio" ]] || return 1
    band=$(pair_remote agent \
        "ba-cli -l '${radio}.OperatingFrequencyBand?' | sed '/^$/d'") || return
    [[ "$band" == 5GHz ]] || {
        printf '%s is not a 5 GHz endpoint radio\n' "$radio" >&2
        return 1
    }
    if [[ "$PAIR_AGENT_PLATFORM" == ospv2 &&
        "$PAIR_AGENT_ENDPOINT" != ep5g0 ]]; then
        echo 'OSPv2 pair tests require the single-link ep5g0 endpoint' >&2
        return 1
    fi
    PAIR_AGENT_RADIO=$radio
    printf 'agent=EP%s/%s/5GHz\n' "$PAIR_AGENT_ENDPOINT" "$PAIR_AGENT_RADIO"
}

# Check that all controller VAPs required by the scenario are enabled.
# Input: $1 controls whether the resolved guest VAP is required.
# Output: returns success only when every selected controller VAP is enabled.
pair_controller_vaps_ready() {
    local include_guest="$1"

    pair_remote_script controller "$include_guest" "$PAIR_FH_AP" \
        "$PAIR_GUEST_AP" "$PAIR_BH_AP" <<'VAPS_READY'
set -eu
include_guest=$1
[ "$(ba-cli -l "WiFi.AccessPoint.${2}.Status?" | sed '/^$/d')" = Enabled ]
[ "$(ba-cli -l "WiFi.AccessPoint.${4}.Status?" | sed '/^$/d')" = Enabled ]
if [ "$include_guest" = 1 ]; then
    [ "$(ba-cli -l "WiFi.AccessPoint.${3}.Status?" | sed '/^$/d')" = Enabled ]
fi
VAPS_READY
}

pair_wait_controller_vaps() {
    pair_wait "$PAIR_WAIT_TIMEOUT_SEC" 'controller VAPs to become enabled' \
        pair_controller_vaps_ready "$1"
}

# Check that the agent endpoint is enabled, idle, and ready to accept WPS.
# Input: PAIR_AGENT_ENDPOINT from the pair environment.
# Output: returns success only when endpoint and agent state permit onboarding.
pair_agent_endpoint_ready() {
    pair_remote_script agent "$PAIR_AGENT_ENDPOINT" <<'AGENT_ENDPOINT_READY'
set -eu
endpoint=$1
case "$(ba-cli -l 'X_PRPLWARE-COM_Agent.Info.CurrentState?' | sed '/^$/d')" in
    WAIT_FOR_BACKHAUL_MANAGER_CONNECTED_NOTIFICATION*) ;;
    *) exit 1 ;;
esac
[ "$(ba-cli -l "WiFi.EndPoint.${endpoint}.Status?" | sed '/^$/d')" = Enabled ]
[ -n "$(ba-cli -l "WiFi.EndPoint.${endpoint}.IntfName?" | sed '/^$/d')" ]
case "$(ba-cli -l "WiFi.EndPoint.${endpoint}.ConnectionStatus?" | sed '/^$/d')" in
    Idle | Disconnected) ;;
    *) exit 1 ;;
esac
case "$(ba-cli -l "WiFi.EndPoint.${endpoint}.WPS.Enable?" | sed '/^$/d')" in
    1 | true) ;;
    *) exit 1 ;;
esac
AGENT_ENDPOINT_READY
}

pair_wait_wps_ready() {
    pair_wait "$PAIR_WAIT_TIMEOUT_SEC" 'agent endpoint to become enabled' \
        pair_agent_endpoint_ready
}

# Remove firewall and reverse-path filtering barriers used by pair traffic.
# Input: $1 is the controller or agent role whose network policy is updated.
# Output: returns the selected device's remote script status.
pair_open_network() {
    local role="$1"

    pair_remote_script "$role" <<'OPEN_NETWORK'
set -eu
iptables -F
iptables -X
iptables -P INPUT ACCEPT
iptables -P OUTPUT ACCEPT
iptables -P FORWARD ACCEPT
sysctl -w net.ipv4.conf.all.rp_filter=0 >/dev/null
sysctl -w net.ipv4.conf.default.rp_filter=0 >/dev/null
for interface in br-lan br-guest; do
    setting="/proc/sys/net/ipv4/conf/${interface}/rp_filter"
    [ ! -e "$setting" ] || printf '0\n' >"$setting"
done
OPEN_NETWORK
}

# Start one validated data-model subscription and wait for its acknowledgement.
# Inputs: $1 is controller or agent; $2 is its allowed subscription query.
# Output: creates remote log/PID files and returns failure if startup is not confirmed.
pair_start_subscription() {
    local role="$1" query="$2"

    pair_remote_script "$role" "$query" "$role" <<'START_SUBSCRIPTION'
set -eu
query=$1
role=$2
case "$role:$query" in
    controller:WiFi.AccessPoint.\?\& | agent:WiFi.EndPoint.\?\&) ;;
    *) exit 2 ;;
esac
cat >/tmp/cram_pair_subscription.sh <<'SUBSCRIPTION'
#!/bin/sh
query=$1
{ printf '%s\n' "$query"; sleep 900; } | ba-cli
SUBSCRIPTION
chmod +x /tmp/cram_pair_subscription.sh
log="/tmp/cram_pair_${role}_events.log"
pid_file="/tmp/cram_pair_${role}_events.pid"
rm -f "$log" "$pid_file"
setsid /tmp/cram_pair_subscription.sh "$query" >"$log" 2>&1 </dev/null &
printf '%s\n' "$!" >"$pid_file"
for _ in $(seq 1 20); do
    grep -Fq 'Added subscription for' "$log" && exit 0
    kill -0 "$!" 2>/dev/null || break
    sleep 1
done
cat "$log" >&2
exit 1
START_SUBSCRIPTION
}

pair_start_wps_subscriptions() {
    pair_start_subscription controller 'WiFi.AccessPoint.?&' || return
    pair_start_subscription agent 'WiFi.EndPoint.?&' || return
    printf 'subscriptions-ready\n'
}

# Detect WPS activity on a controller access point or agent endpoint.
# Inputs: $1 is controller or agent; $2 is the role-specific object identifier.
# Output: returns success when pairing activity is visible (or unsupported on OSPv2).
pair_wps_activity() {
    local role="$1" object="$2"

    if [[ "$role:$PAIR_CONTROLLER_PLATFORM" == controller:ospv2 ]]; then
        return 0
    fi
    if [[ "$role" == controller ]]; then
        pair_remote_script controller "$object" <<'CONTROLLER_WPS_ACTIVITY'
set -eu
case "$(ba-cli -l "WiFi.AccessPoint.${1}.WPS.PairingInProgress?" | sed '/^$/d')" in
    1 | true) ;;
    *) exit 1 ;;
esac
CONTROLLER_WPS_ACTIVITY
    else
        pair_remote_script agent "$object" <<'AGENT_WPS_ACTIVITY'
set -eu
case "$(ba-cli -l "WiFi.EndPoint.${1}.ConnectionStatus?" | sed '/^$/d')" in
    WPS_Pairing | Connecting | Connected) ;;
    *) exit 1 ;;
esac
AGENT_WPS_ACTIVITY
    fi
}

pair_wait_wps_activity() {
    pair_wait 12 "$1 WPS activity" pair_wps_activity "$1" "$2"
}

pair_agent_endpoint_connected() {
    pair_remote agent \
        "test \"\$(ba-cli -l 'WiFi.EndPoint.${PAIR_AGENT_ENDPOINT}.ConnectionStatus?' | sed '/^$/d')\" = Connected"
}

pair_wait_agent_endpoint_connected() {
    pair_wait 360 'agent 5 GHz backhaul connection' \
        pair_agent_endpoint_connected
}

# Check that Wi-Fi onboarding has connected the endpoint and made the agent operational.
# Input: PAIR_AGENT_ENDPOINT from the pair environment.
# Output: returns success only when both connection and agent state are complete.
pair_wifi_onboarding_complete() {
    pair_remote_script agent "$PAIR_AGENT_ENDPOINT" <<'ONBOARDING_COMPLETE'
set -eu
[ "$(ba-cli -l "WiFi.EndPoint.${1}.ConnectionStatus?" | sed '/^$/d')" = Connected ]
case "$(ba-cli -l 'X_PRPLWARE-COM_Agent.Info.CurrentState?' | sed '/^$/d')" in
    OPERATIONAL*) ;;
    *) exit 1 ;;
esac
ONBOARDING_COMPLETE
}

pair_wait_wifi_onboarding() {
    pair_wait 360 'agent Wi-Fi backhaul onboarding' \
        pair_wifi_onboarding_complete
}

pair_wifi_switch_ready() {
    local status

    status=$(pair_remote agent \
        'cat /tmp/cram_pair_wifi_switch.status 2>/dev/null' 2>/dev/null || true)
    [[ "$status" == SUCCESS ]]
}

# Switch the agent from wired to Wi-Fi backhaul without dropping the SSH caller.
# Input: TS_AGENT_WIRED and PAIR_CONTROLLER_IP from the pair environment.
# Output: starts an asynchronous remote switch, records restoration state, and waits
# for the private Wi-Fi path before printing the completion marker.
pair_use_wifi_backhaul() {
    local initial_state launch_command

    initial_state=$(pair_remote_script agent "$TS_AGENT_WIRED" <<'WIRED_STATE'
set -eu
interface=$1
flags=$(cat "/sys/class/net/${interface}/flags")
if [ $((flags & 1)) -eq 1 ]; then printf '1\n'; else printf '0\n'; fi
WIRED_STATE
    ) || return
    case "$initial_state" in
        0 | 1) ;;
        *) printf 'Invalid wired interface state: %s\n' "$initial_state" >&2; return 1 ;;
    esac
    # Read by the EXIT cleanup installed from utils.sh.
    # shellcheck disable=SC2034
    PAIR_AGENT_WIRED_RESTORE=$initial_state
    pair_remote_stdin agent 'cat > /tmp/cram_pair_wifi_switch.sh' \
        <<'WIFI_SWITCH'
#!/bin/sh
set -eu
interface=$1
controller_ip=$2
status_file=/tmp/cram_pair_wifi_switch.status
restore() {
    status=$?
    trap - EXIT
    ip link set dev "$interface" up || true
    printf 'FAILED:%s\n' "$status" >"$status_file"
    exit "$status"
}
trap restore EXIT
rm -f "$status_file"
ip link set dev "$interface" down
for _ in $(seq 1 90); do
    if ping -I br-lan -c 1 -W 1 "$controller_ip" >/dev/null 2>&1; then
        printf 'SUCCESS\n' >"$status_file"
        trap - EXIT
        exit 0
    fi
    sleep 1
done
exit 1
WIFI_SWITCH
    launch_command="chmod +x /tmp/cram_pair_wifi_switch.sh; rm -f /tmp/cram_pair_wifi_switch.status; setsid /tmp/cram_pair_wifi_switch.sh $(pair_quote_argument "$TS_AGENT_WIRED") $(pair_quote_argument "$PAIR_CONTROLLER_IP") >/tmp/cram_pair_wifi_switch.log 2>&1 </dev/null &"
    pair_remote agent "$launch_command" || return
    pair_wait 120 'agent Wi-Fi private data path' pair_wifi_switch_ready || return
    printf 'wifi-backhaul-active\n'
}
