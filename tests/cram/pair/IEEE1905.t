Load the portable pair helpers and define prplOS-style remote aliases:

  $ . "$TESTDIR/utils.sh"
  $ . "$TESTDIR/ieee1905.sh"
  $ set -o pipefail
  $ shopt -s expand_aliases
  $ alias C='pair_remote controller'
  $ alias A='pair_remote agent'
  $ pair_init "ieee1905-${PAIR_IEEE1905_BACKHAUL:-}"

These checks run right after IEEE1905_ETH.t or IEEE1905_WIFI.t brought the pair up. PAIR_IEEE1905_BACKHAUL (eth or wifi) selects the backhaul MediaType that the waits expect:

  $ pair_ieee1905_use_backhaul "$PAIR_IEEE1905_BACKHAUL"

IEEE1905_WIFI.t leaves the wired link down for these checks; bring it back up when this file exits:

  $ PAIR_AGENT_WIRED_RESTORE=1

Read the AL MAC addresses, which are the br-lan MAC addresses of both devices:

  $ PAIR_CONTROLLER_MAC=$(pair_bridge_mac controller)
  $ PAIR_AGENT_MAC=$(pair_bridge_mac agent)

TODO: update the expected MAC addresses for the target devices.

  $ echo "$PAIR_CONTROLLER_MAC"
  52:54:27:52:00:01
  $ echo "$PAIR_AGENT_MAC"
  52:54:de:8e:00:01

Wait until the controller IEEE1905 model contains both devices, and each AL has exactly one neighbor interface with the backhaul MediaType:

  $ pair_wait_ieee1905 "$PAIR_IEEE1905_MEDIA"

Data model checks. The controller board is the GW (Controller+Agent mode) and the agent board is the EasyMesh device, which the test plan also calls the IEEE1905 device.
Over Ethernet this is Setup B of the test plan; over Wi-Fi it is Setup D.
Cases written for other setups, and the default setup cases, are adapted to this pair.

Check the IEEE1905 network defaults (test plan cases Default 1, Default 2, 19, 29):

  $ pair_dm_value IEEE1905.Network.Enable
  1
  $ pair_dm_value IEEE1905.Network.Status
  Available
  $ pair_dm_value IEEE1905.Network.ALNumberOfEntries
  2

Check that both devices are registered with their br-lan MAC addresses (test plan cases 1, 19, 40):

TODO: update the expected MAC addresses for the target devices.

  $ pair_lower "$(pair_dm_value "$PAIR_CONTROLLER_AL.IEEE1905Id")"
  52:54:27:52:00:01
  $ pair_lower "$(pair_dm_value "$PAIR_AGENT_AL.IEEE1905Id")"
  52:54:de:8e:00:01
  $ test "$PAIR_CONTROLLER_AL" != "$PAIR_AGENT_AL" && echo ok
  ok

Check the agent AL information against the agent's own data model (test plan cases 41, 42, 43, 44, 45, 46):

  $ pair_dm_value "$PAIR_AGENT_AL.Version"
  1905\.1a? (re)
  $ pair_check_freq_bands "$(pair_dm_value "$PAIR_AGENT_AL.RegistrarFreqBand")"
  ok
  $ pair_check_equal 'agent AL FriendlyName' "$(pair_dm_value "$PAIR_AGENT_AL.FriendlyName")" "$(pair_remote_value agent Device.DeviceInfo.FriendlyName)"
  ok
  $ pair_check_equal 'agent AL ManufacturerName' "$(pair_dm_value "$PAIR_AGENT_AL.ManufacturerName")" "$(pair_remote_value agent Device.DeviceInfo.Manufacturer)"
  ok
  $ pair_check_equal 'agent AL ManufacturerModel' "$(pair_dm_value "$PAIR_AGENT_AL.ManufacturerModel")" "$(pair_remote_value agent Device.DeviceInfo.ModelName)"
  ok
  $ pair_dm_value "$PAIR_AGENT_AL.ControlURL" >/dev/null && echo ok
  ok

