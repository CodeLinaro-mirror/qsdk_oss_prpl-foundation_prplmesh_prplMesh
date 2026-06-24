Create remote aliasses:

  $ alias C="${CRAM_REMOTE_COMMAND_1:-}"
  $ alias A="${CRAM_REMOTE_COMMAND_2:-}"


#Configure IPs
# IP_1
#$ C ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.168.1.160" | sed '/^$/d'
#$ A ba-cli "IP.Interface.[Name == \"br-lan\"].IPv4Address.lan.IPAddress=192.168.1.1" | sed '/^$/d'


Disable prplMesh on the Controller

  $ C ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":0}}]

  $ sleep 5

Configure prplMesh on the Agent, change its backhaul interface, then disable prplMesh

  $ A ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"CertificationMode":0}}]

  $ A ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode="Multi-AP-Agent" | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"ManagementMode":"Multi-AP-Agent"}}]

  $ A ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":1}}]

  $ sleep 5

  $ A ba-cli -j -l X_PRPLWARE-COM_Agent.Configuration.BackhaulWireInterface="nointerface" | sed '/^$/d'
  [{"X_PRPLWARE-COM_Agent.Configuration.":{"BackhaulWireInterface":"nointerface"}}]

  $ sleep 10

  $ A ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":0}}]

  $ A ba-cli -j -l WiFi.EndPoint.*.ProfileReference="" | sed '/^$/d'
  \[\{"WiFi\.EndPoint\.\d+\.":\{"ProfileReference":""\},"WiFi\.EndPoint\.\d+\.":\{"ProfileReference":""\},"WiFi\.EndPoint\.\d+\.":\{"ProfileReference":""\}\}\] (re)

Turn on radios on Controller and Agent, en/disable the Endpoints

  $ C ba-cli -j -l 'WiFi.Radio.[OperatingFrequencyBand==\"5GHz\"].OperatingChannelBandwidth=\"40MHz\"' | sed '/^$/d'
  \[\{"WiFi\.Radio\.\d+\.":\{"OperatingChannelBandwidth":"40MHz"\}\}\] (re)

  $ C ba-cli -j -l WiFi.EndPoint.*.Enable=0 |grep WiFi
  \[\{"WiFi\.EndPoint\.\d+\.":\{"Enable":0\},"WiFi\.EndPoint\.\d+\.":\{"Enable":0\},"WiFi\.EndPoint\.\d+\.":\{"Enable":0\}\}\] (re)

  $ C ba-cli -j -l WiFi.Radio.*.Enable=1 | sed '/^$/d'
  \[\{"WiFi\.Radio\.\d+\.":\{"Enable":1\},"WiFi\.Radio\.\d+\.":\{"Enable":1\},"WiFi\.Radio\.\d+\.":\{"Enable":1\}\}\] (re)

  $ A ba-cli -j -l WiFi.EndPoint.*.Enable=1 |grep WiFi
  \[\{"WiFi\.EndPoint\.\d+\.":\{"Enable":1\},"WiFi\.EndPoint\.\d+\.":\{"Enable":1\},"WiFi\.EndPoint\.\d+\.":\{"Enable":1\}\}\] (re)

  $ A ba-cli -j -l WiFi.AccessPoint.*.Enable=0 |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.":\{"Enable":0\}(,"WiFi\.AccessPoint\.\d+\.":\{"Enable":0\})*\}\] (re)

  $ A ba-cli -j -l WiFi.Radio.*.Enable=1 | sed '/^$/d'
  \[\{"WiFi\.Radio\.\d+\.":\{"Enable":1\},"WiFi\.Radio\.\d+\.":\{"Enable":1\},"WiFi\.Radio\.\d+\.":\{"Enable":1\}\}\] (re)

  $ sleep 12

Enable prplMesh on Controller

  $ C ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"CertificationMode":0}}]

  $ C ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode="Multi-AP-Controller-and-Agent" | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"ManagementMode":"Multi-AP-Controller-and-Agent"}}]

  $ C ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":1}}]

  $ sleep 15

