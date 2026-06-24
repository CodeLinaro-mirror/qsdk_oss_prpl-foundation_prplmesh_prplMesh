#!/bin/sh

# We need to source some files which are only available on prplWrt
# devices, so prevent shellcheck from trying to read them:
# shellcheck disable=SC1091

set -e

# Start with a new log file:
rm -f /var/log/messages && syslog-ng-ctl reload

# Stop the default ssh server on the lan-bridge
service ssh-server stop || true
rm -f /etc/rc.d/S*ssh-server

# Stop and disable the firewall:
service tr181-firewall stop || true
rm -f /etc/rc.d/S*tr181-firewall

# Disable restarting failing serivces by default
service amx-processmonitor stop || true

# Stop and disable the DHCPv4 client: (PPW-888)
service tr181-dhcpv4client stop || true
rm -f /etc/rc.d/*dhcpv4client
pkill -f -9 tr181-dhcpv4client || true

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
ba-cli 'IP.Interface.wan.IPv4Address.primary.{IPAddress="192.168.250.180", SubnetMask="255.255.255.0", AddressingType="Static", Enable=1}'
# Enable it:
ba-cli IP.Interface.wan.IPv4Enable=1

# Set the LAN/GUEST bridge IP:
ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.165.100.180"
ba-cli "IP.Interface.[Name == \"br-guest\"].IPv4Address.[Alias == \"guest\"].IPAddress=192.165.200.180"

# The backhaulWireInterface might not be UP and in br-lan, if previous test was using wifi backhaul (PPM-3361)
# Remove wireless backhual credentials before enable wired backhual, if previous test used wifi backhaul (PPM-4118)
ba-cli "WiFi.EndPoint.*.Profile.*.-"
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"lan0\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Name == \"lan0\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Name == \"lan0\"].Enable=1"
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"lan0\"].Enable=1"

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
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="lan0"
fi


# Don't hide the BH AP SSID (PPW-1399)
ba-cli WiFi.AccessPoint.*.SSIDAdvertisementEnabled=1


# Drop all iptables rules (Guest TS)
iptables -F && iptables -X && iptables -P INPUT ACCEPT && iptables -P OUTPUT ACCEPT && iptables -P FORWARD ACCEPT

# Disable rp_filter
sysctl -w net.ipv4.conf.br-guest.rp_filter=0
sysctl -w net.ipv4.conf.all.rp_filter=0

# Increase inactivity timeout
printf 'protected\nWiFi.AccessPoint.*.StaInactivityTimeout=1500\nexit\n' | ba-cli

# MTK firmware debug
echo 2 > /sys/kernel/debug/ieee80211/phy0/mt76/fw_debug_wm

dmesg -n8

# Traffic Separation Configuration
ba-cli WiFi.AccessPoint.*.MultiAPProfile=3

# Increase log level of WHM components
ba-cli "WiFi.set_trace_zone(zone=genHapd, level=500)"
ba-cli "WiFi.set_trace_zone(zone=hapdAP, level=500)"
ba-cli "WiFi.set_trace_zone(zone=chanMgt, level=500)"
ba-cli "WiFi.set_trace_zone(zone=wpaCtrl, level=500)"

# Start the SSH server on WAN
start_ssh_commands="
iptables -P INPUT ACCEPT
killall -9 dropbear
dropbear -F -T 10 -p192.168.250.180:22 &"

sleep 5

# Copy generated SSH host keys
cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
bootscript="/etc/rc.local"
boot_cmd="sleep 60 && $start_ssh_commands"
if ! grep -q "$boot_cmd" "$bootscript"; then { head -n -2 "$bootscript"; echo "$boot_cmd"; tail -2 "$bootscript"; } >> btscript.tmp; mv btscript.tmp "$bootscript"; fi
set +e && eval "$start_ssh_commands"
