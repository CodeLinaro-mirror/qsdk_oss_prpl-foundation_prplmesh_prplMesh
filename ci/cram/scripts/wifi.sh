#
# Common WiFi helpers:
#

# set/get wifi datamodel
# In : path under 'Device.WiFi.'
# Out : print ba-cli output
wifi_dm() {
  local path="$1"
  local base_path="$2"
  if [ -z "$base_path" ]; then
    base_path="Device.WiFi."
  fi
  local tool="$3"
  if [ -z "$tool" ]; then
    tool="usp-cli"
  fi

  local obj_name
  local res
  # read object name
  # get str before a single = (ignoring ==) and extracts the string following the final dot,
  # or defaults to the last dot segment if no = is present.
  obj_name=$(printf '%s\n' "$path" | sed -E 's/([^=])=([^=]).*/\1/; s/.*\.//')

  # remove any trailing '?' from next grep
  obj_name=${obj_name%%\?*}
  R logger -t cram "set_wifi_dm: command ${base_path}${path} object ${obj_name}"
  res=$(R "${tool} '${base_path}${path}'" | grep -v '>')
  if ! echo "$res" | grep -q "${obj_name}="; then
    if echo "$res" | grep -q "No data found"; then
      R logger -t cram "set_wifi_dm: ${base_path}${path} failed : No data found"
      echo "No data found"
      return 0
    fi
    R logger -t cram "set_wifi_dm: ${base_path}${path} failed."
    echo "set_wifi_dm: ${base_path}${path} failed. Result:"
    echo "$res"
    return 1
  else
    echo "$res" | grep "${obj_name}=" | sed '/^$/d' | grep -v '>' | sort
  fi

  return 0
}

# set/get wifi datamodel based on band
# In : band (2: 2.4GHz, 5: 5GHz, 6: 6GHz), path under 'WiFi.Radio.N.'
# Out : print ba-cli output
wifi_dm_radio_band() {
  local band="$1"
  local obj="$2"
  local base_path="$3"
  local tool="$4"

  if [ -z "$obj" ]; then
    R logger -t cram "wifi_dm_radio_band: empty object"
    echo "wifi_dm_radio_band: empty object"
    return 1
  fi

  if [ "$band" = "2" ]; then
    rad_filter='[OperatingFrequencyBand=="2.4GHz"]'
  elif [ "$band" = "5" ]; then
   rad_filter='[OperatingFrequencyBand=="5GHz"]'
  elif [ "$band" = "6" ]; then
   rad_filter='[OperatingFrequencyBand=="6GHz"]'
  else
    R logger -t cram "wifi_dm_radio_band: unknown band: $band"
    echo "wifi_dm_radio_band: unknown band: $band"
    return 1
  fi

  wifi_dm "Radio.${rad_filter}.${obj}" "$base_path" "$tool"
}

# getEHTOperations helper with normalized output
# In : band (2.4, 5, 6)
# Out : sorted key=value lines from getEHTOperations()
get_eht_ops() {
  local band="$1"
  local freq_band

  if [ "$band" = "2.4" ] || [ "$band" = "2" ] || [ "$band" = "2.4GHz" ]; then
    freq_band="2.4GHz"
  elif [ "$band" = "5" ] || [ "$band" = "5GHz" ]; then
    freq_band="5GHz"
  elif [ "$band" = "6" ] || [ "$band" = "6GHz" ]; then
    freq_band="6GHz"
  else
    R logger -t cram "get_eht_ops: unknown band: $band"
    echo "get_eht_ops: unknown band: $band"
    return 1
  fi

  R "usp-cli -l \"Device.WiFi.Radio.[OperatingFrequencyBand=='${freq_band}'].getEHTOperations()\"" |
    awk '/^\[/ {f=1; next} /^\]/ {f=0} f' |
    tr -d ' {}[],' |
    sed '/^$/d' |
    sort
}

# Enable AccessPoints
# In : AccessPoint object index
# Out : "enabled" if success, empty otherwise
enable_ap() {
  R "usp-cli -j -l Device.WiFi.AccessPoint.${1}.Enable=1 | grep -q Enable && echo 'Device.WiFi.AccessPoint.${1} enabled'"
}

