Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"

  $ R ba-cli 'Device.WiFi.DataElements.Network.Device.1.Radio.1.AddUnassociatedStation(un_station_mac="AA:BB:CC:DD:12:04",operating_class=81,channel=4)'
  > Device.WiFi.DataElements.Network.Device.1.Radio.1.AddUnassociatedStation(un_station_mac="AA:BB:CC:DD:12:04",operating_class=81,channel=4)
  Device.WiFi.DataElements.Network.Device.1.Radio.1.AddUnassociatedStation() returned
  [
      {
      }
  ]

  $ sleep 1

  $ R ba-cli 'Device.WiFi.DataElements.Network.Device.1.Radio.1.UnassociatedSTA.*.MACAddress?' | grep 'aa:bb:cc:dd:12:04'
  Device.WiFi.DataElements.Network.Device.1.Radio.1.UnassociatedSTA.2.MACAddress="aa:bb:cc:dd:12:04"

  $ R ba-cli 'Device.WiFi.DataElements.Network.Device.1.Radio.1.RemoveUnassociatedStation(un_station_mac="AA:BB:CC:DD:12:04")'
  > Device.WiFi.DataElements.Network.Device.1.Radio.1.RemoveUnassociatedStation(un_station_mac="AA:BB:CC:DD:12:04")
  Device.WiFi.DataElements.Network.Device.1.Radio.1.RemoveUnassociatedStation() returned
  [
      {
      }
  ]

  $ sleep 1

  $ R ba-cli 'Device.WiFi.DataElements.Network.Device.1.Radio.1.UnassociatedSTA.*.MACAddress?' | grep 'aa:bb:cc:dd:12:04'
