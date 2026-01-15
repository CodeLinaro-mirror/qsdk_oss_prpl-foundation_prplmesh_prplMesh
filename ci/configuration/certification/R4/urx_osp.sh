#!/bin/sh

# We need to source some files which are only available on prplWrt
# devices, so prevent shellcheck from trying to read them:
# shellcheck disable=SC1091

set -e

# Start with a new log file:
rm -f /var/log/messages && syslog-ng-ctl reload

# Stop the default ssh server on the lan-bridge
sh /etc/init.d/ssh-server stop || true
rm -f /etc/rc.d/S*ssh-server

# Disable restarting failing serivces by default
sh /etc/init.d/amx-processmonitor stop || true

ubus wait_for IP.Interface

# Stop and disable the DHCP clients and servers:
if ubus call DHCPv4 _list >/dev/null ; then
  ubus call DHCPv4.Server _set '{"parameters": { "Enable": False }}'
else
    echo "DHCPv4 service not active!"
fi
if ubus call DHCPv6 _list >/dev/null ; then
  ubus call DHCPv6.Server _set '{"parameters": { "Enable": False }}'
else
    echo "DHCPv6 service not active!"
fi

sleep 5

# br-lcm has the same MAC as wlan2.1 since UPDK-9.1.60.1, this causes ebtables problems
# so lets change it's MAC
ifconfig br-lcm hw ether 58:13:d3:02:fa:aa

# The OSP has two "physical WAN" ports: eth0_6 and eth0_0 (left to right)
# The yellow "LAN" ports are: eth0_2, eth0_3, eth0_4, eth0_5 (left to right)
# eth0_0 is in the LAN bridge by default
# We use eth0_6 as WAN (control); and eth0_5 as LAN (prplMesh data)

# Set the LAN bridge IP:
ubus call "IP.Interface" _set '{ "rel_path": ".[Name == \"br-lan\"].IPv4Address.[Alias == \"lan\"].", "parameters": { "IPAddress": "192.165.100.120" } }'
sleep 10

# We use WAN - eth0_6 for the control interface.
# Add the IP address if there is none yet:
ubus call IP.Interface _get '{ "rel_path": ".[Alias == \"wan\"].IPv4Address.[Alias == \"wan\"]." }' || {
    echo "Adding IP address $IP"
    ubus call "IP.Interface" _add '{ "rel_path": ".[Alias == \"wan\"].IPv4Address.", "parameters": { "Alias": "wan", "AddressingType": "Static" } }'
}
# Configure it:
ubus call "IP.Interface" _set '{ "rel_path": ".[Alias == \"wan\"].IPv4Address.1", "parameters": { "IPAddress": "192.168.250.120", "SubnetMask": "255.255.255.0", "AddressingType": "Static", "Enable" : true } }'
# Enable it:
ubus call "IP.Interface" _set '{ "rel_path": ".[Alias == \"wan\"].", "parameters": { "IPv4Enable": true } }'
sleep 5

# Set the wired backhaul interface:
if ba-cli "X_PRPLWARE-COM_Agent.Configuration.?" | grep -Eq "No data found|ERROR"; then
  # Prplmesh agent is not running. Data model isn't up.
  echo "Prplmesh agent is not running"
else
  # Prplmesh agent is running, configure it over the bus
  echo "Setting prplMesh BackhaulWireInterface over DM"
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="eth0_5"
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

# configure private vaps
ubus call "WiFi.SSID.1" _set '{ "parameters": { "SSID": "prplmesh" } }'
ubus call "WiFi.SSID.2" _set '{ "parameters": { "SSID": "prplmesh" } }'
ubus call "WiFi.AccessPoint.1.Security" _set '{ "parameters": { "KeyPassPhrase": "prplmesh_pass" } }'
ubus call "WiFi.AccessPoint.2.Security" _set '{ "parameters": { "KeyPassPhrase": "prplmesh_pass" } }'
ubus call "WiFi.AccessPoint.1.Security" _set '{ "parameters": { "ModeEnabled": "WPA2-Personal" } }'
ubus call "WiFi.AccessPoint.2.Security" _set '{ "parameters": { "ModeEnabled": "WPA2-Personal" } }'

# Unset STA credentials from previous test
ubus-cli WiFi.EndPoint.1.ProfileReference=0
ubus-cli WiFi.EndPoint.1.Enable=0
ubus-cli WiFi.EndPoint.1.Enable=1

