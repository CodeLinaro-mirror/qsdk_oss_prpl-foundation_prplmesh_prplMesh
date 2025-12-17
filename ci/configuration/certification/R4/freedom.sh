#!/bin/sh

# We need to source some files which are only available on prplWrt
# devices, so prevent shellcheck from trying to read them:
# shellcheck disable=SC1091

set -e

# Start with a new log file:
rm -f /var/log/messages && syslog-ng-ctl reload

# Don't stop obuspa/upnp services, they are required for USP to work correctly

# Stop the default ssh server on the lan-bridge
sh /etc/init.d/ssh-server stop || true
rm -f /etc/rc.d/S*ssh-server

# Stop and disable the firewall:
sh /etc/init.d/tr181-firewall stop || true
rm -f /etc/rc.d/S*tr181-firewall

ubus wait_for IP.Interface

# Stop and disable the DHCP clients and servers:
ba-cli DHCPv4Client.Client.wan.Enable=0
ba-cli DHCPv6Client.Client.wan.Enable=0
ba-cli DHCPv4Server.Enable=0
ba-cli DHCPv6Server.Enable=0

# Fix overlapping MACs in 6GHz radio
ba-cli Device.Ethernet.Link.ethernet_wan.MACAddress="58:E4:03:D2:10:04"
ba-cli Device.WiFi.SSID.GUEST_RADIO3.MACAddress="58:E4:03:D2:10:50"

# We use WAN for the control interface.
# Add the IP address if there is none yet:
ba-cli IP.Interface.wan.IPv4Address.primary.? | grep -Eq "No data found|ERROR" && {
    echo "Adding IP address $IP"
    ba-cli 'IP.Interface.wan.IPv4Address.+{Alias="primary", AddressingType="Static"}'
}
# Configure it:
ba-cli 'IP.Interface.wan.IPv4Address.primary.{IPAddress="192.168.250.150", SubnetMask="255.255.255.0", AddressingType="Static", Enable=1}'
# Enable it:
ba-cli IP.Interface.wan.IPv4Enable=1

# Set the LAN bridge IP:
ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.165.100.150"

# Set guest bridge IP:
ba-cli "IP.Interface.[Name == \"br-guest\"].IPv4Address.[Alias == \"guest\"].IPAddress=192.165.100.155"

# Setting BackhaulWireIface, or persistence can fail (PPM-3339)
/etc/init.d/prplmesh stop && sleep 2
/etc/init.d/prplmesh start && sleep 2

# Set the wired backhaul interface:
if ba-cli "X_PRPLWARE-COM_Agent.Configuration.?" | grep -Eq "No data found|ERROR"; then
  # Prplmesh agent is not running. Data model isn't up.
  echo "Prplmesh agent is not running"
else
  # Prplmesh agent is running, configure it over the bus
  echo "Setting prplMesh BackhaulWireInterface over DM"
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="lan4"
fi

# enable Wi-Fi radios
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "Enable": "true" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "Enable": "true" } }'

# all pwhm default configuration can be found in /etc/amx/wld/wld_defaults.odl.uc

# Add all VAPs to br-lan by default
#ubus-cli WiFi.AccessPoint.*.DefaultDeviceType="Data"
#ubus-cli WiFi.AccessPoint.*.BridgeInterface="br-lan"

ba-cli WiFi.Radio.*.RegulatoryDomain="US"

# Set multiAP profile for primary_vlan_id support
ubus-cli WiFi.AccessPoint.*.MultiAPProfile=3

# Enable when hostapd on this target supports it
ubus-cli "WiFi.AccessPoint.*.MBOEnable=1"

# Configure Operating Standards
ba-cli "WiFi.Radio.*.OperatingStandardsFormat=\"Standard\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].OperatingStandards=\"b,g\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].OperatingStandards=\"a,n,ac,ax\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"6GHz\"].OperatingStandards=\"ax\""


ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].PacketAggregationEnable=0"

# Restrict channel bandwidth or the certification test could miss beacons
# (see PPM-258)
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].OperatingChannelBandwidth=20MHz"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].OperatingChannelBandwidth=20MHz"

# Drop all iptables rules (Guest TS)
iptables -F && iptables -X && iptables -P INPUT ACCEPT && iptables -P OUTPUT ACCEPT && iptables -P FORWARD ACCEPT

# Disable rp_filter
sysctl -w net.ipv4.conf.br-guest.rp_filter=0
sysctl -w net.ipv4.conf.all.rp_filter=0

# Increase inactivity timeout
printf 'protected\nWiFi.AccessPoint.*.StaInactivityTimeout=1500\nexit\n' | ba-cli

# Commands to start a new SSH server on the control port
start_ssh_commands="killall -9 dropbear
dropbear -F -T 10 -p192.168.250.150:22 &"

sleep 5

# Copy generated SSH host keys
cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
bootscript="/etc/rc.local"
boot_cmd="sleep 60 && $start_ssh_commands"
if ! grep -q "$boot_cmd" "$bootscript"; then { head -n -2 "$bootscript"; echo "$boot_cmd"; tail -2 "$bootscript"; } >> btscript.tmp; mv btscript.tmp "$bootscript"; fi
set +e && eval "$start_ssh_commands"
