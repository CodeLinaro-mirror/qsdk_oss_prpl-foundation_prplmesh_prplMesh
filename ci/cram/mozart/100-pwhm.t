Create R alias:

  $ alias R="${CRAM_REMOTE_COMMAND:-}"
  $ . "${TESTDIR}/../scripts/wifi.sh"

  $ R logger -t cram "Starting PWHM test ..."

Wait for Device.WiFi. datamodel availability:

  $ R "amx_wait_for "Device.WiFi." "

Stop prplMesh:

  $ R "/etc/init.d/prplmesh stop > /dev/null 2>&1"

Set AutoChannelEnable=0 on all WiFi.Radio. interfaces:

  $ wifi_dm "Radio.*.AutoChannelEnable=0"
  WiFi.Radio.1.AutoChannelEnable=0
  WiFi.Radio.2.AutoChannelEnable=0
  WiFi.Radio.3.AutoChannelEnable=0

Set channel to a non DFS one:

  $ wifi_dm "Radio.2.Channel=36"
  WiFi.Radio.2.Channel=36 (re)

Check default SSID status:

  $ get_ap_status
  WiFi.AccessPoint.1.Status="Disabled"
  WiFi.AccessPoint.2.Status="Disabled"
  WiFi.AccessPoint.3.Status="Disabled"
  WiFi.AccessPoint.4.Status="Disabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Check default SSID configuration of access points:

  $ get_ap_ssid
  AccessPoint.1.SSID="prplOS"
  AccessPoint.2.SSID="prplOS-guest"
  AccessPoint.3.SSID="prplOS"
  AccessPoint.4.SSID="prplOS-guest"
  AccessPoint.5.SSID="prplOS"
  AccessPoint.6.SSID="prplOS-guest"
  AccessPoint.7.SSID="backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}" (re)
  AccessPoint.8.SSID="backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}" (re)
  AccessPoint.9.SSID="backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}" (re)

Check that no hostapd instance is running:

  $ R "pgrep -f 'hostapd'"
  [1]

Test activation of access point 1:

  $ R logger -t cram "Test AccessPoint 1 activation "$(get_ssid_ref 1)""

  $ enable_ap_sync 1 1
  AccessPoint.\d+.Enable=1 (re)

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Disabled"
  WiFi.AccessPoint.3.Status="Disabled"
  WiFi.AccessPoint.4.Status="Disabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Save hostap pid:

  $ hostap_pid=$(R pgrep -f 'hostapd')
  $ R logger -t cram "hostap PID : $hostap_pid"

Test activation of access point 2:

  $ R logger -t cram "Test AccessPoint 2 activation "$(get_ssid_ref 2)""

  $ enable_ap_sync 2 1
  AccessPoint.2.Enable=1

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Disabled"
  WiFi.AccessPoint.4.Status="Disabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test activation of access point 3:

  $ R logger -t cram "Test AccessPoint 3 activation "$(get_ssid_ref 3)""

  $ enable_ap_sync 3 1
  AccessPoint.3.Enable=1

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Disabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test activation of access point 4:

  $ R logger -t cram "Test AccessPoint 4 activation "$(get_ssid_ref 4)""

  $ enable_ap_sync 4 1
  AccessPoint.4.Enable=1

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test activation of access point 5:

  $ R logger -t cram "Test AccessPoint 5 activation "$(get_ssid_ref 5)""

  $ enable_ap_sync 5 1
  AccessPoint.5.Enable=1

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test activation of access point 6:

  $ R logger -t cram "Test AccessPoint 6 activation "$(get_ssid_ref 6)""

  $ enable_ap_sync 6 1
  AccessPoint.6.Enable=1

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Enabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test activation of access point 7:

  $ R logger -t cram "Test AccessPoint 7 activation "$(get_ssid_ref 7)""

  $ enable_ap_sync 7 1
  AccessPoint.7.Enable=1

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Enabled"
  WiFi.AccessPoint.7.Status="Enabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test activation of access point 8:

  $ R logger -t cram "Test AccessPoint 8 activation "$(get_ssid_ref 8)""

  $ enable_ap_sync 8 1
  AccessPoint.8.Enable=1

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Enabled"
  WiFi.AccessPoint.7.Status="Enabled"
  WiFi.AccessPoint.8.Status="Enabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test activation of access point 9:

  $ R logger -t cram "Test AccessPoint 9 activation "$(get_ssid_ref 9)""

  $ enable_ap_sync 9 1
  AccessPoint.9.Enable=1

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Enabled"
  WiFi.AccessPoint.7.Status="Enabled"
  WiFi.AccessPoint.8.Status="Enabled"
  WiFi.AccessPoint.9.Status="Enabled"

