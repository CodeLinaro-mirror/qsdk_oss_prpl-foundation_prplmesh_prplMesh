#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 LAN_IP GUEST_IP WIRED_INTERFACE" >&2
    exit 2
fi

lan_ip="$1"
guest_ip="$2"
wired_interface="$3"

case "$lan_ip:$guest_ip:$wired_interface" in
    *[!A-Za-z0-9_.:-]*)
        echo "Invalid pair configuration argument" >&2
        exit 2
        ;;
esac

rm -f /var/log/messages
syslog-ng-ctl reload

ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.[Alias == \"lan\"].IPAddress=$lan_ip"
ba-cli "IP.Interface.[Name == \"br-guest\"].IPv4Address.[Alias == \"guest\"].IPAddress=$guest_ip"
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"$wired_interface\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Name == \"$wired_interface\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Name == \"$wired_interface\"].Enable=1"
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"$wired_interface\"].Enable=1"
ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'
