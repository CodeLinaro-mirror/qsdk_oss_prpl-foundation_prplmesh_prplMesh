#!/usr/bin/env bash

PAIR_PRIVATE_VID=${PAIR_PRIVATE_VID:-100}
PAIR_GUEST_VID=${PAIR_GUEST_VID:-200}

pair_traffic_separation_ready() {
    pair_remote_script controller "$PAIR_PRIVATE_VID" "$PAIR_BH_AP" \
        <<'TRAFFIC_SEPARATION_READY'
set -eu
[ "$(ba-cli -l "WiFi.AccessPoint.${2}.MultiAPVlanId?" | sed '/^$/d')" = "$1" ]
TRAFFIC_SEPARATION_READY
}

pair_wait_traffic_separation() {
    pair_wait "$PAIR_WAIT_TIMEOUT_SEC" 'Traffic Separation policy application' \
        pair_traffic_separation_ready
}
