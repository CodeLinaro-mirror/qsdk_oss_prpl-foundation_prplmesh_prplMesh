#!/usr/bin/env bash

PAIR_REMOTE_TIMEOUT_SEC=${PAIR_REMOTE_TIMEOUT_SEC:-45}
PAIR_WAIT_TIMEOUT_SEC=${PAIR_WAIT_TIMEOUT_SEC:-180}
PAIR_WAIT_INTERVAL_SEC=${PAIR_WAIT_INTERVAL_SEC:-5}

# Verify that every named shell variable has a nonempty value.
# Inputs: positional parameters are variable names resolved by indirect expansion.
# Output: reports the first missing variable and returns failure, or returns success.
pair_require_variables() {
    local name

    for name in "$@"; do
        if [[ -z "${!name:-}" ]]; then
            printf 'Required variable is unset: %s\n' "$name" >&2
            return 1
        fi
    done
}

# Execute a command on one pair device with bounded runtime and optional stdin.
# Inputs: $1 is controller or agent, $2 is the command, and optional $3 is timeout.
# Output: forwards remote stdout/stderr and returns the timeout or remote status.
pair_remote_stdin() {
    local role="$1" command="$2"
    local timeout_sec="${3:-$PAIR_REMOTE_TIMEOUT_SEC}" remote_command
    local -a remote

    case "$role" in
        controller) remote_command=$CRAM_CONTROLLER_COMMAND ;;
        agent) remote_command=$CRAM_AGENT_COMMAND ;;
        *) printf 'Unknown pair role: %s\n' "$role" >&2; return 2 ;;
    esac
    read -r -a remote <<<"$remote_command"
    [[ "${#remote[@]}" -gt 0 ]] || return 2
    timeout --kill-after=2s "$timeout_sec" "${remote[@]}" "$command"
}

pair_remote() {
    pair_remote_stdin "$@" </dev/null
}

pair_quote_argument() {
    printf "'%s'" "${1//\'/\'\\\'\'}"
}

# Execute a stdin-provided shell script remotely with safely quoted arguments.
# Inputs: $1 is controller or agent; remaining parameters become script arguments.
# Output: forwards remote stdout/stderr and returns the remote script status.
pair_remote_script() {
    local role="$1" argument command='sh -s --'

    shift
    for argument in "$@"; do
        command+=" $(pair_quote_argument "$argument")"
    done
    pair_remote_stdin "$role" "$command"
}

# Poll a predicate command until it succeeds or the requested deadline expires.
# Inputs: $1 is timeout seconds, $2 describes the condition, and the rest is a command.
# Output: returns success when ready; on timeout, prints diagnostics and fails.
pair_wait() {
    local timeout_sec="$1" description="$2" deadline

    shift 2
    deadline=$((SECONDS + timeout_sec))
    while ((SECONDS < deadline)); do
        if "$@" >/dev/null 2>&1; then
            return 0
        fi
        sleep "$PAIR_WAIT_INTERVAL_SEC"
    done
    printf 'Timed out waiting for %s\n' "$description" >&2
    "$@" >&2 || true
    return 1
}

# Check that one device has reached the requested active prplMesh role.
# Inputs: $1 is controller or agent; $2 is the expected management mode.
# Output: returns success only when both management mode and status match.
pair_role_ready() {
    local role="$1" mode="$2"

    pair_remote_script "$role" "$mode" <<'ROLE_READY'
set -eu
mode=$1
[ "$(ba-cli -l 'X_PRPLWARE-COM_ProcessManager.PrplMesh.ManagementMode?' |
    sed '/^$/d')" = "$mode" ]
[ "$(ba-cli -l 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Status?' |
    sed '/^$/d')" = Active ]
ROLE_READY
}

pair_wait_role_ready() {
    pair_wait "$PAIR_WAIT_TIMEOUT_SEC" "$1 prplMesh role $2" \
        pair_role_ready "$1" "$2"
}

