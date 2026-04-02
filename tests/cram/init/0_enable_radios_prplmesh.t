Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"

  $ R ba-cli -j -l WiFi.Radio.*.Enable=1 | sed '/^$/d'
  [{"WiFi.Radio.1.":{"Enable":1},"WiFi.Radio.2.":{"Enable":1},"WiFi.Radio.3.":{"Enable":1}}]

  $ sleep 10

  $ R ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.CertificationMode=0 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"CertificationMode":0}}]

  $ R ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode="Multi-AP-Agent" | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"ManagementMode":"Multi-AP-Agent"}}]

  $ R ba-cli -j -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1 | sed '/^$/d'
  [{"X_PRPLWARE-COM_ProcessManager.PrplMesh.":{"Enable":1}}]