Check that hostapd is operating as expected:

  $ R logger -t cram "Check that hostapd is operating"

  $ R "ps axw" | sed -nE 's/.*(hostapd .*)/\1/p' | head -1 | tr -s ' ' '\n' | LC_ALL=C sort
  -g
  -s
  /tmp/wlan2_hapd.conf
  /var/run/hostapd/global\.0x.* (re)
  hostapd

  $ R "ubus list | grep hostapd. | sort"
  hostapd.wlan0.1
  hostapd.wlan0.2
  hostapd.wlan0.3
  hostapd.wlan1.1
  hostapd.wlan1.2
  hostapd.wlan1.3
  hostapd.wlan2.1
  hostapd.wlan2.2
  hostapd.wlan2.3

Check iw interfaces and beaconing:

  $ R "iw dev | grep -e Interface -e ssid | tr -d '\t' | sort"
  Interface wlan0
  Interface wlan0.1
  Interface wlan0.2
  Interface wlan0.3
  Interface wlan1
  Interface wlan1.1
  Interface wlan1.2
  Interface wlan1.3
  Interface wlan2
  Interface wlan2.1
  Interface wlan2.2
  Interface wlan2.3
  ssid backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  ssid backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  ssid backhaul_(1C:F4:3F|20:37:F0):[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2} (re)
  ssid prplOS
  ssid prplOS
  ssid prplOS
  ssid prplOS-guest
  ssid prplOS-guest
  ssid prplOS-guest

Test deactivation of access point 9:

  $ enable_ap_sync 9 0
  AccessPoint.9.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Enabled"
  WiFi.AccessPoint.7.Status="Enabled"
  WiFi.AccessPoint.8.Status="Enabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test deactivation of access point 8:

  $ R logger -t cram "Test AccessPoint 8 deactivation "$(get_ssid_ref 8)""

  $ enable_ap_sync 8 0
  AccessPoint.8.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Enabled"
  WiFi.AccessPoint.7.Status="Enabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test deactivation of access point 7:

  $ R logger -t cram "Test AccessPoint 7 deactivation "$(get_ssid_ref 7)""

  $ enable_ap_sync 7 0
  AccessPoint.7.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Enabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test deactivation of access point 6:

  $ R logger -t cram "Test AccessPoint 6 deactivation "$(get_ssid_ref 6)""

  $ enable_ap_sync 6 0
  AccessPoint.6.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Enabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test deactivation of access point 5:

  $ R logger -t cram "Test AccessPoint 5 deactivation "$(get_ssid_ref 5)""

  $ enable_ap_sync 5 0
  AccessPoint.5.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Enabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test deactivation of access point 4:

  $ R logger -t cram "Test AccessPoint 4 deactivation "$(get_ssid_ref 4)""

  $ enable_ap_sync 4 0
  AccessPoint.4.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Enabled"
  WiFi.AccessPoint.4.Status="Disabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test deactivation of access point 3:

  $ R logger -t cram "Test AccessPoint 3 deactivation "$(get_ssid_ref 3)""

  $ enable_ap_sync 3 0
  AccessPoint.3.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Enabled"
  WiFi.AccessPoint.3.Status="Disabled"
  WiFi.AccessPoint.4.Status="Disabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Test deactivation of access point 2:

  $ R logger -t cram "Test AccessPoint 2 deactivation "$(get_ssid_ref 2)""

  $ enable_ap_sync 2 0
  AccessPoint.2.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Enabled"
  WiFi.AccessPoint.2.Status="Disabled"
  WiFi.AccessPoint.3.Status="Disabled"
  WiFi.AccessPoint.4.Status="Disabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Before deactivating last AP (ie stopping hostpad), check if hostap pid has changed or not:

  $ if [ "$(R pgrep -f 'hostapd')" = "$hostap_pid" ]; then echo "true"; else echo "hostap restarted during the test !"; fi
  true

Test deactivation of access point 1:

  $ R logger -t cram "Test AccessPoint 1 deactivation "$(get_ssid_ref 1)""

  $ enable_ap_sync 1 0
  AccessPoint.1.Enable=0

  $ wifi_dm "AccessPoint.*.Status?0"
  WiFi.AccessPoint.1.Status="Disabled"
  WiFi.AccessPoint.2.Status="Disabled"
  WiFi.AccessPoint.3.Status="Disabled"
  WiFi.AccessPoint.4.Status="Disabled"
  WiFi.AccessPoint.5.Status="Disabled"
  WiFi.AccessPoint.6.Status="Disabled"
  WiFi.AccessPoint.7.Status="Disabled"
  WiFi.AccessPoint.8.Status="Disabled"
  WiFi.AccessPoint.9.Status="Disabled"

Check if hostapd process is stopped:

  $ R "pgrep -f 'hostapd'"
  [1]

Resume prplMesh:

  $ R "/etc/init.d/prplmesh start 2>&1 > /dev/null"

  $ R logger -t cram "Stopping PWHM test .."

Wait 5s before leaving the test:

  $ sleep 5

  $ R logger -t cram "Test finished!"

