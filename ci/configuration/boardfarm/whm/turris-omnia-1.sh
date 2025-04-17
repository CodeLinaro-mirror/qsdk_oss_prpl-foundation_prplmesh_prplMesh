#!/bin/sh

# We need to source some files which are only available on prplWrt
# devices, so prevent shellcheck from trying to read them:
# shellcheck disable=SC1091

set -e

# Start with a new log file:
rm -f /var/log/messages && syslog-ng-ctl reload

sh /etc/init.d/tr181-upnp stop || true
rm -f /etc/rc.d/S*tr181-upnp

sh /etc/init.d/obuspa stop || true
rm -f /etc/rc.d/S*obuspa

# Stop the default ssh server on the lan-bridge
sh /etc/init.d/ssh-server stop || true
rm -f /etc/rc.d/S*ssh-server

# Stop and disable the firewall:
sh /etc/init.d/tr181-firewall stop || true
rm -f /etc/rc.d/S*tr181-firewall

# Disable restarting failing serivces by default
sh /etc/init.d/amx-processmonitor stop || true

ubus wait_for IP.Interface

# Stop and disable the DHCP clients and servers:
ba-cli DHCPv4Client.Client.wan.Enable=0
ba-cli DHCPv6Client.Client.wan.Enable=0
ba-cli DHCPv4Server.Enable=0
ba-cli DHCPv6Server.Enable=0

# IP for device upgrades, operational tests, Boardfarm data network, ...
# Note that this device uses the WAN interface (as on some Omnias the
# others don't work in the bootloader):
# Add the IP address if there is none yet:

ba-cli IP.Interface.wan.IPv4Address.primary.? | grep -Eq "No data found|ERROR" && {
    echo "Adding IP address $IP"
    ba-cli 'IP.Interface.wan.IPv4Address.+{Alias="primary", AddressingType="Static"}'
}
# Configure it:
ba-cli 'IP.Interface.wan.IPv4Address.primary.{IPAddress="192.168.1.100", SubnetMask="255.255.255.0", AddressingType="Static", Enable=1}'
# Enable it:
ba-cli IP.Interface.wan.IPv4Enable=1

# Set the LAN bridge IP:
ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.165.0.100"

# Set the wired backhaul interface:
if ba-cli "X_PRPLWARE-COM_Agent.Configuration.?" | grep -Eq "No data found|ERROR"; then
  # Prplmesh agent is not running. Data model isn't up.
  echo "Prplmesh agent is not running"
else
  # Prplmesh agent is running, configure it over the bus
  echo "Setting prplMesh BackhaulWireInterface over DM"
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="eth2"
fi

# Enable Wi-Fi radios
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].Enable=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].Enable=1"

# all pwhm default configuration can be found in /etc/amx/wld/wld_defaults.odl.uc

# Enable when hostapd on this target supports it
# ba-cli "WiFi.AccessPoint.*.MBOEnable=1"

# Make sure specific channels are configured. If channel is set to 0,
# ACS will be configured. If ACS is configured hostapd will refuse to
# switch channels when we ask it to. Channels 1 and 48 were chosen
# because they are NOT used in the WFA certification tests (this
# allows to verify that the device actually switches channel as part
# of the test).
# See also PPM-1928.
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].Channel=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].Channel=48"

# Restrict channel bandwidth or the certification test could miss beacons
# (see PPM-258)
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].OperatingChannelBandwidth=20MHz"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].OperatingChannelBandwidth=20MHz"

sleep 5

# Copy generated SSH host keys
cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
BOOTSCRIPT="/etc/rc.local"
SERVER_CMD="sleep 60 && /etc/init.d/ssh-server stop && dropbear -F -T 10 -p192.168.1.100:22 &"
if ! grep -q "$SERVER_CMD" "$BOOTSCRIPT"; then { head -n -2 "$BOOTSCRIPT"; echo "$SERVER_CMD"; tail -2 "$BOOTSCRIPT"; } >> btscript.tmp; mv btscript.tmp "$BOOTSCRIPT"; fi

# Stop the default ssh server on the lan-bridge
/etc/init.d/ssh-server stop
dropbear -F -T 10 -p192.168.1.100:22 &
