  $ alias R="${CRAM_REMOTE_COMMAND:-}"

  $ original_mode="$(R ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode? | tr -d '\n')"

  $ R ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode="Multi-AP-Controller-and-Agent" | tr -d '\n'
  Multi-AP-Controller-and-Agent (no-eol)

  $ sleep 5

Check that disabling/enabling IEEE1905 works:

  $ R ba-cli -l IEEE1905.Network.Enable=0 | tr -d '\n'
  0 (no-eol)

  $ R ba-cli -l IEEE1905.Network.ALNumberOfEntries? | tr -d '\n'
  0 (no-eol)

  $ R ba-cli -l IEEE1905.Network.Enable=1 | tr -d '\n'
  1 (no-eol)

  $ sleep 3

  $ test "$(R ba-cli -l IEEE1905.Network.ALNumberOfEntries? | tr -d '\n')" -ge 1 && echo ok
  ok

Check that DataElements MACs are a subset of IEEE1905 MACs:

  $ ieee1905_ids="$(R ba-cli -l IEEE1905.Network.AL.*.IEEE1905Id? | sed '/^$/d' | sort)"

  $ dataelements_ids="$(R ba-cli -l Device.WiFi.DataElements.Network.Device.*.ID? | sed '/^$/d' | sort)"

  $ missing_dataelements="$(for id in $dataelements_ids; do printf '%s\n' "$ieee1905_ids" | grep -Fx "$id" >/dev/null || echo "$id"; done)"

  $ test -z "$missing_dataelements" && echo ok || { printf 'IEEE1905 IDs:\n%s\nDataElements IDs:\n%s\nMissing DataElements IDs:\n%s\n' "$ieee1905_ids" "$dataelements_ids" "$missing_dataelements"; false; }
  ok

Check that all DataElements devices are referenced:

  $ referenced_ids="$(for ref in $(R ba-cli -l IEEE1905.Network.AL.*.AssocWiFiNetworkDeviceRef? | sed '/^$/d'); do R ba-cli -l "$ref.ID?"; done | sed '/^$/d' | sort)"

  $ test "$dataelements_ids" = "$referenced_ids" && echo ok || { printf 'DataElements IDs:\n%s\nReferenced DataElements IDs:\n%s\n' "$dataelements_ids" "$referenced_ids"; false; }
  ok

Check that the IEEE1905 model is populated in NMAP mode:

  $ R ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode="Not-Multi-AP" | tr -d '\n'
  Not-Multi-AP (no-eol)

  $ sleep 5

  $ test "$(R ba-cli -l IEEE1905.Network.ALNumberOfEntries? | tr -d '\n')" -ge 1 && echo ok
  ok

To avoid a potential 60+ second wait, remote ALs are not compared with Controller+Agent mode.
They are discovered only when their periodic Topology Discovery messages arrive, which are sent every 60 seconds.

Check that DataElements is not available in NMAP mode:

  $ R ba-cli -l Device.WiFi.DataElements.? | sed '/^$/d'
  ERROR: Device.WiFi.DataElements. not found.

  $ R ba-cli -l IEEE1905.Network.AL.*.AssocWiFiNetworkDeviceRef? | sed '/^$/d' | wc -l
  0

Check that disabling IEEE1905 also works in NMAP mode:

  $ R ba-cli -l IEEE1905.Network.Enable=0 | tr -d '\n'
  0 (no-eol)

  $ R ba-cli -l IEEE1905.Network.ALNumberOfEntries? | tr -d '\n'
  0 (no-eol)

  $ R ba-cli -l IEEE1905.Network.Enable=1 | tr -d '\n'
  1 (no-eol)

  $ test "$(R ba-cli -l X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode="$original_mode" | tr -d '\n')" = "$original_mode" && echo ok
  ok

  $ sleep 5
