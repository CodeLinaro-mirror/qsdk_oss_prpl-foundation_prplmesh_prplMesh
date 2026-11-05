# BWL Dummy

Dummy BWL implementation for Linux (not using actual WiFi radios).

## Simulating Events

The BWL Dummy implementation provides simple events simulation support using text file modification, specifically for simulated client connect - disconnect.

The way this is implemented is using `inotify` support in Linux to watch a file named EVENT in each radio agent:

```bash
/tmp/beerocks/wlan0/EVENT
/tmp/beerocks/wlan2/EVENT
```

The dummy BWL implementation reads the first line of the EVENT file each time it is modified (or created), and parse the event, then act on it.

This allows simulating WLAN events simply by writing the event as it would have been received from hostapd into this file - according to  [upstream hostapd](https://w1.fi/wpa_supplicant/devel/ctrl_iface_page.html).
For example, simulating client connected event to the 2.4G radio (wlan0):

```bash
echo "AP-STA-CONNECTED 11:22:33:44:55:66" > /tmp/beerocks/wlan0/EVENT
```

This results with the event picked by the BWL dummy implementation:

```bash
INFO 11:42:21:311 <140309100766976> ap_manager_thread.cpp[909] --> STA_Connected mac = 11:22:33:44:55:66
TRACE 11:42:21:311 <140309101423488> son_slave_thread.cpp[2509] --> received ACTION_APMANAGER_CLIENT_ASSOCIATED_NOTIFICATION
INFO 11:42:21:311 <140309101423488> son_slave_thread.cpp[2520] --> client associated sta_mac=11:22:33:44:55:66
```

Then in the controller:

```bash
INFO 11:42:21:312 <140688433724288> son_master_thread.cpp[2451] --> client associated, mac=11:22:33:44:55:66 hostap mac=00:11:22:33:00:10 setting to channel=1 
```

Which can be seen in `bml_conn_map`:

```bash
beerocks_cli -c bml_conn_map

bml_connect: return value is: BML_RET_OK, Success status
bml_nw_map_query: return value is: BML_RET_OK, Success status

GW_BRIDGE: name: GW_MASTER, mac: 00:11:22:33:00:00, ipv4: 172.18.0.234
    ETHERNET: mac: 00:11:22:33:00:01
    RADIO: wlan0 mac: 00:11:22:33:00:10, ch: 1, bw: 80L, freq: 2412MHz
        fVAP[0]: wlan0.0 bssid: 00:11:22:33:00:10, ssid: N/A
            CLIENT: mac: 11:22:33:44:55:66, ipv4: 0.0.0.0, name: N/A, ch: 1, bw: 80L, rx_rssi: -127
    RADIO: wlan2 mac: 00:11:22:33:00:20, ch: 149, bw: 80L, freq: 5745MHz
        fVAP[0]: wlan2.0 bssid: 00:11:22:33:00:20, ssid: N/A
bml_disconnect: return value is: BML_RET_OK, Success status
```

Using this new capability in DUMMY mode, development of flows involving stations such as steering can be tested more thoroughly. In addition, this provides the basis for Agent support in DUMMY mode.

## Per-BSSID WPS push button

The dummy AP implements the per-BSSID WPS push-button methods of `ap_wlan_hal`, and logs the push-button settings of the BSS configuration:

- `update_vap_credentials()` logs the UUID-E, the Device Password ID and the length of the Device Password of each BSS with push-button settings.
  It never logs the Device Password itself.
- `start_wps_pbc_bssid()` replies with the status `Success` for a BSS of the radio, so the WPS session is reported as started.
  It sends no `Ready` event.
  For a BSSID that the radio does not have, it returns `NotSent`.
- `cancel_wps_pbc_bssid()` sends a WPS pairing `Done` event with the reason `Canceled` for the BSS.

The end of a session, or a start event, is simulated with the EVENT file of the AP part of the radio, `/tmp/beerocks/<radio>/EVENT_AP`.
The event names the BSS by its interface name.
The Enrollee MAC address is given with the key `macAddress`, because the event parser keeps only one MAC address without a key.
A `WPS-PAIRING-DONE` event needs a `reason`:

```bash
echo "EVENT WPS-PAIRING-DONE wlan0.0 reason=Success macAddress=11:22:33:44:55:66" > /tmp/beerocks/wlan0/EVENT_AP
echo "EVENT WPS-PAIRING-DONE wlan0.0 reason=Timeout" > /tmp/beerocks/wlan0/EVENT_AP
echo "EVENT WPS-PAIRING-DONE wlan0.0 reason=Overlap" > /tmp/beerocks/wlan0/EVENT_AP
echo "EVENT WPS-PAIRING-DONE wlan0.0 reason=Failure" > /tmp/beerocks/wlan0/EVENT_AP
echo "EVENT WPS-PAIRING-READY wlan0.0" > /tmp/beerocks/wlan0/EVENT_AP
echo "EVENT WPS-PAIRING-ERROR wlan0.0 reason=Failure" > /tmp/beerocks/wlan0/EVENT_AP
```

Each event reaches the AP manager as a `WPS_Pairing_Event` with the BSSID of the named BSS.
The AP manager reports it only for a BSSID with a running push-button session.
