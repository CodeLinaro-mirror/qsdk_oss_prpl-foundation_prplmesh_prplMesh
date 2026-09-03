Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"

Stop prplmesh:
  $ R ba-cli 'WiFi.AccessPoint.*.Enable=0'

  $ R logger -t cram "Stop prplmesh"
  $ R "ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0" | tr -d '\n'
  0 (no-eol)

  $ sleep 2

Restart prplmesh in controller + local agent mode:
  $ R logger -t cram "Restart prplmesh"

  $ R "ba-cli X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode=Multi-AP-Controller-and-Agent" > /dev/null
  $ R "ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=1" | tr -d '\n'
  1 (no-eol)

  $ R "amx_wait_for X_PRPLWARE-COM_WiFiController.Network.Device.1"

  $ R "ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode?" | tr -d '\n'
  Multi-AP-Controller-and-Agent (no-eol)

Test SAEPassphrase

  $ R ba-cli 'WiFi.AccessPoint.1.Security.Mode="WPA3-Personal-Transition"' >/dev/null
  $ R ba-cli 'WiFi.AccessPoint.1.Security.KeyPassphrase="key_passphrase"' >/dev/null
  $ R ba-cli 'WiFi.AccessPoint.1.Security.SAEPassphrase="sae_passphrase"' >/dev/null
  $ R ba-cli 'WiFi.AccessPoint.1.Enable=1' >/dev/null

  $ sleep 10

  $ R logread -e "Autoconfiguration for ssid" | tail -1 | sed -n 's/.*network_key: \([^ ]*\).*/\1/p'
  sae_passphrase

Test KeyPassphrase
  $ R ba-cli 'WiFi.AccessPoint.1.Security.Mode="WPA2-Personal"' >/dev/null
  $ R ba-cli 'WiFi.AccessPoint.1.Security.KeyPassphrase="key_passphrase"' >/dev/null
  $ R ba-cli 'WiFi.AccessPoint.1.Security.SAEPassphrase="sae_passphrase"' >/dev/null

  $ sleep 10

  $ R logread -e "Autoconfiguration for ssid" | tail -1 | sed -n 's/.*network_key: \([^ ]*\).*/\1/p'
  key_passphrase