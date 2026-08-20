  $ alias R="${CRAM_REMOTE_COMMAND:-}"

  $ R ba-cli -j -l Device.DeviceInfo.ModelName? | sed '/^$/d'
  [{"Device.DeviceInfo.":{"ModelName":"prpl Foundation Freedom"}}]

Check enabling AP via pWHM:
  $ R "sed -i 's/^use_dataelements_vap_configs=1/use_dataelements_vap_configs=0/' /opt/prplmesh/config/beerocks_controller.conf"

  $ R logger -t cram "Restart prplmesh"

  $ R "ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0" > /dev/null
  $ R "ba-cli X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=Multi-AP-Controller-and-Agent"  > /dev/null
  $ R "ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1" > /dev/null

  $ R "ubus -t 60 wait_for X_PRPLWARE-COM_WiFiController.Network.Device.1"

  $ R "ba-cli 'Device.WiFi.AccessPoint.1.SSIDReference+.SSID="ap1"' >> /dev/null"
  $ R "ba-cli 'Device.WiFi.AccessPoint.2.SSIDReference+.SSID="ap2"' >> /dev/null"
  $ R "ba-cli 'Device.WiFi.AccessPoint.3.SSIDReference+.SSID="ap3"' >> /dev/null"
  $ R "ba-cli 'Device.WiFi.AccessPoint.4.SSIDReference+.SSID="ap4"' >> /dev/null"
  $ R "ba-cli 'Device.WiFi.AccessPoint.5.SSIDReference+.SSID="ap5"' >> /dev/null"
  $ R "ba-cli 'Device.WiFi.AccessPoint.6.SSIDReference+.SSID="ap6"' >> /dev/null"
  $ R "ba-cli 'Device.WiFi.AccessPoint.7.SSIDReference+.SSID="ap7"' >> /dev/null"

  $ R "ba-cli 'Device.WiFi.AccessPoint.*.Enable=1' >> /dev/null"

  $ sleep 10

  $ R "ba-cli 'Device.WiFi.DataElements.Network.Device.*.Radio.*.BSS.?0' -j -l | jsonfilter -e @[0]'[*].SSID' -e @[0]'[*].Enabled' | sort | xargs" 
  1 1 1 1 1 1 1 ap1 ap2 ap3 ap4 ap5 ap6 ap7
