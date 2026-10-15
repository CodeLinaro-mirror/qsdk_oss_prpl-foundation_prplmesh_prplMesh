Load the portable pair helpers and define prplOS-style remote aliases:

  $ . "$TESTDIR/utils.sh"
  $ . "$TESTDIR/wps.sh"
  $ . "$TESTDIR/ieee1905.sh"
  $ set -o pipefail
  $ shopt -s expand_aliases
  $ alias C='pair_remote controller'
  $ alias A='pair_remote agent'
  $ pair_init ieee1905-wifi

Bring-up. Setup: the GW (controller board in Multi-AP-Controller-and-Agent mode) onboards an EasyMesh device (agent board in Multi-AP-Agent mode) over Wi-Fi with WPS, and then the wired link between the boards is removed.

Verify that the expected controller and agent received this build:

  $ pair_preflight
  $ pair_require_variables PAIR_FH_SSID_NAME PAIR_BH_SSID_NAME

Start prplMesh in controller-only mode before changing controller VAPs:

  $ pair_restart_prplmesh controller Multi-AP-Controller

Discover the controller's 5 GHz fronthaul and backhaul VAPs:

  $ pair_resolve_controller_layout 0
  home=AP*/*home* backhaul=AP*/*backhaul* (glob)

Configure the 5 GHz fronthaul and backhaul VAPs:

  $ C "ba-cli '${PAIR_FH_SSID}.SSID=\"${PAIR_FH_SSID_NAME}\"'" >/dev/null
  $ C "ba-cli 'WiFi.AccessPoint.${PAIR_FH_AP}.Enable=1'" >/dev/null
  $ C "ba-cli '${PAIR_BH_SSID}.SSID=\"${PAIR_BH_SSID_NAME}\"'" >/dev/null
  $ C "ba-cli 'WiFi.AccessPoint.${PAIR_BH_AP}.Enable=1'" >/dev/null

Start prplMesh as controller and agent on the controller board:

  $ pair_restart_prplmesh controller Multi-AP-Controller-and-Agent
  $ pair_wait_controller_vaps 0

Start the agent and configure its single-link 5 GHz endpoint:

  $ pair_restart_prplmesh agent Multi-AP-Agent
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

Restart the agent so it consumes the backhaul configuration:

  $ pair_restart_prplmesh agent Multi-AP-Agent
  $ pair_wait_wps_ready

Subscribe before invoking WPS PBC on both boards:

  $ pair_start_wps_subscriptions
  subscriptions-ready
  $ controller_wps_ok=0; for attempt in 1 2 3; do controller_wps=$(C "ba-cli -l 'X_PRPLWARE-COM_Agent.WPS.InitiateWPSPBC()'") && ! grep -Eqi '^ERROR:|failed with status|unknown error|not found' <<<"$controller_wps" && pair_wait_wps_activity controller "$PAIR_FH_AP" && { controller_wps_ok=1; break; }; sleep 3; done; test "$controller_wps_ok" = 1
  $ agent_wps_ok=0; for attempt in 1 2 3; do agent_wps=$(A "ba-cli -l 'X_PRPLWARE-COM_Agent.WPS.InitiateWPSPBC()'") && ! grep -Eqi '^ERROR:|failed with status|unknown error|not found' <<<"$agent_wps" && pair_wait_wps_activity agent "$PAIR_AGENT_ENDPOINT" && { agent_wps_ok=1; break; }; sleep 3; done; test "$agent_wps_ok" = 1

Wait for the Wi-Fi association, then remove the wired fallback:

  $ pair_wait_agent_endpoint_connected
  $ pair_open_network controller
  $ pair_open_network agent
  $ pair_use_wifi_backhaul
  wifi-backhaul-active

Verify that WPS completed and the agent is operational on Wi-Fi backhaul:

  $ pair_wait_wifi_onboarding
  $ A "ba-cli -l 'WiFi.EndPoint.${PAIR_AGENT_ENDPOINT}.ConnectionStatus?'" | grep -Ev '^>|^$'
  Connected

Wi-Fi backhaul checks. Setup D of the test plan: GW (Controller+Agent mode) --(wireless)-- EasyMesh device (Extender). IEEE1905.t runs the shared data model and update checks next, with PAIR_IEEE1905_BACKHAUL=wifi.

Read the AL MAC addresses (the br-lan MAC addresses) and the controller backhaul BSSID:

  $ PAIR_CONTROLLER_MAC=$(pair_bridge_mac controller)
  $ PAIR_AGENT_MAC=$(pair_bridge_mac agent)
  $ PAIR_BH_BSSID=$(pair_lower "$(pair_remote_value controller "${PAIR_BH_SSID}.BSSID")")
  $ echo "$PAIR_BH_BSSID"
  ([0-9a-f]{2}:){5}[0-9a-f]{2} (re)

Wait until the stale wired neighbor is gone and both ALs see each other only over Wi-Fi:

  $ pair_wait_ieee1905 '^IEEE_802_11'

Check the agent's Wi-Fi backhaul interface: NetworkMembership is the controller backhaul BSSID and the role is a STA (test plan cases 36, 37):

  $ pair_check_equal "$PAIR_AGENT_IFACE.NetworkMembership" "$(pair_lower "$(pair_dm_value "$PAIR_AGENT_IFACE.NetworkMembership")")" "$PAIR_BH_BSSID"
  ok
  $ pair_dm_value "$PAIR_AGENT_IFACE.Role"
  non-AP/non-PCP STA

Best effort: check that the controller's interface toward the agent is the backhaul AP:

  $ pair_dm_value "$PAIR_CONTROLLER_IFACE.Role"
  AP
  $ pair_check_equal "$PAIR_CONTROLLER_IFACE.InterfaceId" "$(pair_lower "$(pair_dm_value "$PAIR_CONTROLLER_IFACE.InterfaceId")")" "$PAIR_BH_BSSID"
  ok

Best effort: wait for link metrics, then check the RSSI of the Wi-Fi link (test plan case 28):

  $ pair_wait_ieee1905_links '^IEEE_802_11'
  $ pair_dm_value "$PAIR_CONTROLLER_LINK.Metric.RSSI"
  [1-9][0-9]* (re)

Keep the wired link down for IEEE1905.t, which brings it back up when it exits:

  $ PAIR_AGENT_WIRED_RESTORE=0
