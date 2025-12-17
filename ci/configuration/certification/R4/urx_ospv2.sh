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

# Stop and disable the firewall:
sh /etc/init.d/tr181-firewall stop || true
rm -f /etc/rc.d/S*tr181-firewall

# Stop and disable the DHCPv4 client: (PPW-888)
sh /etc/init.d/tr181-dhcpv4client stop || true
rm -f /etc/rc.d/*dhcpv4client
pkill -f -9 tr181-dhcpv4client || true

# Stop and disable the DHCP clients and servers:
ba-cli DHCPv6Client.Client.wan.Enable=0
ba-cli DHCPv4Server.Enable=0
ba-cli DHCPv6Server.Enable=0

sleep 2

# Set the LAN bridge IP:
ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.[Alias == \"lan\"].IPAddress=192.165.100.160"

# We use WAN - eth1/sfp for the control interface.
# Add the IP address if there is none yet:
ba-cli "IP.Interface.[Alias == \"wan\"].IPv4Address.[Alias == \"wan\"].?" | grep -Eq "No data found|ERROR" && {
    echo "Adding IP address 192.168.250.160 on WAN"
    ba-cli "IP.Interface.[Alias == \"wan\"].IPv4Address.*.-" || true
    ba-cli "IP.Interface.[Alias == \"wan\"].IPv6Address.*.-" || true
    ba-cli "IP.Interface.[Alias == \"wan\"].IPv6Prefix.*.-" || true
    ba-cli "IP.Interface.[Alias == \"wan\"].IPv4Address.+{Alias=wan, AddressingType=Static, SubnetMask=255.255.255.0, IPAddress=192.168.250.160}"
    ba-cli "IP.Interface.[Alias == \"wan\"].IPv4Address.[Alias == \"wan\"].Enable=1"
}

sleep 5

# Setting BackhaulWireIface, or persistence can fail (PPM-3339)
/etc/init.d/prplmesh stop && sleep 2
/etc/init.d/prplmesh start && sleep 2
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

# Configure Operating Standards
ba-cli "WiFi.Radio.*.OperatingStandardsFormat=\"Standard\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].OperatingStandards=\"b,g\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].OperatingStandards=\"a,n,ac,ax\""
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"6GHz\"].OperatingStandards=\"ax\""

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

# Make sure specific channels are configured. If channel is set to 0,
# ACS will be configured. If ACS is configured hostapd will refuse to
# switch channels when we ask it to. Channels 1 and 48 were chosen
# because they are NOT used in the WFA certification tests (this
# allows to verify that the device actually switches channel as part
# of the test).
# See also PPM-1928.
ba-cli WiFi.Radio.*.AutoChannelEnable=0
#ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].Channel=6"
#ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].Channel=36"

sleep 5

# enable STA-mode on 2.4 and 5GHz
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].STA_Mode=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"2.4GHz\"].STASupported_Mode=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].STA_Mode=1"
ba-cli "WiFi.Radio.[OperatingFrequencyBand == \"5GHz\"].STASupported_Mode=1"

ba-cli "WiFi.set_trace_zone(zone=genHapd, level=500)"
ba-cli "WiFi.set_trace_zone(zone=hapdAP, level=500)"
ba-cli "WiFi.set_trace_zone(zone=chanMgt, level=500)"
ba-cli "WiFi.set_trace_zone(zone=wpaCtrl, level=500)"
ba-cli "WiFi.set_trace_zone(zone=mxlRad, level=500)"


# Reduce DWELL time of channel scans to 20ms
printf "protected\nDevice.WiFi.Vendor.ModuleMode.CertificationMode=1\nexit\n" | ba-cli

# Radio's need to be up to set the antenna configuration (workaroud for missing sniffer captures)
ba-cli WiFi.Radio.*.Enable=1
sleep 10

iw-mxl dev wlan0 iwlwav sCoCPower 0 1 1
sleep 1
iw-mxl dev wlan2 iwlwav sCoCPower 0 1 1

# Commands to start a new SSH server on the control port
start_ssh_commands="iptables -P INPUT ACCEPT
killall -9 dropbear
dropbear -F -T 10 -p192.168.250.160:22 &"

sleep 2

# Copy generated SSH host keys
cp /etc/config/ssh_server/*_key /etc/dropbear/

# Add command to start dropbear to rc.local to allow SSH access after reboot
bootscript="/etc/rc.local"
boot_cmd="sleep 60 && $start_ssh_commands"
if ! grep -q "$boot_cmd" "$bootscript"; then { head -n -2 "$bootscript"; echo "$boot_cmd"; tail -2 "$bootscript"; } >> btscript.tmp; mv btscript.tmp "$bootscript"; fi
set +e && eval "$start_ssh_commands"
