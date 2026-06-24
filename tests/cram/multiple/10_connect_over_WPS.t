Create remote aliasses:

  $ alias DEV1="${CRAM_REMOTE_COMMAND_DEVICE_1:-}"
  $ alias DEV2="${CRAM_REMOTE_COMMAND_DEVICE_2:-}"
  $ alias DEV3="${CRAM_REMOTE_COMMAND_DEVICE_3:-}"

  $ FREEDOM1 ba-cli -j -l WiFi.Radio.*.Enable? | sed '/^$/d'
  [{"WiFi.Radio.1.":{"Enable":1},"WiFi.Radio.2.":{"Enable":1},"WiFi.Radio.3.":{"Enable":1}}]

  $ R ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.? | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":1,"ManagementMode":"Multi-AP-Agent","CertificationMode":0,"Status":"Active","FaultCode":"NoFault"}}]

