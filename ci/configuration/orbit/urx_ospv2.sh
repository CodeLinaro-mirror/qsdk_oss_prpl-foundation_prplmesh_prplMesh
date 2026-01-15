#!/bin/sh

# We need to source some files which are only available on prplWrt
# devices, so prevent shellcheck from trying to read them:
# shellcheck disable=SC1091

set -e

# Start with a new log file:
rm -f /var/log/messages && syslog-ng-ctl reload

ubus wait_for IP.Interface

sleep 5


# Set the LAN bridge IP:
ubus call "IP.Interface" _set '{ "rel_path": ".[Name == \"br-lan\"].IPv4Address.[Alias == \"lan\"].", "parameters": { "IPAddress": "192.168.1.99" } }'
sleep 10

# Set the wired backhaul interface:
if ba-cli "X_PRPLWARE-COM_Agent.Configuration.?" | grep -Eq "No data found|ERROR"; then
  # Prplmesh agent is not running. Data model isn't up.
  echo "Prplmesh agent is not running"
else
  # Prplmesh agent is running, configure it over the bus
  echo "Setting prplMesh BackhaulWireInterface over DM"
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="eth0_2"
fi

uci batch << 'EOF'
set prplmesh.radio0.hostap_iface='wlan2'
set prplmesh.radio0.hostap_iface_steer_vaps='wlan2.1'
set prplmesh.radio0.sta_iface='wlan3'
set prplmesh.radio1.hostap_iface='wlan0'
set prplmesh.radio1.hostap_iface_steer_vaps='wlan0.1'
set prplmesh.radio1.sta_iface='wlan1'
EOF

uci commit

# Required for config_load:
. /lib/functions/system.sh
# Required for config_foreach:
. /lib/functions.sh

# Unset STA credentials from previous test
ubus-cli WiFi.EndPoint.1.ProfileReference=0
ubus-cli WiFi.EndPoint.1.Enable=0
ubus-cli WiFi.EndPoint.1.Enable=1

ubus-cli "WiFi.AccessPoint.*.MBOEnable=1"
ubus-cli WiFi.AccessPoint.*.DefaultDeviceType="Data"
ubus-cli WiFi.AccessPoint.*.BridgeInterface="br-lan"

sleep 5

# enable STA-mode on 2.4 and 5GHz
# ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "STA_Mode": "true" } }'
# ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "STASupported_Mode": "true" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "STA_Mode": "true" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "STASupported_Mode": "true" } }'

sleep 5

# enable Wi-Fi radios
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "Enable": "true" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "Enable": "true" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"6GHz\"].", "parameters": { "Enable": "true" } }'


# Increase log trace
ubus-cli "WiFi.set_trace_zone(zone=genHapd, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=hapdAP, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=chanMgt, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=wpaCtrl, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=mxlRad, level=500)"

# Automatic setting is not supported yet
# ubus-cli WiFi.Vendor.ModuleMode.CertificationMode=1

sleep 3
ubus-cli WiFi.AccessPoint.*.MultiAPProfile=3
sleep 2
ubus-cli WiFi.EndPoint.*.Vendor.MultiApProfile=3
sleep 2
#iw dev wlan0 iwlwav sCoCPower 0 1 1
sleep 1
#iw dev wlan2 iwlwav sCoCPower 0 1 1

# Copy generated host keys
# cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
BOOTSCRIPT="/etc/rc.local"
SERVER_CMD="sleep 20 && iptables -P INPUT ACCEPT"
if ! grep -q "$SERVER_CMD" "$BOOTSCRIPT"; then { head -n -2 "$BOOTSCRIPT"; echo "$SERVER_CMD"; tail -2 "$BOOTSCRIPT"; } >> btscript.tmp; mv btscript.tmp "$BOOTSCRIPT"; fi

# Stop and disable the firewall:
sh /etc/init.d/tr181-firewall stop
rm -f /etc/rc.d/S22tr181-firewall

iptables -P INPUT ACCEPT
