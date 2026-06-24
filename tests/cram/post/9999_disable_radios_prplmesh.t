Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ alias A="${CRAM_REMOTE_COMMAND_DEVICE_2:CRAM_REMOTE_COMMAND}"

  $ R ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":0}}]


  $ R ba-cli -j -l WiFi.Radio.*.Enable=0 | sed '/^$/d'
  [{"WiFi.Radio.1.":{"Enable":0},"WiFi.Radio.2.":{"Enable":0},"WiFi.Radio.3.":{"Enable":0}}]

  $ A ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":0}}]


  $ A ba-cli -j -l WiFi.Radio.*.Enable=0 | sed '/^$/d'
  [{"WiFi.Radio.1.":{"Enable":0},"WiFi.Radio.2.":{"Enable":0},"WiFi.Radio.3.":{"Enable":0}}]
