Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"

  $ R ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode="Multi-AP-Controller-and-Agent" | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"ManagementMode":"Multi-AP-Controller-and-Agent"}}]

  $ sleep 10

  $ R "ba-cli -j -l \"Device.WiFi.DataElements.Network.Device.1.Radio.1.AddUnassociatedStation(un_station_mac='AA:BB:CC:DD:12:04',operating_class=81,channel=4)\"" | sed '/^$/d'
  Device.WiFi.DataElements.Network.Device.1.Radio.1.AddUnassociatedStation() returned
  [{}]

  $ sleep 1

  $ R "ba-cli -j -l \"Device.WiFi.DataElements.Network.Device.1.Radio.1.UnassociatedSTA.*.MACAddress?\"" | grep 'aa:bb:cc:dd:12:04'
  [{"Device.WiFi.DataElements.Network.Device.1.Radio.1.UnassociatedSTA.1.":{"MACAddress":"aa:bb:cc:dd:12:04"}}]

  $ R "ba-cli -j -l \"Device.WiFi.DataElements.Network.Device.1.Radio.1.RemoveUnassociatedStation(un_station_mac='AA:BB:CC:DD:12:04')\"" | sed '/^$/d'
  Device.WiFi.DataElements.Network.Device.1.Radio.1.RemoveUnassociatedStation() returned
  [{}]

  $ sleep 1

  $ R "ba-cli -j -l \"Device.WiFi.DataElements.Network.Device.1.Radio.1.UnassociatedSTA.*.MACAddress?\"" | grep 'aa:bb:cc:dd:12:04'
  \[\d+\] (re)

  $ sleep 10

  $ R ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode="Multi-AP-Agent" | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"ManagementMode":"Multi-AP-Agent"}}]