pair_ping_once() {
    local role="$1" source="$2" destination="$3"

    pair_remote_script "$role" "$source" "$destination" <<'PING_ONCE'
set -eu
ping -I "$1" -c 1 -W 2 "$2" >/dev/null 2>&1
PING_ONCE
}

pair_wait_ping() {
    pair_wait "$PAIR_WAIT_TIMEOUT_SEC" "$1 $2 reach $3" \
        pair_ping_once "$1" "$2" "$3"
}

# Verify a device's model, LAN address, and installed prplMesh revision.
# Inputs: $1 is role, $2 expected model, and $3 expected IP; the optional expected
# revision comes from PAIR_EXPECTED_REVISION.
# Output: writes mismatch diagnostics and returns failure, otherwise succeeds.
pair_check_device() {
    local role="$1" expected_model="$2" expected_ip="$3"
    local expected_revision="${PAIR_EXPECTED_REVISION:-}"

    pair_remote_script "$role" "$role" "$expected_model" "$expected_ip" \
        "$expected_revision" <<'DEVICE_CHECK'
set -eu
role=$1
expected_model=$2
expected_ip=$3
expected_revision=$4
model=$(ba-cli 'Device.DeviceInfo.ModelName?' |
    sed -n 's/.*ModelName="\([^"]*\)".*/\1/p' | head -n 1)
address=$(ip -4 -o address show dev br-lan scope global |
    sed -n 's/.* inet \([^/]*\)\/.*/\1/p' | head -n 1)
installed=$(sed -n 's/^prplmesh_revision=//p' \
    /opt/prplmesh/config/version | head -n 1)
[ "$model" = "$expected_model" ] || {
    printf '%s model mismatch: expected=%s observed=%s\n' \
        "$role" "$expected_model" "$model" >&2
    exit 1
}
[ "$address" = "$expected_ip" ] || {
    printf '%s address mismatch: expected=%s observed=%s\n' \
        "$role" "$expected_ip" "$address" >&2
    exit 1
}
[ "${#installed}" -ge 7 ] || {
    printf '%s prplMesh revision is missing\n' "$role" >&2
    exit 1
}
if [ -n "$expected_revision" ]; then
    case "$expected_revision" in
        "$installed"*) ;;
        *)
            printf '%s revision mismatch: expected=%s installed=%s\n' \
                "$role" "$expected_revision" "$installed" >&2
            exit 1
            ;;
    esac
fi
DEVICE_CHECK
}

# Validate that the configured controller and agent are distinct expected devices.
# Input: the PAIR_CONTROLLER_* and PAIR_AGENT_* environment variables.
# Output: returns failure on duplicate addresses or either device mismatch.
pair_preflight() {
    [[ "$PAIR_CONTROLLER_IP" != "$PAIR_AGENT_IP" ]] || {
        echo 'Controller and agent addresses must differ' >&2
        return 1
    }
    pair_check_device controller "$PAIR_CONTROLLER_MODEL" \
        "$PAIR_CONTROLLER_IP" || return
    pair_check_device agent "$PAIR_AGENT_MODEL" "$PAIR_AGENT_IP"
}

# Stop controller and agent data-model subscriptions left by pair tests.
# Input: remote commands configured by pair_init; there are no positional parameters.
# Output: performs best-effort cleanup and always completes both roles.
pair_stop_subscriptions() {
    local role

    for role in controller agent; do
        # The variables in this command expand on the remote board.
        # shellcheck disable=SC2016
        pair_remote "$role" \
            'for file in /tmp/cram_pair_*_events.pid; do [ -f "$file" ] || continue; kill "$(cat "$file")" 2>/dev/null || true; done' \
            >/dev/null 2>&1 || true
    done
}