Configure front and backhaul configuration, using WiFi DM

  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap24ghome\"].Security.KeyPassphrase=\"prplMeshRocks2\"'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.Security\.":\{"KeyPassphrase":"prplMeshRocks2"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap24ghome\"].SSIDReference+.SSID=\"CRAM_2G_FRONTHAUL\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"SSID":"CRAM_2G_FRONTHAUL"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap24ghome\"].SSIDReference+.MLDUnit=\"-1\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap24ghome\"].Enable=1'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.":\{"Enable":1\}\}\] (re)

  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5ghome\"].Security.KeyPassphrase=\"prplMeshRocks\"'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.Security\.":\{"KeyPassphrase":"prplMeshRocks"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5ghome\"].SSIDReference+.SSID=\"CRAM_5G_FRONTHAUL\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"SSID":"CRAM_5G_FRONTHAUL"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5ghome\"].SSIDReference+.MLDUnit=\"-1\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5ghome\"].Enable=1'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.":\{"Enable":1\}\}\] (re)

  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap6ghome\"].Security.KeyPassphrase=\"prplMeshRocks3\"'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.Security\.":\{"KeyPassphrase":"prplMeshRocks3"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap6ghome\"].SSIDReference+.SSID=\"CRAM_6G_FRONTHAUL\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"SSID":"CRAM_6G_FRONTHAUL"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap6ghome\"].SSIDReference+.MLDUnit=\"-1\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap6ghome\"].Enable=1'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.":\{"Enable":1\}\}\] (re)

  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5gguest\"].Security.KeyPassphrase=\"prplMeshRocksGuest\"'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.Security\.":\{"KeyPassphrase":"prplMeshRocksGuest"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5gguest\"].SSIDReference+.SSID=\"CRAM_5G_FRONTHAUL_GUEST\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"SSID":"CRAM_5G_FRONTHAUL_GUEST"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5gguest\"].SSIDReference+.MLDUnit=\"-1\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5gguest\"].Enable=1'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.":\{"Enable":1\}\}\] (re)

  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5gbackhaul\"].Security.KeyPassphrase=\"prplMeshRocks_backhaul\"'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.Security\.":\{"KeyPassphrase":"prplMeshRocks_backhaul"\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5gbackhaul\"].SSIDReference+.MLDUnit=\"-1\"'" |grep WiFi
  \[\{"Device\.WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\}\}\] (re)
  $ C "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5gbackhaul\"].Enable=1'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.":\{"Enable":1\}\}\] (re)

  $ sleep 10

Workaround for QCA issues on extender (set MLDUnits to disable MLO; toggle an AP to enable EP functionality)

  $ A "ba-cli -j -l WiFi.SSID.*.MLDUnit=\"-1\"" |grep WiFi
  \[\{"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\},"WiFi\.SSID\.\d+\.":\{"MLDUnit":\-1\}\}\] (re)

  $ A "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5ghome\"].Enable=1'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.":\{"Enable":1\}\}\] (re)

  $ sleep 5

  $ A "ba-cli -j -l 'protected; WiFi.AccessPoint.[CustomAlias==\"vap5ghome\"].Enable=0'" |grep WiFi
  \[\{"WiFi\.AccessPoint\.\d+\.":\{"Enable":0\}\}\] (re)

  $ sleep 10

Enable prplMesh on Agent

  $ A ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":1}}]

  $ sleep 15

Check Agent is disconnected from controller

  $ A "/opt/prplmesh/bin/prplmesh_cli -c status -o json" | yq -c ".Agent.ControllerConnected, .Agent.CurrentState, .Agent.ManagementMode"
  false
  "WAIT_FOR_BACKHAUL_MANAGER_CONNECTED_NOTIFICATION"
  "Multi-AP-Agent"

  $ A "ba-cli -j -l WiFi.EndPoint.ep5g0.ConnectionStatus?" | sed '/^$/d'
  \[\{"WiFi\.EndPoint\.\d+\.":\{"ConnectionStatus":"(Disconnected|Idle)"\}\}\] (re)

  $ A "(sleep 5; ip link set dev ${BH_IFACE_2} down) 2>&1 & ba-cli -j -l \"X_PRPLWARE-COM_Agent.WPS.InitiateWPSPBC()\"" | sed '/^$/d'
  X_PRPLWARE-COM_Agent.WPS.InitiateWPSPBC() returned
  [""]

  $ C "ba-cli -j -l \"X_PRPLWARE-COM_Agent.WPS.InitiateWPSPBC()\"" | sed '/^$/d'
  X_PRPLWARE-COM_Agent.WPS.InitiateWPSPBC() returned
  [""]

  $ sleep 80

Check if the Agent is connected to the controller

  $ A "/opt/prplmesh/bin/prplmesh_cli -c status -o json" | yq -c ".Agent.ControllerConnected, .Agent.CurrentState, .Agent.ManagementMode"
  true
  "OPERATIONAL"
  "Multi-AP-Agent"

  $ A "ba-cli -j -l WiFi.EndPoint.ep5g0.ConnectionStatus?" |grep WiFi
  \[\{"WiFi.EndPoint\.\d+\.":\{"ConnectionStatus":"Connected"\}\}\] (re)

Check if the Agent is enabling the correct BSSes

  $ A "ba-cli -j -l 'WiFi.SSID.[Status==\"Up\"].SSID?'" | yq -c ".[][].SSID" | tr -d '"' | sort
  CRAM_2G_FRONTHAUL
  CRAM_5G_FRONTHAUL
  CRAM_5G_FRONTHAUL_GUEST
  CRAM_6G_FRONTHAUL
  backhaul_([0-9A-F]{2}:){5}[0-9A-F]{2} (re)
  backhaul_([0-9A-F]{2}:){5}[0-9A-F]{2} (re)

Check if the Agent tears down fronthauls on controller loss

  $ C ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":0}}]

# $ sleep 200

# $ A "ba-cli -j -l WiFi.SSID.[Status==\"Up\"].SSID?" | yq -c ".[][].SSID" | tr -d '"' | sort
# [{}]


Disable prplMesh on Agent

  $ A ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":0}}]

Turn off radios on Controller and Agent
Restore BH ETH connectivity

  $ C ba-cli -j -l WiFi.Radio.*.Enable=0 | sed '/^$/d'
  \[\{"WiFi\.Radio\.\d+\.":\{"Enable":0\},"WiFi\.Radio\.\d+\.":\{"Enable":0\},"WiFi\.Radio\.\d+\.":\{"Enable":0\}\}\] (re)

  $ A "(ip link set dev ${BH_IFACE_2} up) 2>&1 & ba-cli -j -l WiFi.Radio.*.Enable=0; ba-cli -j -l WiFi.EndPoint.*.ProfileReference=\"\"" |grep WiFi
  \[\{"WiFi\.Radio\.\d+\.":\{"Enable":0\},"WiFi\.Radio\.\d+\.":\{"Enable":0\},"WiFi\.Radio\.\d+\.":\{"Enable":0\}\}\] (re)
  \[\{"WiFi\.EndPoint\.\d+\.":\{"ProfileReference":""\},"WiFi\.EndPoint\.\d+\.":\{"ProfileReference":""\},"WiFi\.EndPoint\.\d+\.":\{"ProfileReference":""\}\}\] (re)