Check that the agent AL refers to the agent's DataElements device (test plan cases 8, Setup C AI test 3):

  $ agent_device_ref=$(pair_dm_path "$(pair_dm_value "$PAIR_AGENT_AL.AssocWiFiNetworkDeviceRef")")
  $ echo "$agent_device_ref"
  WiFi\.DataElements\.Network\.Device\.[0-9]+ (re)

TODO: update the expected MAC addresses for the target devices.

  $ pair_lower "$(pair_remote_value controller "Device.${agent_device_ref}.ID")"
  52:54:de:8e:00:01
  $ pair_remote_value controller Device.WiFi.DataElements.Network.DeviceNumberOfEntries
  2

Check that the controller AL has exactly one neighbor, which points to the agent AL (test plan cases 20, 24 over Ethernet; 34, 38 over Wi-Fi):

  $ pair_ieee1905_neighbor_iface_count "$PAIR_CONTROLLER_AL"
  1
  $ pair_dm_value "$PAIR_CONTROLLER_IFACE.IEEE1905NeighborNumberOfEntries"
  1
  $ pair_check_equal "$PAIR_CONTROLLER_NEIGHBOR.IEEE1905DeviceRef" "$(pair_dm_path "$(pair_dm_value "$PAIR_CONTROLLER_NEIGHBOR.IEEE1905DeviceRef")")" "$PAIR_AGENT_AL"
  ok
  $ pair_dm_value "$PAIR_CONTROLLER_IFACE.MediaType"
  IEEE_802_(3|11).* (re)

Check that the agent AL has exactly one neighbor, which points to the controller AL (test plan cases 22, 25 over Ethernet; 35, 39 over Wi-Fi):

  $ pair_ieee1905_neighbor_iface_count "$PAIR_AGENT_AL"
  1
  $ pair_dm_value "$PAIR_AGENT_IFACE.IEEE1905NeighborNumberOfEntries"
  1
  $ pair_check_equal "$PAIR_AGENT_NEIGHBOR.IEEE1905DeviceRef" "$(pair_dm_path "$(pair_dm_value "$PAIR_AGENT_NEIGHBOR.IEEE1905DeviceRef")")" "$PAIR_CONTROLLER_AL"
  ok
  $ pair_dm_value "$PAIR_AGENT_IFACE.MediaType"
  IEEE_802_(3|11).* (re)

Wait for link metrics, then check the Link objects in both directions (test plan cases 26, 27, 28):

  $ pair_wait_ieee1905_links "$PAIR_IEEE1905_MEDIA"
  $ pair_check_equal "$PAIR_CONTROLLER_LINK.InterfaceId" "$(pair_lower "$(pair_dm_value "$PAIR_CONTROLLER_LINK.InterfaceId")")" "$(pair_lower "$(pair_dm_value "$PAIR_AGENT_IFACE.InterfaceId")")"
  ok
  $ pair_dm_value "$PAIR_CONTROLLER_LINK.MediaType"
  IEEE_802_(3|11).* (re)
  $ pair_check_equal "$PAIR_AGENT_LINK.InterfaceId" "$(pair_lower "$(pair_dm_value "$PAIR_AGENT_LINK.InterfaceId")")" "$(pair_lower "$(pair_dm_value "$PAIR_CONTROLLER_IFACE.InterfaceId")")"
  ok
  $ pair_dm_value "$PAIR_AGENT_LINK.MediaType"
  IEEE_802_(3|11).* (re)
  $ pair_dm_value "$PAIR_CONTROLLER_LINK.Metric.MACThroughputCapacity"
  [0-9]+ (re)
  $ pair_dm_value "$PAIR_CONTROLLER_LINK.Metric.PHYRate"
  [0-9]+ (re)

Check the agent IPv4 address from the Higher Layer response (test plan case 49):

  $ pair_dm_value "$PAIR_AGENT_AL.IPv4AddressNumberOfEntries"
  [1-9][0-9]* (re)
  $ pair_check_ipv4 "$PAIR_AGENT_AL" "$PAIR_AGENT_IP" "$PAIR_AGENT_MAC"
  ok