# Collect logs and runtime state from one role into its artifact directory.
# Input: $1 is controller or agent; PAIR_ARTIFACT_DIR selects the destination.
# Output: creates local artifact files; unavailable remote data is tolerated.
pair_collect_role() {
    local role="$1" destination="$PAIR_ARTIFACT_DIR/$1"

    mkdir -p "$destination"
    # The variables in these commands expand on the remote board.
    # shellcheck disable=SC2016
    pair_remote "$role" \
        'for file in /var/log/messages /var/log/messages_*; do [ -f "$file" ] || continue; printf "===== %s =====\n" "$file"; cat "$file"; done' \
        >"$destination/messages.log" 2>&1 || true
    # shellcheck disable=SC2016
    pair_remote "$role" \
        'for file in /var/log/messages_wifi*; do [ -f "$file" ] || continue; printf "===== %s =====\n" "$file"; cat "$file"; done' \
        >"$destination/messages_wifi.log" 2>&1 || true
    pair_remote "$role" \
        'cat /tmp/cram_pair_*_events.log 2>/dev/null || true' \
        >"$destination/dm-subscription-events.log" 2>&1 || true
    pair_remote "$role" \
        'tar -czf - /tmp/beerocks/logs 2>/dev/null' \
        >"$destination/beerocks-logs.tar.gz" 2>/dev/null || true
    pair_remote "$role" \
        'ba-cli "X_PRPLWARE-COM_ProcessManager.PrplMesh.?" 2>/dev/null || true; ba-cli "X_PRPLWARE-COM_Agent.Info.?" 2>/dev/null || true; ba-cli "X_PRPLWARE-COM_Controller.Configuration.TrafficSeparation.?" 2>/dev/null || true; ba-cli "WiFi.AccessPoint.?" 2>/dev/null || true; ba-cli "WiFi.EndPoint.?" 2>/dev/null || true; ip -o link show; brctl show 2>/dev/null || true' \
        >"$destination/runtime-state.log" 2>&1 || true
}

# Perform idempotent pair cleanup, artifact collection, and wired-link restoration.
# Input: PAIR_* cleanup state and remote commands; there are no positional parameters.
# Output: sets PAIR_CLEANUP_DONE, writes artifacts, and restores the link when needed.
pair_cleanup() {
    [[ "${PAIR_CLEANUP_DONE:-0}" -eq 0 ]] || return 0
    PAIR_CLEANUP_DONE=1
    pair_stop_subscriptions
    pair_collect_role controller
    pair_collect_role agent
    if [[ "${PAIR_AGENT_WIRED_RESTORE:-0}" -eq 1 ]]; then
        pair_remote_script agent "$TS_AGENT_WIRED" \
            <<'RESTORE_WIRED' >/dev/null 2>&1 || true
set -eu
ip link set dev "$1" up
RESTORE_WIRED
    fi
}

pair_exit() {
    local status=$?

    trap - EXIT
    pair_cleanup
    exit "$status"
}

# Initialize shared pair-test state, validation, artifacts, and EXIT cleanup.
# Input: $1 is wps, eth, or wifi plus the required PAIR_* environment variables.
# Output: sets runtime globals, creates the artifact directory, and installs a trap.
pair_init() {
    local scenario="$1" artifact_root

    case "$scenario" in
        eth | wifi | wps) ;;
        *) printf 'Unknown pair scenario: %s\n' "$scenario" >&2; return 2 ;;
    esac
    pair_require_variables CRAM_CONTROLLER_COMMAND CRAM_AGENT_COMMAND \
        PAIR_CONTROLLER_IP PAIR_CONTROLLER_MODEL PAIR_CONTROLLER_PLATFORM \
        PAIR_AGENT_IP PAIR_AGENT_MODEL PAIR_AGENT_PLATFORM \
        PAIR_AGENT_ENDPOINT TS_AGENT_WIRED || return
    PAIR_EXPECTED_REVISION=${PAIR_EXPECTED_REVISION:-${CI_COMMIT_SHA:-}}
    artifact_root=${CRAM_PAIR_ARTIFACT_ROOT:-${TESTTMP:-/tmp}/cram-pair-artifacts}
    PAIR_ARTIFACT_DIR="$artifact_root/$scenario"
    mkdir -p "$PAIR_ARTIFACT_DIR"
    PAIR_AGENT_WIRED_RESTORE=0
    PAIR_CLEANUP_DONE=0
    trap pair_exit EXIT
}
