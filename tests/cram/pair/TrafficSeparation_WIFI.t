Load the portable pair helpers and define prplOS-style remote aliases:

  $ . "$TESTDIR/utils.sh"
  $ . "$TESTDIR/wps.sh"
  $ . "$TESTDIR/traffic_separation.sh"
  $ set -o pipefail
  $ shopt -s expand_aliases
  $ alias C='pair_remote controller'
  $ alias A='pair_remote agent'
  $ pair_init wifi

Verify that the expected controller and agent received this build:

  $ pair_preflight
  $ pair_require_variables PAIR_FH_SSID_NAME PAIR_GUEST_SSID_NAME PAIR_BH_SSID_NAME
  $ pair_require_variables TS_CONTROLLER_GUEST_IP TS_AGENT_GUEST_IP

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

Start the agent and configure its single-link 5 GHz endpoint:

  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'" >/dev/null
  $ sleep 5
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=\"Multi-AP-Agent\"'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=1'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1'" >/dev/null
  $ pair_wait_role_ready agent Multi-AP-Agent
  $ pair_resolve_agent_radio
  agent=EP*/WiFi.Radio.*/5GHz (glob)
  $ A "ba-cli 'X_PRPLWARE-COM_Agent.Configuration.BackhaulWireDiscoveryMode=\"StaticList\"'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface=\"cram-disabled\"'" >/dev/null
  $ A "ba-cli '${PAIR_AGENT_RADIO}.RegulatoryDomain=\"US\"'" >/dev/null
  $ A "ba-cli '${PAIR_AGENT_RADIO}.OperatingStandardsFormat=\"Standard\"'" >/dev/null
  $ A "ba-cli '${PAIR_AGENT_RADIO}.OperatingStandards=\"a,n,ac,ax\"'" >/dev/null
  $ A "ba-cli '${PAIR_AGENT_RADIO}.OperatingChannelBandwidth=20MHz'" >/dev/null
  $ if [[ "$PAIR_AGENT_PLATFORM" == ospv2 ]]; then A "ba-cli '${PAIR_AGENT_RADIO}.AutoChannelEnable=0'" >/dev/null; fi
  $ if [[ "$PAIR_AGENT_PLATFORM" == ospv2 ]]; then A "ba-cli '${PAIR_AGENT_RADIO}.STA_Mode=1'" >/dev/null; fi
  $ if [[ "$PAIR_AGENT_PLATFORM" == ospv2 ]]; then A "ba-cli '${PAIR_AGENT_RADIO}.STASupported_Mode=1'" >/dev/null; fi
  $ if [[ "$PAIR_AGENT_PLATFORM" == ospv2 ]]; then certification_ok=0; for attempt in 1 2 3; do certification=$(A "ba-cli 'protected; WiFi.Vendor.ModuleMode.CertificationMode=1'") && ! grep -Eqi '^ERROR:|failed with status|unknown error|not found' <<<"$certification" && { certification_ok=1; break; }; sleep 1; done; test "$certification_ok" = 1; fi
  $ A "ba-cli '${PAIR_AGENT_RADIO}.Enable=1'" >/dev/null
  $ A "ba-cli 'WiFi.EndPoint.${PAIR_AGENT_ENDPOINT}.Enable=1'" >/dev/null
  $ A "ba-cli 'WiFi.EndPoint.${PAIR_AGENT_ENDPOINT}.WPS.Enable=1'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'" >/dev/null
  $ sleep 5
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=\"Multi-AP-Agent\"'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=1'" >/dev/null
  $ A "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1'" >/dev/null
  $ pair_wait_role_ready agent Multi-AP-Agent
  $ pair_wait_wps_ready

Subscribe before invoking WPS PBC on both boards:

  $ pair_start_wps_subscriptions
  subscriptions-ready
  $ controller_wps_ok=0; for attempt in 1 2 3; do controller_wps=$(C "ba-cli -l 'X_PRPLWARE-COM_Agent.WPS.InitiateWPSPBC()'") && ! grep -Eqi '^ERROR:|failed with status|unknown error|not found' <<<"$controller_wps" && pair_wait_wps_activity controller "$PAIR_FH_AP" && { controller_wps_ok=1; break; }; sleep 3; done; test "$controller_wps_ok" = 1
  $ agent_wps_ok=0; for attempt in 1 2 3; do agent_wps=$(A "ba-cli -l 'X_PRPLWARE-COM_Agent.WPS.InitiateWPSPBC()'") && ! grep -Eqi '^ERROR:|failed with status|unknown error|not found' <<<"$agent_wps" && pair_wait_wps_activity agent "$PAIR_AGENT_ENDPOINT" && { agent_wps_ok=1; break; }; sleep 3; done; test "$agent_wps_ok" = 1
  $ pair_wait_agent_endpoint_connected

Remove the wired fallback so autoconfiguration and traffic use Wi-Fi backhaul:

  $ pair_open_network controller
  $ pair_open_network agent
  $ pair_use_wifi_backhaul
  wifi-backhaul-active

Verify that WPS completed and the agent became operational:

  $ pair_wait_wifi_onboarding
  $ A "ba-cli -l 'WiFi.EndPoint.${PAIR_AGENT_ENDPOINT}.ConnectionStatus?'" | tr -d '\n'
  Connected (no-eol)
  $ A "ba-cli -l 'X_PRPLWARE-COM_Agent.Info.CurrentState?'" | tr -d '\n'
  OPERATIONAL.* (re)

Verify private and guest traffic in both directions:

  $ pair_wait_ping controller br-lan "$PAIR_AGENT_IP"
  $ pair_wait_ping controller br-guest "$TS_AGENT_GUEST_IP"
  $ pair_wait_ping agent br-lan "$PAIR_CONTROLLER_IP"
  $ pair_wait_ping agent br-guest "$TS_CONTROLLER_GUEST_IP"