Check that the agent bridging tuples reference the agent's own interfaces (test plan case 56):

  $ pair_dm_value "$PAIR_AGENT_AL.BridgingTupleNumberOfEntries"
  [1-9][0-9]* (re)
  $ pair_check_bridging_tuples "$PAIR_AGENT_AL"
  ok

Check that disabling the model clears both ALs and enabling it rebuilds them (test plan case Update 7):

  $ C "ba-cli -l 'IEEE1905.Network.Enable=0'" | grep -Ev '^>|^$'
  0
  $ C "ba-cli -l 'IEEE1905.Network.ALNumberOfEntries?'" | grep -Ev '^>|^$'
  0
  $ C "ba-cli -l 'IEEE1905.Network.Enable=1'" | grep -Ev '^>|^$'
  1
  $ pair_wait_ieee1905 "$PAIR_IEEE1905_MEDIA"

Update checks (test plan "Testcases for update of parameters"). Setup: the same GW and EasyMesh device over the same backhaul; the steps change values on the devices and stop and restart prplMesh on both of them. The agent is stopped and started by a detached script on the agent, because SSH to the agent may run over the Wi-Fi backhaul.

Shorten the controller higher layer request interval so that agent updates show up quickly (test plan group 6):

  $ ctrl_cfg=X_PRPLWARE-COM_Controller.Configuration
  $ orig_hl_interval=$(pair_remote_value controller "$ctrl_cfg.HigherLayerRequestIntervalSec")
  $ echo "$orig_hl_interval"
  60
  $ C "ba-cli '$ctrl_cfg.HigherLayerRequestIntervalSec=${PAIR_IEEE1905_HL_INTERVAL_SEC}'" >/dev/null
  $ pair_check_equal "$ctrl_cfg.HigherLayerRequestIntervalSec" "$(pair_remote_value controller "$ctrl_cfg.HigherLayerRequestIntervalSec")" "$PAIR_IEEE1905_HL_INTERVAL_SEC"
  ok

Check that a FriendlyName change on the agent reaches the agent AL, then restore it (test plan case Update 1):

  $ orig_friendly_name=$(pair_remote_value agent Device.DeviceInfo.FriendlyName)
  $ pair_check_equal 'agent AL FriendlyName' "$(pair_dm_value "$PAIR_AGENT_AL.FriendlyName")" "$orig_friendly_name"
  ok
  $ new_friendly_name=cram-ieee1905
  $ pair_set_remote_string agent Device.DeviceInfo.FriendlyName "$new_friendly_name"
  $ pair_remote_value agent Device.DeviceInfo.FriendlyName
  cram-ieee1905
  $ pair_wait_ieee1905_agent_value FriendlyName "$new_friendly_name"
  $ pair_dm_value "$PAIR_AGENT_AL.FriendlyName"
  cram-ieee1905
  $ pair_set_remote_string agent Device.DeviceInfo.FriendlyName "$orig_friendly_name"
  $ pair_wait_ieee1905_agent_value FriendlyName "$orig_friendly_name"
  $ pair_check_equal 'restored agent AL FriendlyName' "$(pair_dm_value "$PAIR_AGENT_AL.FriendlyName")" "$orig_friendly_name"
  ok

Check that an IPv4 address added to the agent br-lan is reported and that removing it clears the entry (test plan case Update 2):

  $ A "ip addr add '${PAIR_IEEE1905_EXTRA_IPV4}/32' dev br-lan"
  $ pair_wait_ieee1905_agent_ipv4 present "$PAIR_IEEE1905_EXTRA_IPV4"
  $ pair_check_ipv4 "$PAIR_AGENT_AL" "$PAIR_IEEE1905_EXTRA_IPV4" "$PAIR_AGENT_MAC"
  ok
  $ pair_check_ipv4 "$PAIR_AGENT_AL" "$PAIR_AGENT_IP" "$PAIR_AGENT_MAC"
  ok
  $ A "ip addr del '${PAIR_IEEE1905_EXTRA_IPV4}/32' dev br-lan"
  $ pair_wait_ieee1905_agent_ipv4 absent "$PAIR_IEEE1905_EXTRA_IPV4"
  $ pair_check_ipv4 "$PAIR_AGENT_AL" "$PAIR_IEEE1905_EXTRA_IPV4" "$PAIR_AGENT_MAC" >/dev/null || echo ok
  ok
  $ pair_check_ipv4 "$PAIR_AGENT_AL" "$PAIR_AGENT_IP" "$PAIR_AGENT_MAC"
  ok