# Disable AccessPoints
# In : AccessPoint object index, statut (0/1) (Optional)
# Out : AccessPoint.X.Enable=0,1 if success
enable_ap_sync() {
  local res
  local ap=$1
  local enable=$2
  local timeout=${3:-15}

  if [ $enable  -eq 0 ]; then
    tgt_state='Status="Disabled"'
  else
    tgt_state='Status="Enabled"'
  fi

  if ! res=$(wifi_dm "AccessPoint.${ap}.Enable=$enable"); then
    echo "$res"
    echo "Could not enable AP $ap"
    return 1
  fi

  while [ $timeout -gt 0 ]; do
    wifi_dm "AccessPoint.${ap}.Status?0" | grep -q $tgt_state && break
    timeout=$((timeout-1))
    sleep 1
  done

  [ "$timeout" -gt 0 ] && echo "AccessPoint.${ap}.Enable=${enable}" || echo "AccessPoint.$ap enable=${enable} timed out"

  # guard delay
  sleep 1
}

# Disable AccessPoints
# In : AccessPoint object index
# Out : "disabled" if success, empty otherwise
disable_ap() {
  R "usp-cli -j -l Device.WiFi.AccessPoint.${1}.Enable=0 | grep -q Enable && echo 'Device.WiFi.AccessPoint.${1} disabled'"
}

# Wait until SSID status is Up/Down
# In : AccessPoint object index
# In : Expected status Up/Down
# Out: "SSID Reference is {Up/Down}"
check_ap_ref_ssid() {
  R "
    i=10
    while [ \$i -gt 1 ]; do
      usp-cli -j -l Device.WiFi.AccessPoint.${1}.SSIDReference+.Status? |
        grep Device.WiFi.SSID. |
        grep -q \"${2}\" &&
        echo 'Device.WiFi.AccessPoint.${1} SSID Reference is ${2}' && break
      i=\$(( i - 1 ))
      sleep 2
    done
  "
}

# Print SSIDReference status
# In : AccessPoint object index
# Out : Enable / Disable / Dormant ...
get_ssid_ref() {
  msg=$(R "usp-cli -j -l Device.WiFi.AccessPoint.${1}.SSIDReference+.Status?")
  echo "$msg" | sed '/^$/d'
}

# Print APs status
get_ap_status() {
  wifi_dm "AccessPoint.*.Status?0"
}

# Print APs status
get_ap_ssid() {
  ap_num=$(R "usp-cli -l \"Device.WiFi.AccessPointNumberOfEntries?\"" | sed '/^$/d')
  local i=1
  while [ "$i" -le "$ap_num" ]; do
    wifi_dm "AccessPoint.$i.SSIDReference+.SSID?" | sed "s/Device.WiFi.SSID.[0-9]*/AccessPoint.$i/g"
    i=$((i + 1))
  done
}

# list wpacltrl socket file of a specific AP
# In : AccessPoint object index
# Out : local ls output
ls_ap_hapd_socket () {
  local ap=$1
  local mld_unit
  local main_itf
  local link_id
  local wpa_file

  # read main link interface from pwhm dm
  mld_unit=$(R "usp-cli -l \"Device.WiFi.AccessPoint.${ap}.SSIDReference+.MLDUnit?\"" | sed '/^$/d')
  main_itf=$(R "usp-cli -l \"Device.WiFi.SSID.[MLDRole=='Primary' && MLDUnit==${mld_unit}].Name?\"" | sed '/^$/d')

  # read link id from SSID object
  link_id=$(R "usp-cli -l \"Device.WiFi.AccessPoint.${ap}.SSIDReference+.MLDLinkID?\"" | sed '/^$/d')

  # ls socket file
  wpa_file="/var/run/hostapd/${main_itf}_link${link_id}"
#  /var/run/hostapd/wlan[0-9.]+_link[0-9] (re)
  R "[ -e ${wpa_file} ] && ls ${wpa_file} || echo \"not found\""
}


# list all wpacltrl socket file of a specific AP
# Out : local ls output
ls_hapd_sockets () {
  # ls socket file
  wpa_file="/var/run/hostapd/wlan*_link*"
#  /var/run/hostapd/wlan[0-9.]+_link[0-9] (re)
  R "ls /var/run/hostapd/ | grep wlan" | sort
}

# Print SSIDs status
get_ssid_status() {
  R "usp-cli -j -l Device.WiFi.SSID.?0 | jsonfilter -e @[0]'[@.Alias != \"ep2g0\" && @.Alias != \"ep5g0\" && @.Alias != \"ep6g0\"].Status'" | LC_ALL=C sort
}

# Print SSIDs values
get_ssid_ssid() {
  R "usp-cli -j -l Device.WiFi.SSID.?0 | jsonfilter -e @[0]'[@.Alias != \"ep2g0\" && @.Alias != \"ep5g0\" && @.Alias != \"ep6g0\"].SSID'" | LC_ALL=C sort
}