ubus-cli "WiFi.AccessPoint.*.MBOEnable=1"
ubus-cli WiFi.AccessPoint.*.DefaultDeviceType="Data"
ubus-cli WiFi.AccessPoint.*.BridgeInterface="br-lan"

# Restrict channel bandwidth or the certification test could miss beacons
# (see PPM-258)
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "OperatingChannelBandwidth": "20MHz" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "OperatingChannelBandwidth": "20MHz" } }'


# Make sure specific channels are configured. If channel is set to 0,
# ACS will be configured. If ACS is configured hostapd will refuse to
# switch channels when we ask it to. Channels 1 and 48 were chosen
# because they are NOT used in the WFA certification tests (this
# allows to verify that the device actually switches channel as part
# of the test).
# See also PPM-1928.
ubus-cli WiFi.Radio.*.AutoChannelEnable=0
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"2.4GHz\"].", "parameters": { "Channel": "6" } }'
ubus call "WiFi.Radio" _set '{ "rel_path": ".[OperatingFrequencyBand == \"5GHz\"].", "parameters": { "Channel": "36" } }'

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

# Remove the default lan/wan SSH servers if they exist
ubus call "SSH.Server" _del '{ "rel_path": ".[Alias == \"lan\"]" }' || true
ubus call "SSH.Server" _del '{ "rel_path": ".[Alias == \"wan\"]" }' || true

ubus-cli "WiFi.set_trace_zone(zone=genHapd, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=hapdAP, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=chanMgt, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=wpaCtrl, level=500)"
ubus-cli "WiFi.set_trace_zone(zone=mxlRad, level=500)"

# Automatic setting is not supported yet
# ubus-cli WiFi.Vendor.ModuleMode.CertificationMode=1

# Traffic Separation Configuration
ifconfig br-guest hw ether 4C:BA:7D:80:AA:BA
ifconfig br-lcm hw ether 4C:BA:7D:80:7E:BA
ubus-cli "WiFi.addVAPIntf(vap=wlan2_3,radio=radio2,bridge=br-lan)"
sleep 3
ubus-cli "WiFi.addVAPIntf(vap=wlan2_4,radio=radio2,bridge=br-lan)"
sleep 3
ubus-cli "WiFi.addVAPIntf(vap=wlan0_3,radio=radio0,bridge=br-lan)"
sleep 3
ubus-cli "WiFi.addVAPIntf(vap=wlan0_4,radio=radio0,bridge=br-lan)"
sleep 3
ubus-cli WiFi.AccessPoint.*.MultiAPProfile=3
sleep 2
ubus-cli WiFi.EndPoint.*.Vendor.MultiApProfile=3
sleep 2
iw dev wlan0 iwlwav sCoCPower 0 1 1
sleep 1
iw dev wlan2 iwlwav sCoCPower 0 1 1

# Trigger the startup of the SSH server
# The SSH server on eth0 has some problems starting through the server component
# Launch a server on the control IP later
# ubus call "SSH.Server" _set '{ "rel_path": ".[Alias == \"control\"].", "parameters": { "Enable": false } }'
# sleep 5
# ubus call "SSH.Server" _set '{ "rel_path": ".[Alias == \"control\"].", "parameters": { "Enable": true } }'

# Stop the default ssh server on the lan-bridge
sh /etc/init.d/ssh-server stop || true
sleep 5

# Copy generated host keys
cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
BOOTSCRIPT="/etc/rc.local"
SERVER_CMD="sleep 20 && sh /etc/init.d/ssh-server stop && iptables -P INPUT ACCEPT && dropbear -F -T 10 -p192.168.250.120:22 &"
if ! grep -q "$SERVER_CMD" "$BOOTSCRIPT"; then { head -n -2 "$BOOTSCRIPT"; echo "$SERVER_CMD"; tail -2 "$BOOTSCRIPT"; } >> btscript.tmp; mv btscript.tmp "$BOOTSCRIPT"; fi

# Stop and disable the firewall:
sh /etc/init.d/tr181-firewall stop
rm -f /etc/rc.d/S22tr181-firewall

iptables -P INPUT ACCEPT

# Start an ssh server on the control interfce
dropbear -F -T 10 -p192.168.250.120:22 &

