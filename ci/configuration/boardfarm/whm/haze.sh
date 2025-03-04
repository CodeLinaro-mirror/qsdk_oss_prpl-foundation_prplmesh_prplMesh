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

# Set the LAN bridge IP:
ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.168.1.150"

# Wired backhaul interface:
uci set prplmesh.config.backhaul_wire_iface='wan'
uci commit

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

# Add the WAN port to br-lan
# Add a control interface to Haze on the WAN port, with vlan 200
start_ssh_commands="brctl addif br-lan wan
ip link add link wan name wan.200 type vlan id 200
ip addr add 192.168.200.150/24 dev wan.200
ip link set up dev wan.200
killall -9 dropbear
dropbear -F -T 10 -p192.168.200.150:22 &"

sleep 5

# Copy generated SSH host keys
cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
bootscript="/etc/rc.local"
boot_cmd="sleep 60 && $start_ssh_commands"
if ! grep -q "$boot_cmd" "$bootscript"; then { head -n -2 "$bootscript"; echo "$boot_cmd"; tail -2 "$bootscript"; } >> btscript.tmp; mv btscript.tmp "$bootscript"; fi
set +e && eval "$start_ssh_commands"
