#!/usr/bin/env bash

TESTBED_DEVICES_FILENAME="prplMesh/testbed_devices.toml"

target_in_device_list(){
    local target=$1

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

    echo $VALUE
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

    #export -p
}