Stop the agent for 30 seconds and start it again, then check that it is registered only once (test plan case Update 6):

  $ pair_agent_prplmesh_bounce 30
  $ sleep 35
  $ pair_wait_agent_prplmesh_bounce 30
  $ pair_wait_ieee1905 "$PAIR_IEEE1905_MEDIA"
  $ sleep "$PAIR_IEEE1905_SETTLE_SEC"
  $ pair_wait_ieee1905 "$PAIR_IEEE1905_MEDIA"
  $ pair_ieee1905_al_count "$PAIR_AGENT_MAC"
  1
  $ pair_dm_value IEEE1905.Network.ALNumberOfEntries
  2

Stop the agent and check that the controller removes its AL and neighbor entries while the agent is down (test plan case Update 3):

  $ pair_agent_prplmesh_bounce "$PAIR_IEEE1905_AGENT_DOWN_SEC"
  $ pair_wait_ieee1905_agent_gone $((PAIR_IEEE1905_AGENT_DOWN_SEC - 30))
  $ pair_dm_value IEEE1905.Network.ALNumberOfEntries
  1

TODO: update the expected MAC addresses for the target devices.

  $ pair_lower "$(pair_dm_value "$PAIR_CONTROLLER_AL.IEEE1905Id")"
  52:54:27:52:00:01
  $ pair_ieee1905_al_count "$PAIR_AGENT_MAC"
  0
  $ pair_ieee1905_neighbor_iface_count "$PAIR_CONTROLLER_AL"
  0

Wait for the agent to start again and check that it is registered with neighbor references in both directions (test plan cases 19, 24, 25 over Ethernet; 19, 38, 39 over Wi-Fi):

  $ pair_wait_agent_prplmesh_bounce "$PAIR_IEEE1905_AGENT_DOWN_SEC"
  $ pair_wait_ieee1905 "$PAIR_IEEE1905_MEDIA"
  $ pair_dm_value IEEE1905.Network.ALNumberOfEntries
  2
  $ pair_ieee1905_al_count "$PAIR_AGENT_MAC"
  1
  $ pair_check_equal "$PAIR_CONTROLLER_NEIGHBOR.IEEE1905DeviceRef" "$(pair_dm_path "$(pair_dm_value "$PAIR_CONTROLLER_NEIGHBOR.IEEE1905DeviceRef")")" "$PAIR_AGENT_AL"
  ok
  $ pair_check_equal "$PAIR_AGENT_NEIGHBOR.IEEE1905DeviceRef" "$(pair_dm_path "$(pair_dm_value "$PAIR_AGENT_NEIGHBOR.IEEE1905DeviceRef")")" "$PAIR_CONTROLLER_AL"
  ok

Restart prplMesh on the controller and check that the model is rebuilt with the same number of ALs (test plan case Update 5, adapted from a full reboot):

  $ pair_dm_value IEEE1905.Network.ALNumberOfEntries
  2
  $ pair_restart_prplmesh controller Multi-AP-Controller-and-Agent
  $ pair_wait_ieee1905 "$PAIR_IEEE1905_MEDIA"
  $ pair_dm_value IEEE1905.Network.ALNumberOfEntries
  2
  $ pair_ieee1905_al_count "$PAIR_AGENT_MAC"
  1

Restore the controller higher layer request interval:

  $ C "ba-cli '$ctrl_cfg.HigherLayerRequestIntervalSec=${orig_hl_interval}'" >/dev/null
  $ pair_remote_value controller "$ctrl_cfg.HigherLayerRequestIntervalSec"
  60
