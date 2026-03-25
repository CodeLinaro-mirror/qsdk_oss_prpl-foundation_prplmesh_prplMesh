Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ . "${TESTDIR}/../scripts/wifi.sh"

Set AutoChannelEnable=0 on all WiFi.Radio. interfaces:

  $ R "ba-cli -j -l WiFi.Radio.*.AutoChannelEnable=0 | sed '/^$/d'"
  [{"WiFi.Radio.1.":{"AutoChannelEnable":0},"WiFi.Radio.2.":{"AutoChannelEnable":0},"WiFi.Radio.3.":{"AutoChannelEnable":0}}]

Set channel to a non DFS one:

  $ R "ba-cli -j -l WiFi.Radio.2.Channel=36 | sed '/^$/d'"
  [{"WiFi.Radio.2.":{"Channel":36}}]

  $ sleep 5

Configure controller, requires PPM-3022 to work:

  $ R logger -t cram "Stop prplmesh"

  $ R "ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0" | tr -d '\n'
  0 (no-eol)

  $ sleep 2
  $ R "sed -i 's/use_dataelements_vap_configs=0/use_dataelements_vap_configs=1/g' /opt/prplmesh/config/beerocks_controller.conf"

Restart prplmesh:

  $ R logger -t cram "Restart prplmesh"

  $ R "ba-cli X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=Multi-AP-Controller-and-Agent"  > /dev/null
  $ R "ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1" | tr -d '\n'
  1 (no-eol)

  $ R "ubus -t 60 wait_for X_PRPLWARE-COM_WiFiController.Network.Device.1"

First call of AccessPointCommit, controller should push empty config to agents:

  $ R logger -t cram "first call of AccessPointCommit pushes empty config, global teardown"

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network AccessPointCommit"
  {"retval":""}
  {}
  {"amxd-error-code":0}

  $ R sleep 15

Check all AccessPoint.SSIDReference+ instances are disabled

  $ get_ssid_status
  Down
  Down
  Down
  Down
  Down
  Down
  Down
  Down
  Down

  $ get_ssid_ssid
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  prplOS
  prplOS
  prplOS
  prplOS-guest
  prplOS-guest
  prplOS-guest


Create instances of Network.AccessPoint and push them to the agent:

  $ R logger -t cram "create instances of Network.AccessPoint and push them to the agent"

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint _add"
  {"object":"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1.","index":1,"name":"1","parameters":{},"path":"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1."}
  {}
  {"amxd-error-code":0}

