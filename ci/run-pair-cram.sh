#!/usr/bin/env bash

set -euo pipefail

readonly tftp_server_ip=192.168.1.199
readonly agent_endpoint=ep5g0

# Load the firmware, addressing, and interface configuration for one pair role.
# Inputs: $1 is CONTROLLER or AGENT; $2 is a supported CRAM target name.
# Output: exports PAIR_<role>_* and TS_<role>_* variables for later stages.
load_target() {
    local role="$1" target="$2" config
    local device ip guest_ip boot_ip model platform wired image rootfs

    case "$target" in
        freedom)
            config='freedom|192.168.1.1|192.168.2.1|192.168.1.150|prpl Foundation Freedom|freedom|lan4'
            ;;
        freedom2)
            config='freedom|192.168.1.20|192.168.2.20|192.168.1.150|prpl Foundation Freedom|freedom|lan4'
            ;;
        urx_ospv2)
            config='urx_ospv2|192.168.1.160|192.168.2.160|192.168.1.160|mxl,osp-tb341-v2|ospv2|eth0_2'
            ;;
        urx_ospv22)
            config='urx_ospv2|192.168.1.167|192.168.2.167|192.168.1.167|OSPv2|ospv2|eth0_2'
            ;;
        *)
            echo "Unknown CRAM pair target: $target" >&2
            return 2
            ;;
    esac
    IFS='|' read -r device ip guest_ip boot_ip model platform wired <<<"$config"

    case "$platform" in
        freedom)
            image=${FREEDOM_FULLIMAGE:?FREEDOM_FULLIMAGE is not set}
            rootfs=${FREEDOM_ROOTFS:?FREEDOM_ROOTFS is not set}
            ;;
        ospv2)
            image=${URX_OSPV2_FULLIMAGE:?URX_OSPV2_FULLIMAGE is not set}
            rootfs=
            ;;
    esac

    export "PAIR_${role}_TARGET=$target"
    export "PAIR_${role}_DEVICE=$device"
    export "PAIR_${role}_IP=$ip"
    export "PAIR_${role}_MODEL=$model"
    export "PAIR_${role}_PLATFORM=$platform"
    export "PAIR_${role}_IMAGE=$image"
    export "PAIR_${role}_ROOTFS=$rootfs"
    export "PAIR_${role}_BOOT_IP=$boot_ip"
    export "TS_${role}_GUEST_IP=$guest_ip"
    export "TS_${role}_WIRED=$wired"
}

# Validate job inputs and assemble the controller-agent pair configuration.
# Input: CI, image, and PAIR_* target variables from the job environment.
# Output: exports both role configurations plus the pair SSID names.
load_pair() {
    : "${CI_PROJECT_DIR:?CI_PROJECT_DIR is not set}"
    : "${CI_REGISTRY_IMAGE:?CI_REGISTRY_IMAGE is not set}"
    : "${PARENT_PIPELINE_ID:?PARENT_PIPELINE_ID is not set}"
    : "${PAIR_CONTROLLER_TARGET:?PAIR_CONTROLLER_TARGET is not set}"
    : "${PAIR_AGENT_TARGET:?PAIR_AGENT_TARGET is not set}"

    load_target CONTROLLER "$PAIR_CONTROLLER_TARGET"
    load_target AGENT "$PAIR_AGENT_TARGET"
    export PAIR_AGENT_ENDPOINT=$agent_endpoint

    case "$PAIR_CONTROLLER_PLATFORM" in
        freedom)
            export PAIR_FH_SSID_NAME=freedom_1_FH_private
            export PAIR_GUEST_SSID_NAME=freedom_1_FH_guest
            export PAIR_BH_SSID_NAME=freedom_1_BH
            ;;
        ospv2)
            export PAIR_FH_SSID_NAME=ospv2_FH_private
            export PAIR_GUEST_SSID_NAME=ospv2_FH_guest
            export PAIR_BH_SSID_NAME=ospv2_BH
            ;;
    esac
}

# Deploy firmware, pair-specific configuration, and prplMesh to one target.
# Inputs: $1..$9 are device, target, host, image, rootfs, boot IP, LAN IP,
# guest IP, and wired interface respectively.
# Output: updates the target and returns the deployment command status.
deploy_pair_device() {
    local device="$1" target="$2" host="$3" image="$4" rootfs="$5"
    local boot_ip="$6" lan_ip="$7" guest_ip="$8" wired_interface="$9"
    local -a deploy=(
        tools/deploy_firmware.py
        --device "$device"
        --target-name "$target"
        --ssh-host "$host"
        --image "$image"
        --ipaddr "$boot_ip"
        --serverip "$tftp_server_ip"
        --whm
        --full
    )

    [[ -z "$rootfs" ]] || deploy+=(--rootfs "$rootfs")
    "${deploy[@]}" --configuration ci/configuration/cram/pair.sh \
        --configuration-arguments "$lan_ip" "$guest_ip" "$wired_interface"
    tools/deploy_ipk.sh --no-host-key-cache \
        "root@$host" "build/$device/prplmesh.ipk"
}