# Set MLDUnit
# In : AccessPoint object index, MLDUnit
# Out : MLDUnit value set
set_mlduint() {
  R "usp-cli -l Device.WiFi.AccessPoint.$1.SSIDReference+.MLDUnit=$2"  | sed '/^$/d'
}

# Set Radio [Arg1] OperatingStandardsFormat = [Arg2]; Standard : list of all standards; Legacy : only highest enabled 802.11 standard
set_radio_operating_standard_format(){
  R "ba-cli -l -j \"WiFi.Radio.[OperatingFrequencyBand=='$1'].OperatingStandardsFormat='$2'\" | jsonfilter -e @[0]'[*].OperatingStandardsFormat'"
}

# Set Radio [Arg1] OperatingStandards = [Arg2];
set_radio_operating_standards(){
  R "usp-cli -l -j \"Device.WiFi.Radio.[OperatingFrequencyBand=='$1'].OperatingStandards='$2'\" | jsonfilter -e @[0]'[*].OperatingStandards'"
}

# read hostapd option from configuration file
# Input : interface name, option
# Output : echo option value if it exists else error message
get_hapd_config() {
  local target_iface="$1"
  local target_param="$2"
  local result
  local target_conf_path="/tmp/${target_iface%.*}_hapd.conf"

  # As when in MLO all relevant interface options are set to main link itf name, use the BSSID instead while parsing parameters
  # this ensure the detection of the right section
  target_bssid=$(R "usp-cli -l \"Device.WiFi.SSID.[Name=='$itf'].BSSID?\"" | sed '/^$/d' | awk '{print toupper($0)}')

  R logger -t cram "get_hapd_config: get '$target_param' of '$target_iface' with bssid '$target_bssid' from '$target_conf_path'"

  result=$(R "cat $target_conf_path" 2>/dev/null | awk -v t_if="$target_bssid" -v t_pa="$target_param" '
      BEGIN { f_sec=0; f_val=0 }

      # Detect start of a section (Primary interface or BSS)
      /^bssid=/ || /^bss=/ {
          split($0, a, "=");
          current_if = a[2];
          if (current_if == t_if) { f_sec=1 }
          next
      }

      # If inside the correct section, look for the parameter
      f_sec == 1 && current_if == t_if {
          # Check for exact parameter match (start of line followed by =)
          if ($0 ~ "^" t_pa "=") {
              split($0, b, "=");
              print b[2];
              f_val=1;
              exit;
          }
      }

      END {
          if (f_sec == 0) { print "ERR_SECTION_NOT_FOUND"; exit 1 }
          if (f_val == 0) { print "ERR_OPTION_NOT_FOUND"; exit 1 }
      }
  ')

  # Handle the output and exit codes
  case "$result" in
      "ERR_SECTION_NOT_FOUND")
          R logger -t cram "get_hapd_config: Interface section not found"
          echo "Interface section not found"
          return 0
          ;;
      "ERR_OPTION_NOT_FOUND")
          R logger -t cram "get_hapd_config: Option '$target_param' not found"
          echo "Option '$target_param' not found"
          return 0
          ;;
      *)
          R logger -t cram "get_hapd_config: $target_param='$result'"
          echo "$result"
          return 0
          ;;
  esac
}
#
# APMLD helpers
#

# Print MLDUnit of private MLD based on default SSID (prplOS)
# In : N/A
# Out : MLDUNit
get_private_mldunit() {
   R 'usp-cli -j -l "Device.WiFi.SSID.[SSID==\"prplOS\"].MLDUnit?" | jsonfilter -e @[0]'[*].MLDUnit''  | head -n 1
}

# Print MLDUnit of private MLD based on default SSID (prplOS-guest)
# In : N/A
# Out : MLDUnit
get_guest_mldunit() {
   R 'usp-cli -j -l "Device.WiFi.SSID.[SSID==\"prplOS-guest\"].MLDUnit?" | jsonfilter -e @[0]'[*].MLDUnit''  | head -n 1
}

# validate mac address
is_valid_mac() {
  printf '%s\n' "$1" | grep -Eq '^([0-9A-Fa-f]{2}[:\-]){5}[0-9A-Fa-f]{2}$'
}

# Print APMLD MACAddress
# In : MLDID (MLDUnit)
# Out : APMLD MACAddress. If an APMLD matches the MLDID with empty MACAddress return an error message
get_apmld_mac_from_dm() {
  mac=$(R "usp-cli -l -j 'Device.WiFi.APMLD.[MLDID == ${1}].MLDMACAddress?' | jsonfilter -e @[0]'[*].MLDMACAddress' | strings")
  if ! is_valid_mac "$mac"; then
    echo "not_found"
    R logger -t cram "get_apmld_mac_from_dm: MLDMACAddress $mac of MLD ${1} not found"
    #R "iw dev > /tmp/$(R date +'%Y_%m_%d_%H_%M_%S')_aplmd_iw_out.txt"
  else
    echo "$mac"
  fi
}