Since no persistent storage of NbAPI Network subsection, always index:1 after controller restart:

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1 _set '{\"parameters\":{\"Band2_4G\":1,\"Band5GH\":1,\"Band5GL\":1,\"Band6G\":1}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1.":{"Band5GH":true,"Band6G":true,"Band2_4G":true,"Band5GL":true}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1 _set '{\"parameters\":{\"MultiApMode\":\"Fronthaul+Backhaul\",\"X_PRPLWARE_VapType\":\"home\"}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1.":{"X_PRPLWARE_VapType":"home","MultiApMode":"Fronthaul+Backhaul"}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1.Security _set '{\"parameters\":{\"ModeEnabled\":\"WPA2-Personal\",\"KeyPassphrase\":\"password\"}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1.Security.":{"KeyPassphrase":"password","ModeEnabled":"WPA2-Personal"}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1 _set '{\"parameters\":{\"SSID\":\"prplOSpriv\"}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1.":{"SSID":"prplOSpriv"}}
  {}
  {"amxd-error-code":0}

In case the controller does not yet have this parameter, catch error here isof later during teardown test:
  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1 _set '{\"parameters\":{\"Enable\":1}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1.":{"Enable":true}}
  {}
  {"amxd-error-code":0}

Create second instance of Network.AccessPoint for guest VAPs:

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint _add"
  {"object":"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2.","index":2,"name":"2","parameters":{},"path":"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2."}
  {}
  {"amxd-error-code":0}
  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2 _set '{\"parameters\":{\"Band2_4G\":1,\"Band5GH\":1,\"Band5GL\":1,\"Band6G\":1}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2.":{"Band5GH":true,"Band6G":true,"Band2_4G":true,"Band5GL":true}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2 _set '{\"parameters\":{\"MultiApMode\":\"Fronthaul\",\"X_PRPLWARE_VapType\":\"guest\"}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2.":{"X_PRPLWARE_VapType":"guest","MultiApMode":"Fronthaul"}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2.Security _set '{\"parameters\":{\"ModeEnabled\":\"WPA2-Personal\",\"KeyPassphrase\":\"passwordGUEST\"}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2.Security.":{"KeyPassphrase":"passwordGUEST","ModeEnabled":"WPA2-Personal"}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2 _set '{\"parameters\":{\"SSID\":\"prplOSguest\"}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2.":{"SSID":"prplOSguest"}}
  {}
  {"amxd-error-code":0}

In case the controller does not yet have this parameter, catch error here isof later during teardown test:
  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2 _set '{\"parameters\":{\"Enable\":1}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2.":{"Enable":true}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network AccessPointCommit"
  {"retval":""}
  {}
  {"amxd-error-code":0}


  $ sleep 15

Check that wireless is operating:

  $ get_ssid_status
  Down
  Down
  Down
  Up
  Up
  Up
  Up
  Up
  Up

  $ get_ssid_ssid
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  prplOSguest
  prplOSguest
  prplOSguest
  prplOSpriv
  prplOSpriv
  prplOSpriv

  $ sleep 10

Check that prplmesh processes are running:

  $ R logger -t cram "Check that prplmesh processes are running"

  $ R "ps axw" | sed -nE 's/.*(\/opt\/prplmesh\/bin.*)/\1/p' | LC_ALL=C sort
  /opt/prplmesh/bin/beerocks_agent
  /opt/prplmesh/bin/beerocks_controller
  /opt/prplmesh/bin/beerocks_fronthaul -i wlan0
  /opt/prplmesh/bin/beerocks_fronthaul -i wlan1
  /opt/prplmesh/bin/beerocks_fronthaul -i wlan2
  /opt/prplmesh/bin/beerocks_vendor_message
  /opt/prplmesh/bin/ieee1905_transport

Check that prplmesh is operational:

  $ R logger -t cram "Check that prplmesh is operational"

  $ R "/opt/prplmesh/bin/prplmesh_cli -c status -o pretty" | sed 's/\t/        /g'
  Mode: Agent+Controller
  Controller:
          bridge MAC: [0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
          1 agent(s) connected
  Agent:
          MAC address: [0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
          management mode: Multi-AP-Controller-and-Agent
          fronthaul ifaces: wlan0,wlan1,wlan2
          current state: OPERATIONAL
          best state: OPERATIONAL
          Fronthaul:
                  interface: wlan0
                  current state: OPERATIONAL
                  best state: OPERATIONAL
          Fronthaul:
                  interface: wlan1
                  current state: OPERATIONAL
                  best state: OPERATIONAL
          Fronthaul:
                  interface: wlan2
                  current state: OPERATIONAL
                  best state: OPERATIONAL

Check that controller received correct info about wifi subsystem:

  $ R logger -t cram "Check controller info about network"

  $ R "/opt/prplmesh/bin/beerocks_cli -c bml_conn_map" | egrep '(wlan|OK)' | sed -E "s/.*: (wlan[0-9.]+) .*/\1/" | LC_ALL=C sort
  bml_connect: return value is: BML_RET_OK, Success status
  bml_disconnect: return value is: BML_RET_OK, Success status
  bml_nw_map_query: return value is: BML_RET_OK, Success status
  wlan0
  wlan0.0
  wlan0.1
  wlan1
  wlan1.0
  wlan1.1
  wlan1.2
  wlan2
  wlan2.0
  wlan2.1
  wlan2.2

To disable wireless, disable instances of Network.AccessPoint{i} and call AccessPointCommit():

  $ R logger -t cram "Stop wireless"

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1 _set '{\"parameters\":{\"Enable\":0}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.1.":{"Enable":false}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2 _set '{\"parameters\":{\"Enable\":0}}'"
  {"X_PRPLWARE-COM_WiFiController.Network.AccessPoint.2.":{"Enable":false}}
  {}
  {"amxd-error-code":0}

  $ R "ubus -S call X_PRPLWARE-COM_WiFiController.Network AccessPointCommit"
  {"retval":""}
  {}
  {"amxd-error-code":0}

  $ sleep 10

Restore Security.ModeEnabled for AccessPoints used in the test

  $ R "ba-cli  \"WiFi.AccessPoint.[RadioReference == 'Device.WiFi.Radio.1'].Security.ModeEnabled='WPA3-Personal-Transition'\"" | grep 'ModeEnabled=' | sed '/^$/d' | grep -v '>'
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)

  $ R "ba-cli  \"WiFi.AccessPoint.[RadioReference == 'Device.WiFi.Radio.2'].Security.ModeEnabled='WPA3-Personal-Transition'\"" | grep 'ModeEnabled=' | sed '/^$/d' | grep -v '>'
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)

  $ R "ba-cli  \"WiFi.AccessPoint.[RadioReference == 'Device.WiFi.Radio.3'].Security.ModeEnabled='WPA3-Personal'\"" | grep 'ModeEnabled=' | sed '/^$/d' | grep -v '>'
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal" (re)

Check that wireless is disabled:

  $ get_ssid_status
  Down
  Down
  Down
  Down
  Down
  Down
  Down
  Down
  Down

Check that SSIDs did not change:

  $ get_ssid_ssid
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  prplOSguest
  prplOSguest
  prplOSguest
  prplOSpriv
  prplOSpriv
  prplOSpriv

Restore Security Mode to default values

  $ R "ba-cli  \"WiFi.AccessPoint.[RadioReference == 'Device.WiFi.Radio.1'].Security.ModeEnabled='WPA3-Personal-Transition'\"" | grep 'ModeEnabled=' | sed '/^$/d' | grep -v '>'
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)

  $ R "ba-cli  \"WiFi.AccessPoint.[RadioReference == 'Device.WiFi.Radio.2'].Security.ModeEnabled='WPA3-Personal-Transition'\"" | grep 'ModeEnabled=' | sed '/^$/d' | grep -v '>'
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal-Transition" (re)

  $ R "ba-cli  \"WiFi.AccessPoint.[RadioReference == 'Device.WiFi.Radio.3'].Security.ModeEnabled='WPA3-Personal'\"" | grep 'ModeEnabled=' | sed '/^$/d' | grep -v '>'
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal" (re)
  WiFi.AccessPoint.\d+.Security.ModeEnabled="WPA3-Personal" (re)
