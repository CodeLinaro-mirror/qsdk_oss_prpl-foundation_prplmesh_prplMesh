#!/bin/sh

# We need to source some files which are only available on prplWrt
# devices, so prevent shellcheck from trying to read them:
# shellcheck disable=SC1091

set -e

# Start with a new log file:
rm -f /var/log/messages && syslog-ng-ctl reload

# Don't stop obuspa/upnp services, they are required for USP to work correctly

# Stop the default ssh server on the lan-bridge
service ssh-server stop || true
rm -f /etc/rc.d/S*ssh-server

# Stop and disable the firewall:
service tr181-firewall stop || true
rm -f /etc/rc.d/S*tr181-firewall

# Disable restarting failing serivces by default
service amx-processmonitor stop || true

ubus wait_for IP.Interface

# Stop and disable the DHCP clients and servers:
ba-cli DHCPv4Client.Client.wan.Enable=0
ba-cli DHCPv6Client.Client.wan.Enable=0
ba-cli DHCPv4Server.Enable=0
ba-cli DHCPv6Server.Enable=0

# We use WAN for the control interface.
# Add the IP address if there is none yet:
ba-cli IP.Interface.wan.IPv4Address.primary.? | grep -Eq "No data found|ERROR" && {
    echo "Adding IP address $IP"
    ba-cli 'IP.Interface.wan.IPv4Address.+{Alias="primary", AddressingType="Static"}'
}
# Configure it:
ba-cli 'IP.Interface.wan.IPv4Address.primary.{IPAddress="192.168.250.160", SubnetMask="255.255.255.0", AddressingType="Static", Enable=1}'
# Enable it:
ba-cli IP.Interface.wan.IPv4Enable=1

# Set the LAN bridge IP:
ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.165.100.160"

# The backhaulWireInterface might not be UP and in br-lan, if previous test was using wifi backhaul (PPM-3361)
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"eth0_2\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Name == \"eth0_2\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Name == \"eth0_2\"].Enable=1"
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"eth0_2\"].Enable=1"

ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0"
sleep 5
ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=\"Multi-AP-Agent\""
ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=1"
ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1"
sleep 5

# Set the wired backhaul interface:
if ba-cli "X_PRPLWARE-COM_Agent.Configuration.?" | grep -Eq "No data found|ERROR"; then
  # Prplmesh agent is not running. Data model isn't up.
  echo "Prplmesh agent is not running"
else
  # Prplmesh agent is running, configure it over the bus
  echo "Setting prplMesh BackhaulWireInterface over DM"
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="eth0_2"
fi


ba-cli WiFi.Radio.*.RegulatoryDomain="US"

ba-cli "WiFi.set_trace_zone(zone=genHapd, level=500)"
ba-cli "WiFi.set_trace_zone(zone=hapdAP, level=500)"
ba-cli "WiFi.set_trace_zone(zone=chanMgt, level=500)"
ba-cli "WiFi.set_trace_zone(zone=wpaCtrl, level=500)"
ba-cli "WiFi.set_trace_zone(zone=mxlRad, level=500)"


# Reduce DWELL time of channel scans to 20ms
printf "protected\nDevice.WiFi.Vendor.ModuleMode.CertificationMode=1\nexit\n" | ba-cli

# Radio's need to be up to set the antenna configuration (workaroud for missing sniffer captures in default 4x4 configuration)
ba-cli WiFi.Radio.*.Enable=1
ba-cli "WiFi.SSID.[Alias == \"VAP2G0PRIV\"].Enable=1"
ba-cli "WiFi.SSID.[Alias == \"VAP5G0PRIV\"].Enable=1"
sleep 10
iw-mxl dev wlan0 iwlwav sCoCPower 0 1 1
sleep 1
iw-mxl dev wlan2 iwlwav sCoCPower 0 1 1

# Commands to start a new SSH server on the control port
start_ssh_commands="iptables -P INPUT ACCEPT
killall -9 dropbear
dropbear -F -T 10 -p192.168.250.160:22 &"

sleep 5

# Copy generated SSH host keys
cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
bootscript="/etc/rc.local"
boot_cmd="sleep 60 && $start_ssh_commands"
if ! grep -q "$boot_cmd" "$bootscript"; then { head -n -2 "$bootscript"; echo "$boot_cmd"; tail -2 "$bootscript"; } >> btscript.tmp; mv btscript.tmp "$bootscript"; fi
set +e && eval "$start_ssh_commands"
