Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"

  $ R ba-cli -j -l WiFi.Radio.*.Enable? | sed '/^$/d'
  [{"WiFi.Radio.1.":{"Enable":1},"WiFi.Radio.2.":{"Enable":1},"WiFi.Radio.3.":{"Enable":1}}]

  $ R ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.? | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":1,"ManagementMode":"Multi-AP-Agent","CertificationMode":0,"Status":"Active","FaultCode":"NoFault"}}]


