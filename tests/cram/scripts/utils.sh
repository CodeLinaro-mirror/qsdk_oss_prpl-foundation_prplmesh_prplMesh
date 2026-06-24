#!/usr/bin/env bash
###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2019-2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

TESTBED_DEVICES_FILENAME="prplMesh/testbed_devices.toml"

target_in_device_list(){
    local target=$1

    # shellcheck disable=SC2016 # The shell expansion warning doesn't apply to the tomlq selector, supress it
    tomlq --arg v "$target" \
        'to_entries
         | any(.key == $v and
               ((.value|type) == "object"
                or ((.value|type) == "array" and (.value[0]|type) == "object")))' \
        "$TESTBED_DEVICES_FILENAME" | grep -q '^true$'
}

get_value_of_target(){
    local target=$1
    local key=$2

    VALUE=$(tomlq -r ".$target.$key" $TESTBED_DEVICES_FILENAME)

    if [ "${VALUE}" = "null" ]; then
        exit 1
    fi

    echo "$VALUE"
}

get_keys_of_target(){
    local target=$1

    tomlq -r ".$target | keys[]" "$TESTBED_DEVICES_FILENAME"
}

export_values_of_target(){
    local target=$1
    local num=$2

    local key
    while IFS= read -r key; do
        local value
        value=$(get_value_of_target "$target" "$key") || { echo "Failed to get $key of device $target, exiting" >&2; exit 1; }
        export "${key}_${num}=${value}"
    done < <(get_keys_of_target "$target")
}

parse_testbed_devices_file(){
    i=1
    while [[ -v TARGET_DEVICE_$i ]]; do
        target="TARGET_DEVICE_$i"

        if ! target_in_device_list ${!target}; then
            echo "Target ${!target} not defined in testbed_devices.toml"
            exit 1
        fi

        export_values_of_target ${!target} $i
        ip_target="IP_${i}"
        if [[ ! -v "$ip_target" ]]; then
            echo "IP of ${!target} not found in testbed_devices.toml"
            exit 1
        fi
        export "CRAM_REMOTE_COMMAND_${i}=${CRAM_REMOTE_COMMAND_BASE}${!ip_target}"
        ((i++))
    done

    export "CRAM_REMOTE_COMMAND=$CRAM_REMOTE_COMMAND_1"
}

copy_logs_of_targets(){
    local destination=$1

    if [ ! -d "$destination" ] || [ ! -w "$destination" ]; then
        echo "Log directory '$destination' does not exist or is not writable" >&2
        return
    fi

    local i=1
    while [[ -v TARGET_DEVICE_$i ]]; do
        local target="TARGET_DEVICE_$i"
        local ip_target="IP_${i}"
        local target_dir="${destination}/${!target}_${i}"

        mkdir -p "$target_dir"
        echo "Copying /var/log of ${!target} (${!ip_target}) to $target_dir"
        # shellcheck disable=SC2086 # CRAM_REMOTE_COPY contains the scp command and its options, it has to be split
        $CRAM_REMOTE_COPY -r "root@${!ip_target}:/var/log" "$target_dir/" \
            || echo "Failed to copy /var/log of ${!target}" >&2
        chmod -R a+rwX "$target_dir"
        ((i++))
    done
}
