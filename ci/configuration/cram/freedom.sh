#!/bin/sh

set -e

# Start with a new log file:
rm -f /var/log/messages && syslog-ng-ctl reload

ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.168.1.150"

# The backhaulWireInterface might not be UP and in br-lan, if previous test was using wifi backhaul (PPM-3361)
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"lan4\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Name == \"lan4\"].Enable=0"
ba-cli "Device.Ethernet.Interface.[Name == \"lan4\"].Enable=1"
ba-cli "Bridging.Bridge.[Alias == \"lan\"].Port.[Name == \"lan4\"].Enable=1"

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
  ba-cli X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="lan4"
fi
