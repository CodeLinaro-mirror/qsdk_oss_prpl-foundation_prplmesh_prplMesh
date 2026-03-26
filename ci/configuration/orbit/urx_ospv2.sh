#!/bin/sh

# We need to source some files which are only available on prplWrt
# devices, so prevent shellcheck from trying to read them:
# shellcheck disable=SC1091

set -e

# Start with a new log file:
rm -f /var/log/messages && syslog-ng-ctl reload

# Stop and disable the firewall:
sh /etc/init.d/tr181-firewall stop || true
rm -f /etc/rc.d/S*tr181-firewall

ubus wait_for IP.Interface

sleep 5

# Set the LAN bridge IP:
ubus call "IP.Interface" _set '{ "rel_path": ".[Name == \"br-lan\"].IPv4Address.[Alias == \"lan\"].", "parameters": { "IPAddress": "192.168.1.99" } }'
sleep 10

ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0"
sleep 5
ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=\"Multi-AP-Agent\""
ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=1"
ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1"
sleep 5

ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"eth0_2\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Alias == \"cpe-eth0_2\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Alias == \"cpe-eth0_2\"].Enable=1"
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"eth0_2\"].Enable=1"

# Set the wired backhaul interface:
if ba-cli "X_PRPLWARE-COM_Agent.Configuration.?" | grep -Eq "No data found|ERROR"; then
  # Prplmesh agent is not running. Data model isn't up.
  echo "Prplmesh agent is not running"
else
  # Prplmesh agent is running, configure it over the bus
  echo "Setting prplMesh BackhaulWireInterface over DM"
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="eth0_2"
fi

sleep 5

# enable STA-mode on 2.4 and 5GHz
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].STA_Mode=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].STASupported_Mode=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].STA_Mode=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].STASupported_Mode=1"

# Increase log trace
ubus-cli "WiFi.set_trace_zone(zone=genHapd, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=hapdAP, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=chanMgt, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=wpaCtrl, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=mxlRad, level=500)"

# Reduce DWELL time of channel scans to 20ms
printf "protected\nDevice.WiFi.Vendor.ModuleMode.CertificationMode=1\nexit\n" | ba-cli

sleep 3

#iw dev wlan0 iwlwav sCoCPower 0 1 1
#sleep 1
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
