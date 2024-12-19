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

# Disable restarting failing serivces by default
sh /etc/init.d/amx-processmonitor stop || true

ubus wait_for IP.Interface

# Stop and disable the DHCP clients and servers:
if ubus call DHCPv4 _list >/dev/null ; then
  ubus call DHCPv4.Server _set '{"parameters": { "Enable": False }}'
fi
if ubus call DHCPv6 _list >/dev/null ; then
  ubus call DHCPv6.Server _set '{"parameters": { "Enable": False }}'
fi

# Fix overlapping MACs in 6GHz radio
# ubus-cli WiFi.SSID.DEFAULT_RADIO2.MACAddress="58:E4:03:D2:70:16"
# ubus-cli WiFi.SSID.DEFAULT_RADIO2.BSSID="58:e4:03:d2:70:16"
# ubus-cli WiFi.SSID.GUEST_RADIO2.MACAddress="58:E4:03:D2:70:17"
# ubus-cli WiFi.SSID.GUEST_RADIO2.BSSID="58:e4:03:d2:70:17"
ubus-cli Device.Ethernet.Link.ethernet_wan.MACAddress="58:E4:03:D2:10:04"
ubus-cli Device.WiFi.SSID.GUEST_RADIO3.MACAddress="58:E4:03:D2:10:50"


# We use WAN for the control interface.
# Add the IP address if there is none yet:
ubus call IP.Interface _get '{ "rel_path": ".[Alias == \"wan\"].IPv4Address.[Alias == \"wan\"]." }' || {
    echo "Adding IP address $IP"
    ubus call "IP.Interface" _add '{ "rel_path": ".[Alias == \"wan\"].IPv4Address.", "parameters": { "Alias": "wan", "AddressingType": "Static" } }'
}
# Configure it:
ubus call "IP.Interface" _set '{ "rel_path": ".[Alias == \"wan\"].IPv4Address.1", "parameters": { "IPAddress": "192.168.250.150", "SubnetMask": "255.255.255.0", "AddressingType": "Static", "Enable" : true } }'
# Enable it:
ubus call "IP.Interface" _set '{ "rel_path": ".[Alias == \"wan\"].", "parameters": { "IPv4Enable": true } }'

# Set the LAN bridge IP:
ubus call "IP.Interface" _set '{ "rel_path": ".[Name == \"br-lan\"].IPv4Address.[Alias == \"lan\"].", "parameters": { "IPAddress": "192.165.100.150" } }'

# Wired backhaul interface:
uci set prplmesh.config.backhaul_wire_iface='lan4'
uci commit

# enable Wi-Fi radios
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "Enable": "true" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "Enable": "true" } }'

# all pwhm default configuration can be found in /etc/amx/wld/wld_defaults.odl.uc

# Restart the ssh server
sh /etc/init.d/ssh-server restart

# Required for config_load:
. /lib/functions/system.sh
# Required for config_foreach:
. /lib/functions.sh

# add private vaps to lan to workaround Netmodel missing wlan mib
# this must be reverted once Netmodel version is integrated
# brctl addif br-lan wlan0 > /dev/null 2>&1 || true
# brctl addif br-lan wlan1 > /dev/null 2>&1 || true

ubus-cli WiFi.AccessPoint.*.DefaultDeviceType="Data"
ubus-cli WiFi.AccessPoint.*.BridgeInterface="br-lan"

ba-cli WiFi.Radio.*.RegulatoryDomain="US"

# Set multiAP profile for primary_vlan_id support
ubus-cli WiFi.AccessPoint.*.MultiAPProfile=3


# Enable when hostapd on this target supports it
ubus-cli "WiFi.AccessPoint.*.MBOEnable=1"

# Make sure specific channels are configured. If channel is set to 0,
# ACS will be configured. If ACS is configured hostapd will refuse to
# switch channels when we ask it to. Channels 1 and 48 were chosen
# because they are NOT used in the WFA certification tests (this
# allows to verify that the device actually switches channel as part
# of the test).
# See also PPM-1928.
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "Channel": "1" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "Channel": "48" } }'

# Restrict channel bandwidth or the certification test could miss beacons
# (see PPM-258)
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "OperatingChannelBandwidth": "20MHz" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "OperatingChannelBandwidth": "20MHz" } }'

# Stop the default ssh server on the lan-bridge
sh /etc/init.d/ssh-server stop || true
sleep 5

# Copy generated SSH host keys
cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
BOOTSCRIPT="/etc/rc.local"
SERVER_CMD="sleep 20 && sh /etc/init.d/ssh-server stop && dropbear -F -T 10 -p192.168.250.150:22 &"
if ! grep -q "$SERVER_CMD" "$BOOTSCRIPT"; then { head -n -2 "$BOOTSCRIPT"; echo "$SERVER_CMD"; tail -2 "$BOOTSCRIPT"; } >> btscript.tmp; mv btscript.tmp "$BOOTSCRIPT"; fi

# Stop and disable the firewall:
sh /etc/init.d/tr181-firewall stop
rm -f /etc/rc.d/S22tr181-firewall

# Start an ssh server on the control interfce
dropbear -F -T 10 -p192.168.250.150:22 &
