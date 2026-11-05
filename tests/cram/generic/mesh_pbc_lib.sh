# shellcheck shell=sh
###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

TT=X_PRPLWARE-COM_Templates
NW=X_PRPLWARE-COM_WiFiController.Network
PM=X_PRPLWARE-COM_ProcessManager.PrplMesh
UD=X_PRPLWARE-COM_Controller.UseDataElementsVapConfigs
PT=$TT.X_PRPLWARE-COM_MeshPBCTemplate
STATE=/tmp/cram-mesh-pbc.state
OUT=/tmp/cram-pbc.out
READY=/tmp/cram-mesh-pbc.ready

M() {
    m_cmd=
    for m_arg in "$@"; do
        m_cmd="$m_cmd '$(printf '%s' "$m_arg" | sed "s/'/'\\\\''/g")'"
    done
    # CRAM_REMOTE_COMMAND holds a command with its options: split it.
    # shellcheck disable=SC2086
    {
        cat "$TESTDIR/mesh_pbc_lib.sh"
        printf '%s\n' "$m_cmd"
    } | ${CRAM_REMOTE_COMMAND:-} sh -s
}

get() {
    ba-cli -l "$1?" 2>/dev/null </dev/null | sed '/^$/d'
}

show() {
    show_obj=$1
    shift
    for show_p in "$@"; do
        echo "$show_p=$(get "$show_obj$show_p")"
    done
}

errors() {
    sed -n -e 's/^ERROR: .*status \([0-9]*\) - .*/error \1/p' \
        -e 's/^ERROR: .*(\([0-9]*\) - .*/error \1/p'
}

put() {
    ba-cli -l "$1=\"$2\"" 2>&1 </dev/null | errors
}

first_key() {
    sed -n 's/^\[{"\([^"]*\)".*/\1/p'
}

poll() {
    poll_end=$(($(date +%s) + $3))
    while :; do
        poll_v=$(get "$1")
        echo "$poll_v" | grep -Eqx "$2" && return 0
        [ "$(date +%s)" -ge "$poll_end" ] && break
        sleep 0.5
    done
    echo "timeout: $1=$poll_v"
    return 1
}

agent() {
    ba-cli -j -l "$NW.Device.[ID==\"$(get $NW.ID)\"].ID?" 2>/dev/null | first_key
}

bss() {
    bss_a=$(agent)
    [ -n "$bss_a" ] || return 1
    ba-cli -j -l "${bss_a}Radio.*.BSS.[SSID==\"PRPL-PRIVATE-5G\"].BSSID?" 2>/dev/null | first_key
}

tpl() {
    ba-cli -j -l "$PT.[PBCTemplateID==\"$1\"].PBCTemplateID?" 2>/dev/null | first_key
}

reply() {
    reply_j=$(grep '^\[' "$1")
    for reply_f in Status FailureReason EnrolleeMACAddress BSSID PBCSessionID; do
        reply_v=$(echo "$reply_j" | jsonfilter -e "@[1].$reply_f" 2>/dev/null) &&
            echo "$reply_f=$reply_v"
    done
    errors <"$1"
}

call() {
    ba-cli -j -l "$1" >"$OUT.now" 2>&1 </dev/null
    reply "$OUT.now"
}

within() {
    within_s=$(date +%s)
    within_n=$1
    shift
    "$@"
    within_t=$(($(date +%s) - within_s))
    [ "$within_t" -le "$within_n" ] || echo "took $within_t s, limit $within_n s"
}

bg() {
    rm -f "$OUT"
    (
        ba-cli -j -l "$1" >"$OUT.tmp" 2>&1
        mv "$OUT.tmp" "$OUT"
    ) </dev/null >/dev/null 2>&1 &
}

collect() {
    collect_end=$(($(date +%s) + $1))
    until [ -f "$OUT" ]; do
        if [ "$(date +%s)" -ge "$collect_end" ]; then
            echo "timeout: no reply in $OUT"
            return 1
        fi
        sleep 0.5
    done
    reply "$OUT"
}

keep() {
    grep -q "^$1=" "$STATE" 2>/dev/null || echo "$1=$(get "$1")" >>"$STATE"
}

saved() {
    sed -n "s|^$1=||p" "$STATE"
}

restart() {
    put "$PM.Enable" 0
    poll "$PM.Status" Idle 60
    put "$PM.Enable" 1
    poll "$PM.Status" Active 60
    poll "$TT.Network.X_PRPLWARE-COM_MeshPBCUUID_E" '[0-9a-fA-F-]{36}' 120
}

ssc_ref() {
    echo "$(ba-cli -j -l "$TT.SSCTemplate.[SSCTemplateID==\"ssc-private\"].SSCTemplateID?" 2>/dev/null |
        first_key)X_PRPLWARE-COM_MeshPBCTemplateReference"
}

bss_tpl_enable() {
    echo "$(ba-cli -j -l "$TT.BSSTemplate.[BSSTemplateID==\"PRPL-PRIVATE-5G\"].BSSTemplateID?" 2>/dev/null |
        first_key)Enable"
}

save_controller() {
    for save_p in "$UD" "$TT.Network.Enable" "$TT.Network.X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference" \
        "$TT.Network.X_PRPLWARE-COM_MeshPBCUUID_E" "$(ssc_ref)" "$(bss_tpl_enable)"; do
        keep "$save_p"
    done
}

