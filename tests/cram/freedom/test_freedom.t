Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"

  $ R ba-cli -j -l Device.DeviceInfo.ModelName? | sed '/^$/d'
  [{"Device.DeviceInfo.":{"ModelName":"prpl Foundation Freedom"}}]
