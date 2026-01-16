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

# Set the LAN bridge IP:
ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.165.100.180"

# Setting BackhaulWireIface, or persistence can fail (PPM-3339)
/etc/init.d/prplmesh setmode --mode Multi-AP-Controller-and-Agent --cert true

# Set the wired backhaul interface:
if ba-cli "X_PRPLWARE-COM_Agent.Configuration.?" | grep -Eq "No data found|ERROR"; then
  # Prplmesh agent is not running. Data model isn't up.
  echo "Prplmesh agent is not running"
else
  # Prplmesh agent is running, configure it over the bus
  echo "Setting prplMesh BackhaulWireInterface over DM"
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="lan0"
fi


ba-cli WiFi.Radio.*.RegulatoryDomain="US"

ba-cli WiFi.AccessPoint.*.MBOEnable=1

# Configure Operating Standards
ba-cli "WiFi.Radio.*.OperatingStandardsFormat=\"Standard\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].OperatingStandards=\"b,g,n,ax\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].OperatingStandards=\"a,n,ac,ax\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"6GHz\"].OperatingStandards=\"ax\""


# Make sure specific channels are configured. If channel is set to 0,
# ACS will be configured. If ACS is configured hostapd will refuse to
# switch channels when we ask it to. Channels 1 and 48 were chosen
# because they are NOT used in the WFA certification tests (this
# allows to verify that the device actually switches channel as part
# of the test).
# See also PPM-1928.
ba-cli WiFi.Radio.*.AutoChannelEnable=0
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].Channel=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].Channel=48"

# Restrict channel bandwidth or the certification test could miss beacons
# (see PPM-258)
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].OperatingChannelBandwidth=20MHz"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].OperatingChannelBandwidth=20MHz"

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