# The Controller writes UseDataElementsVapConfigs to its conf file in an event
# handler. The restart can stop the Controller before the handler runs, and the
# handler ignores a write that comes before the Templates data model is
# initialized. After a restart the value comes from the conf file, so a lost
# write shows and is made again. restart() waits for MeshPBCUUID_E, which the
# Controller sets after that initialization.
set_restart() {
    for set_try in 1 2 3 4 5; do
        [ "$(get "$1")" = "$2" ] && return 0
        put "$1" "$2"
        restart
    done
    [ "$(get "$1")" = "$2" ] || echo "$1 is not $2 after $set_try restarts"
}

restore() {
    grep -q "^$1=" "$STATE" 2>/dev/null || return 0
    restore_v=$(saved "$1")
    [ "$(get "$1")" = "$restore_v" ] || put "$1" "$restore_v"
}

restore_restart() {
    grep -q "^$1=" "$STATE" 2>/dev/null || return 0
    set_restart "$1" "$(saved "$1")"
}

restore_templates() {
    sed -n "s|^\($TT\.[^=]*\)=.*|\1|p" "$STATE" 2>/dev/null | while read -r restore_p; do
        restore "$restore_p"
    done
}

delete_templates() {
    for delete_id in cram cram-default; do
        delete_p=$(tpl "$delete_id")
        [ -z "$delete_p" ] || ba-cli -l "$delete_p-" 2>&1 | errors
    done
}

# pwhm starts WPS only on a radio with Status "Up", so the wait includes a DFS
# CAC. Before the push-button configuration, WPS.Enable has the platform
# default.
wifi_up() {
    wifi_s=$(ba-cli -j -l "WiFi.SSID.[BSSID==\"$1\"].Status?" 2>/dev/null </dev/null | first_key)
    [ -n "$wifi_s" ] && [ "$(get "${wifi_s}Status")" = Up ] || return 1
    wifi_r=$(get "${wifi_s}LowerLayers")
    [ "$(get "${wifi_r#Device.}.Status")" = Up ] || return 1
    wifi_ap=$(ba-cli -j -l "WiFi.AccessPoint.[SSIDReference==\"Device.${wifi_s%.}\"].Status?" 2>/dev/null </dev/null |
        first_key)
    [ -n "$wifi_ap" ] && [ "$(get "${wifi_ap}Status")" = Enabled ] || return 1
    [ "$2" = any ] || [ "$(get "${wifi_ap}WPS.Enable")" = 1 ]
}

ready() {
    ready_end=$(($(date +%s) + $1))
    while :; do
        ready_a=$(agent)
        ready_w=$([ -n "$ready_a" ] && get "${ready_a}WPS.Status")
        ready_b=$(bss)
        ready_m=$([ -n "$ready_b" ] && get "${ready_b}BSSID")
        ready_u=no
        [ -n "$ready_m" ] && wifi_up "$ready_m" "$2" && ready_u=yes
        if [ "$ready_u" = yes ] && { [ "$ready_w" = Configured ] || [ "$2" = any ]; }; then
            return 0
        fi
        [ "$(date +%s)" -ge "$ready_end" ] && break
        sleep 1
    done
    echo "not ready: agent=$ready_a WPS.Status=$ready_w bss=$ready_b BSSID=$ready_m up=$ready_u"
    return 1
}

wait_agent() {
    wait_end=$(($(date +%s) + $1))
    until [ -n "$(agent)" ]; do
        if [ "$(date +%s)" -ge "$wait_end" ]; then
            echo "timeout: the own Agent is not in $NW.Device"
            return 1
        fi
        sleep 1
    done
}

# WPS.Status "Error" stays after a session in which every BSS of the Agent
# reported Not started, until a session starts on a BSS of the Agent.
clear_error() {
    clear_a=$(agent)
    [ "$(get "${clear_a}WPS.Status")" = Error ] || return 0
    clear_t=$(tpl cram)
    clear_end=$(($(date +%s) + $1))
    while :; do
        ba-cli -j -l "${clear_t}InitiatePBC()" >"$OUT.now" 2>&1 </dev/null
        poll "${clear_a}WPS.Status" 'Configured|Unconfigured' 5 >/dev/null && return 0
        [ "$(date +%s)" -ge "$clear_end" ] && break
        sleep 2
    done
    echo "WPS.Status of the own Agent is still Error"
    return 1
}

renew() {
    renew_t=$(tpl cram)
    put "${renew_t}ListenDuration" 6
    put "${renew_t}ListenDuration" 5
}

# After a restart the BSSs come up only after one more Templates write, and an
# Agent does not apply a configuration that it gets during a DFS CAC. The link
# is removed first, so "Configured" at the end comes from the new link.
link_template() {
    wait_agent 300 || return 1
    put "$(ssc_ref)" ""
    renew
    poll "$(agent)WPS.Status" 'Unconfigured|Error' 60 || return 1
    ready "$1" any || return 1
    link_t=$(tpl cram)
    put "$(ssc_ref)" "${link_t%.}"
    clear_error 120 || return 1
    ready 300 && touch "$READY"
}

stop_session() {
    stop_t=$(tpl cram)
    if [ -n "$stop_t" ] && [ "$(get "${stop_t}Status.State")" = InProgress ]; then
        ba-cli -l "${stop_t}TerminatePBC()" >/dev/null 2>&1 </dev/null
    fi
    stop_end=$(($(date +%s) + 40))
    while [ -f "$OUT.tmp" ] && [ "$(date +%s)" -lt "$stop_end" ]; do
        sleep 0.5
    done
    [ ! -f "$OUT.tmp" ] || echo "timeout: a call still writes $OUT.tmp"
}