# Deploy the configured agent and controller in the required pair order.
# Input: the PAIR_* and TS_* variables exported by load_pair.
# Output: updates both targets and returns failure when either deployment fails.
deploy_pair() {
    deploy_pair_device "$PAIR_AGENT_DEVICE" "$PAIR_AGENT_TARGET" \
        "$PAIR_AGENT_IP" "$PAIR_AGENT_IMAGE" "$PAIR_AGENT_ROOTFS" \
        "$PAIR_AGENT_BOOT_IP" "$PAIR_AGENT_IP" "$TS_AGENT_GUEST_IP" \
        "$TS_AGENT_WIRED"
    deploy_pair_device "$PAIR_CONTROLLER_DEVICE" "$PAIR_CONTROLLER_TARGET" \
        "$PAIR_CONTROLLER_IP" "$PAIR_CONTROLLER_IMAGE" \
        "$PAIR_CONTROLLER_ROOTFS" "$PAIR_CONTROLLER_BOOT_IP" \
        "$PAIR_CONTROLLER_IP" "$TS_CONTROLLER_GUEST_IP" \
        "$TS_CONTROLLER_WIRED"
}

# Run one named pair scenario in the matching Cram container and timeout.
# Input: $1 is wps, eth, or wifi; pair and CI variables come from the environment.
# Output: stores Cram artifacts and returns the test process status.
run_pair_cram() {
    local scenario="$1" test_file timeout_sec run_dir

    case "$scenario" in
        eth)
            test_file=TrafficSeparation_ETH.t
            timeout_sec=1800s
            ;;
        wifi)
            test_file=TrafficSeparation_WIFI.t
            timeout_sec=2400s
            ;;
        wps)
            test_file=WPS_WIFI.t
            timeout_sec=1200s
            ;;
        *)
            echo "Unknown CRAM pair scenario: $scenario" >&2
            return 2
            ;;
    esac

    run_dir="$CI_PROJECT_DIR/cram-pair-run/$scenario"
    install -d "$run_dir" "$CI_PROJECT_DIR/cram-pair-artifacts"
    cp -R "$CI_PROJECT_DIR/tests/cram/pair/." "$run_dir/"
    export CRAM_CONTROLLER_COMMAND="ssh -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@$PAIR_CONTROLLER_IP"
    export CRAM_AGENT_COMMAND="ssh -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@$PAIR_AGENT_IP"

    timeout --kill-after=240s --verbose "$timeout_sec" \
        docker run --rm --init --user "$(id -u):$(id -g)" \
        -v /etc/passwd:/etc/passwd:ro \
        -v "$run_dir:/pair" -w /pair \
        -v "$CI_PROJECT_DIR/cram-pair-artifacts:/artifacts" \
        -e CI_COMMIT_SHA -e CRAM_CONTROLLER_COMMAND -e CRAM_AGENT_COMMAND \
        -e CRAM_PAIR_ARTIFACT_ROOT=/artifacts \
        -e PAIR_CONTROLLER_IP -e PAIR_CONTROLLER_MODEL \
        -e PAIR_CONTROLLER_PLATFORM -e PAIR_AGENT_IP -e PAIR_AGENT_MODEL \
        -e PAIR_AGENT_PLATFORM -e PAIR_AGENT_ENDPOINT \
        -e PAIR_FH_SSID_NAME -e PAIR_GUEST_SSID_NAME -e PAIR_BH_SSID_NAME \
        -e TS_CONTROLLER_GUEST_IP -e TS_AGENT_GUEST_IP \
        -e TS_CONTROLLER_WIRED -e TS_AGENT_WIRED \
        "$CI_REGISTRY_IMAGE/prplmesh-cram:$PARENT_PIPELINE_ID" \
        python3 -m cram --shell=/bin/bash --verbose "$test_file"
}

# Isolate the pair, redeploy it for each scenario, and retain the first failure.
# Inputs: positional parameters are one or more wps, eth, or wifi scenarios.
# Output: returns the first nonzero scenario status after all scenarios run.
main() {
    local scenario scenario_status status=0

    [[ "$#" -gt 0 ]] || {
        echo "Usage: $0 {wps|eth|wifi}..." >&2
        return 2
    }
    load_pair
    ci/cram-testbed-isolation.sh isolate \
        "$PAIR_CONTROLLER_TARGET" "$PAIR_AGENT_TARGET"

    for scenario in "$@"; do
        deploy_pair
        ci/cram-testbed-isolation.sh enforce
        if run_pair_cram "$scenario"; then
            scenario_status=0
        else
            scenario_status=$?
        fi
        if [[ "$status" -eq 0 && "$scenario_status" -ne 0 ]]; then
            status=$scenario_status
        fi
    done
    return "$status"
}

main "$@"