# Print intefrace name from a MACAddress
# In : wlan MAC address
# Out : interface name
get_interface_name() {
  R "usp-cli -l -j 'Device.WiFi.SSID.[MACAddress==\"${1}\"].Name?' | jsonfilter -e @[0]'[*].Name' || echo 'Could not find SSID'"
}

# Print link number of an interface
# In : wlan interface
# Out : link number
get_link_info() {
  local itf=$1
  R logger -t cram "get_link_info: interface ${1}"
  R "iw dev ${itf} info" | grep -e addr -e channe -e link | sed 's/^[ \t]*//' | sed 's/:*$//' | sort | uniq
}

# print main link interface name from MAC address
# In : interface MAC address
# Out : main link interface name
get_main_link_itf () {
  local found=0
  local ifaces
  local mac=$1

  ifaces=$(R usp-cli -l "Device.WiFi.SSID.*.Name?0 | strings")

  for iface in $ifaces; do
    info=$(R iw dev "$iface" info 2>/dev/null)
    if echo "$info" | grep -i ${mac} -B2 | grep -q "link"; then
      R logger -t cram "MAC $1 found in main link interface $iface"
      echo "$iface"
      found=1
      break
    fi
  done

  if [ "$found" -eq 0 ]; then
    R logger -t cram "MAC $1 not associated to any main link"
    echo "MAC $1 not associated to any main link"
  fi
}

# print link number from iw output
# In : APMLD index (ie MLDID)
# Out : link number from iw output
iw_affliated_link_info_from_mldid() {
  # Detect main link interface
  mac_address=$(get_apmld_mac_from_dm "$1")
  if [ -z "$mac_address" ]; then
    R logger -t cram "iw_affliated_link_info_from_mldid: empty mac_address !"
  else
    R logger -t cram "iw_affliated_link_info_from_mldid: mac_address = $mac_address"
    itf_name=$(get_main_link_itf "$mac_address")
    get_link_info "$itf_name"
  fi
}

# print MAC addresses list of link interfaces from iw  output
# In : main link interface name
# Out : LinkID and MAC addresses list of affiliated interfaces (format : link N addr xx:xx:xx:xx:xx)
iw_get_main_link_mac_list() {
  local iface=$1
  R logger -t cram "get_main_link_mac_list $iface"
  R "iw dev $iface info"  | grep link -A3 | grep -e 'addr ' -e link | sed -nE 'N;s/.*link ([0-9]+):\n[[:space:]]*addr ([0-9a-fA-F:]+)/link \1 addr \2/p'  | LC_ALL=C sort
}

# print MAC addresses list of link interfaces from iw  output
# In : MLDID
# Out : MAC addresses list of link interfaces
iw_affilated_mac_list_from_mldid() {
  # Detect main link interface
  mac_address=$(get_apmld_mac_from_dm "$1") && R logger -t cram "mac_address = $mac_address"
  if [ -z "$mac_address" ]; then
    R logger -t cram "iw_affliated_mac_from_mldid: empty mac_address !"
  else
    itf_name=$(get_main_link_itf "$mac_address")
    iw_get_main_link_mac_list "$itf_name"
  fi
}

# print MAC addresses list of link interfaces from WiFi DM
# In : MLDID
# Out : LinkID and MAC addresses list of affiliated interfaces (format : link N addr xx:xx:xx:xx:xx)
dm_affilated_mac_list_from_mldid() {
  local output=""
  local idx=1
  local mld_id="$1"
  local aff_ap_nb
  local bssid
  local link_id

  aff_ap_nb=$(R "usp-cli -l 'Device.WiFi.APMLD.[ MLDID == ${mld_id} ].AffiliatedAPNumberOfEntries?'" | sed '/^$/d')

  while [ "$idx" -le "$aff_ap_nb" ]; do
    bssid=$(R "usp-cli -j -l 'Device.WiFi.APMLD.[ MLDID == ${mld_id} ].AffiliatedAP.${idx}.BSSID?' | jsonfilter -e @[0]'[*].BSSID'")
    link_id=$(R "usp-cli -j -l 'Device.WiFi.APMLD.[ MLDID == ${mld_id} ].AffiliatedAP.${idx}.LinkID?' | jsonfilter -e @[0]'[*].LinkID'")
    output="link ${link_id} addr ${bssid}\n${output}"
    idx=$((idx + 1))
  done

  printf "%b" "$output"
}
