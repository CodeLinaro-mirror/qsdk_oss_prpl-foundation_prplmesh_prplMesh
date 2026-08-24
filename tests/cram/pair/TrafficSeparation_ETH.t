Load the portable pair helpers and define prplOS-style remote aliases:

  $ . "$TESTDIR/utils.sh"
  $ . "$TESTDIR/wps.sh"
  $ . "$TESTDIR/traffic_separation.sh"
  $ set -o pipefail
  $ shopt -s expand_aliases
  $ alias C='pair_remote controller'
  $ alias A='pair_remote agent'
  $ pair_init eth

Verify that the expected controller and agent received this build:

  $ pair_preflight
  $ pair_require_variables PAIR_FH_SSID_NAME PAIR_GUEST_SSID_NAME PAIR_BH_SSID_NAME
  $ pair_require_variables TS_CONTROLLER_WIRED TS_CONTROLLER_GUEST_IP TS_AGENT_GUEST_IP

Start prplMesh in controller-only mode before changing controller VAPs:

  $ C "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'" >/dev/null
  $ sleep 5
  $ C "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=\"Multi-AP-Controller\"'" >/dev/null
  $ C "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=1'" >/dev/null
  $ C "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1'" >/dev/null
  $ pair_wait_role_ready controller Multi-AP-Controller

Discover the controller's 5 GHz home, guest and backhaul VAPs:

  $ pair_resolve_controller_layout 1
  home=AP*/*home* guest=AP*/*guest* backhaul=AP*/*backhaul* (glob)

Configure the 5 GHz home VAP:

  $ C "ba-cli '${PAIR_FH_SSID}.SSID=\"${PAIR_FH_SSID_NAME}\"'" >/dev/null
  $ C "ba-cli 'WiFi.AccessPoint.${PAIR_FH_AP}.Enable=1'" >/dev/null

Configure the distinct 5 GHz backhaul VAP:

  $ C "ba-cli '${PAIR_BH_SSID}.SSID=\"${PAIR_BH_SSID_NAME}\"'" >/dev/null
  $ C "ba-cli 'WiFi.AccessPoint.${PAIR_BH_AP}.Enable=1'" >/dev/null

Configure the 5 GHz guest VAP:

  $ C "ba-cli '${PAIR_GUEST_SSID}.SSID=\"${PAIR_GUEST_SSID_NAME}\"'" >/dev/null
  $ C "ba-cli 'WiFi.AccessPoint.${PAIR_GUEST_AP}.Enable=1'" >/dev/null

Start prplMesh on the controller and verify the required APs:

  $ C "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'" >/dev/null
  $ sleep 5
  $ C "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=\"Multi-AP-Controller-and-Agent\"'" >/dev/null
  $ C "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=1'" >/dev/null
  $ C "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1'" >/dev/null
  $ pair_wait_role_ready controller Multi-AP-Controller-and-Agent
  $ pair_wait_controller_vaps 1
  $ C "ba-cli -l 'WiFi.AccessPoint.${PAIR_FH_AP}.Status?'" | tr -d '\n'
  Enabled (no-eol)
  $ C "ba-cli -l 'WiFi.AccessPoint.${PAIR_BH_AP}.Status?'" | tr -d '\n'
  Enabled (no-eol)
  $ C "ba-cli -l 'WiFi.AccessPoint.${PAIR_GUEST_AP}.Status?'" | tr -d '\n'
  Enabled (no-eol)

Apply the private and guest Traffic Separation policy:

  $ C "ba-cli 'X_PRPLWARE-COM_Controller.Configuration.TrafficSeparation.PrivateVID=${PAIR_PRIVATE_VID}'" >/dev/null
  $ C "ba-cli 'X_PRPLWARE-COM_Controller.Configuration.TrafficSeparation.GuestVID=${PAIR_GUEST_VID}'" >/dev/null
  $ C "ba-cli 'X_PRPLWARE-COM_Controller.Configuration.TrafficSeparation.Enable=1'" >/dev/null
  $ pair_wait_traffic_separation

Start the agent with Ethernet backhaul:

  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'" >/dev/null
  $ sleep 5
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=\"Multi-AP-Agent\"'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=1'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1'" >/dev/null
  $ pair_wait_role_ready agent Multi-AP-Agent
  $ A "ip link set dev '${TS_AGENT_WIRED}' up"
  $ A "ba-cli 'X_PRPLWARE-COM_Agent.Configuration.BackhaulWireDiscoveryMode=\"StaticList\"'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface=\"${TS_AGENT_WIRED}\"'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'" >/dev/null
  $ sleep 5
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=\"Multi-AP-Agent\"'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=1'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1'" >/dev/null
  $ pair_wait_role_ready agent Multi-AP-Agent

Verify private and guest traffic in both directions:

  $ pair_open_network controller
  $ pair_open_network agent
  $ pair_wait_ping controller br-lan "$PAIR_AGENT_IP"
  $ pair_wait_ping controller br-guest "$TS_AGENT_GUEST_IP"
  $ pair_wait_ping agent br-lan "$PAIR_CONTROLLER_IP"
  $ pair_wait_ping agent br-guest "$TS_CONTROLLER_GUEST_IP"
