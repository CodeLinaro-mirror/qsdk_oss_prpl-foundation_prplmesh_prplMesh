#!/bin/sh
self=$0
device=$1
radio=$2
target=$3
beacon=$4

err_usage=1
err_notfound=2
err_target=3
err_beacon=4
non_oper=0
bcn_pref=7
max_pref=14
path=Device.WiFi.DataElements.Network.Device.$device.Radio.$radio.OpClassPreference.

usage() {
	echo "usage: $self <device indice> <radio indice> <channel> [beacon channel]"
	echo
	echo "device indice - matching Device.WiFi.DataElements.Device.X"
	echo "radio indice - matching Device.WiFi.DataElements.Device._.Radio.X"
	echo "channel - represented as opclass-channel, eg. 81-1 is OpClass: 81, Channel: 1"
	echo "beacon channel - optional, for wider than 40MHz channels"
	echo
	echo "examples:"
	echo "  - channel 1, 20Mhz on 2.4GHz"
	echo "    $self 1 1 81-1"
	echo "  - channel 1, 40Mhz on 2.4GHz"
	echo "    $self 1 1 83-1"
	echo "  - channel 36 (center 42), 80Mhz on 5GHz"
	echo "    $self 1 1 128-42 115-36"
	echo "  - channel 5 (center 15), 160MHz on 6GHz"
	echo "    $self 1 1 134-15 131-5"
	exit $err_usage
}

test -z "$device" && usage
test -z "$radio" && usage
test -z "$target" && usage

. /usr/share/libubox/jshn.sh

# wrapped with {} because jshn doesn't support ingesting top-level arrays
instances="{ \"output\": $(ba-cli -laj "${path}?") }"

json_load "$instances"
json_select output
if test $? -ne 0
then
	# Failing to parse means the device+radio likely does not exist
	echo "$path: not found"
	exit $err_notfound
fi

json_select 1
json_get_keys keys

channels=$(
	for key in $keys
	do
		json_select "$key"
		json_get_vars OpClass ChannelList Preference
		for channel in $(echo "$ChannelList" | tr ',' ' ')
		do
			test "$Preference" -ne 0 && echo "$OpClass-$channel"
		done
		json_select ..
	done
)

if ! echo "$channels" | grep -qxF "$target"
then
	echo "channel $target is unavailable, refusing to continue"
	echo "channels available:"
	echo "$channels"
	exit $err_target
fi

if test -n "$beacon" && ! echo "$channels" | grep -qxF "$beacon"
then
	echo "beacon channel $beacon is unavailable, refusing to continue"
	echo "channels available:"
	echo "$channels"
	exit $err_beacon
fi

prefs=$(
	echo "$channels" \
		| grep -xFv "$target" \
		| grep -xFv "$beacon" \
		| sed "s/$/:$non_oper/"
	test -n "$target" && echo "$target:$max_pref"
	test -n "$beacon" && echo "$beacon:$bcn_pref"
)

arg=$(
	indice=1
	for pref in $prefs
	do
		opclass=$(echo "$pref" | cut -d- -f1)
		channel=$(echo "$pref" | cut -d- -f2 | cut -d: -f1)
		preference=$(echo "$pref" | cut -d- -f2 | cut -d: -f2)
		echo "$indice={OpClass=$opclass,Channel={1={Channel=$channel,Preference=$preference}}}"
		indice=$(expr $indice + 1)
	done | tr '\n' ','
)

exec ba-cli "Device.WiFi.DataElements.Network.Device.$device.Radio.$radio.ChannelSelectionRequest(Class={$arg})"
