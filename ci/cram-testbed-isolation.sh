#!/usr/bin/env bash

# Pass the targets that must remain active to "isolate"; every other configured
# CRAM target is disabled until "restore" returns it to its saved state.
# With the default freedom/freedom2/urx_ospv2/urx_ospv22 testbed, for example:
#   cram-testbed-isolation.sh isolate freedom freedom2  # keep two, disable two
#   cram-testbed-isolation.sh isolate freedom           # keep one, disable three
#   cram-testbed-isolation.sh isolate freedom urx_ospv2 urx_ospv22
#                                                       # disable only freedom2
# Run "enforce" after deployment to disable the same non-participants again.
set -euo pipefail

readonly state_dir="${CI_PROJECT_DIR:-$PWD}/.cram-testbed-isolation-${CI_JOB_ID:-local}"
read -r -a devices <<<"${CRAM_TESTBED_DEVICES:-freedom freedom2 urx_ospv2 urx_ospv22}"
readonly devices

# Run a command script on a CRAM target over serial, retrying transient failures.
# Inputs: $1 is the target name; stdin contains the command script.
# Output: writes command stdout and returns its status after at most three attempts.
run_serial_command() {
    local target="$1" commands output attempt

    commands="$(cat)"
    for attempt in 1 2 3; do
        echo "Running serial command on ${target} (attempt ${attempt}/3)" >&2
        if output="$(printf '%s\n' "${commands}" |
            tools/serial_command.py "${target}")"; then
            printf '%s\n' "${output}"
            return 0
        fi
        [ -z "${output}" ] || printf '%s\n' "${output}" >&2
        [ "${attempt}" -eq 3 ] || sleep 5
    done
    return 1
}

# Restore one isolated CRAM target from the state captured before isolation.
# Input: $1 is the target name; its saved state is read from state_dir.
# Output: restores radios, br-lan, and prplMesh; returns the serial command status.
restore_device() {
    local target="$1" state_file="${state_dir}/$1"
    local commands="" setting link_setting="" link_state="down" pm_setting=""

    while IFS= read -r setting; do
        case "${setting}" in
            WiFi.Radio.*.Enable=[01])
                commands+="ba-cli '${setting}'"$'\n'
                ;;
            X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=[01])
                pm_setting="${setting}"
                ;;
            Network.Interface.br-lan.Enable=[01])
                link_setting="${setting##*=}"
                ;;
            *)
                echo "Invalid saved CRAM testbed state for ${target}: ${setting}" >&2
                return 1
                ;;
        esac
    done <"${state_file}"

    [ -n "${link_setting}" ] && [ -n "${pm_setting}" ] || return
    echo "Restoring non-participating CRAM device ${target}"
    [ "${link_setting}" -eq 0 ] || link_state="up"
    commands+="ip link set dev br-lan ${link_state}"$'\n'
    printf "%sba-cli '%s'\n" "${commands}" "${pm_setting}" |
        run_serial_command "${target}" >/dev/null
}

# Restore every CRAM target disabled by the current isolation operation.
# Input: state files under state_dir; there are no positional parameters.
# Output: returns failure if any target could not be restored.
restore() {
    local state_file status=0

    [ -d "${state_dir}" ] || return 0
    for state_file in "${state_dir}"/*; do
        [ -f "${state_file}" ] || continue
        restore_device "${state_file##*/}" || status=1
    done
    return "${status}"
}

is_participant() {
    local target="$1" participant

    shift
    for participant in "$@"; do
        [ "${target}" != "${participant}" ] || return 0
    done
    return 1
}

# Stop prplMesh, radios, and wired connectivity on one non-participating target.
# Input: $1 is the CRAM target name.
# Output: returns success only after the target is verified to be inactive.
disable_device() {
    local target="$1"

    echo "Disabling non-participating CRAM device ${target}"
    # Keep the wired 1905 path down if a device service restarts prplMesh.
    printf '%s\n' \
        "set -e" \
        "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=0'" \
        "processes='beerocks_vendor_message beerocks_agent beerocks_fronthaul beerocks_controller ieee1905_transport'" \
        "for process in \$processes; do start-stop-daemon -K -s TERM -x \"/opt/prplmesh/bin/\$process\" >/dev/null 2>&1 || true; done" \
        "for _ in \$(seq 1 10); do ! pidof \$processes >/dev/null && break; sleep 1; done" \
        "for process in \$processes; do start-stop-daemon -K -s KILL -x \"/opt/prplmesh/bin/\$process\" >/dev/null 2>&1 || true; done" \
        "for _ in \$(seq 1 5); do ! pidof \$processes >/dev/null && break; sleep 1; done" \
        "ba-cli 'WiFi.Radio.*.Enable=0'" \
        "ip link set dev br-lan down" \
        "test \"\$(ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable?' | sed -n 's/.*Enable=\([01]\).*/\1/p')\" = 0" \
        "radio_state=\$(ba-cli 'WiFi.Radio.*.Enable?')" \
        "echo \"\$radio_state\" | grep -q 'Enable=0'" \
        "! echo \"\$radio_state\" | grep -q 'Enable=1'" \
        "! ip -o link show dev br-lan | sed -n 's/^[^<]*<\([^>]*\)>.*/\1/p' | tr ',' '\n' | grep -qx UP" \
        "! pidof \$processes >/dev/null" |
        run_serial_command "${target}" >/dev/null
}

# Reapply isolation to every target recorded in the current isolation state.
# Input: state files under state_dir; there are no positional parameters.
# Output: returns failure if any recorded target could not be disabled.
enforce() {
    local state_file status=0

    [ -d "${state_dir}" ] || {
        echo "CRAM testbed isolation state does not exist: ${state_dir}" >&2
        return 1
    }
    for state_file in "${state_dir}"/*; do
        [ -f "${state_file}" ] || continue
        disable_device "${state_file##*/}" || status=1
    done
    return "${status}"
}

# Save and disable every configured target except the requested participants.
# Inputs: all positional parameters are CRAM target names that must stay active.
# Output: creates state_dir and returns failure if capture or isolation fails.
isolate() {
    local output participant state_file target

    [ "$#" -gt 0 ] || {
        echo "At least one participating CRAM target is required" >&2
        return 2
    }
    [ ! -e "${state_dir}" ] || {
        echo "CRAM testbed isolation state already exists: ${state_dir}" >&2
        return 1
    }
    for participant in "$@"; do
        case " ${devices[*]} " in
            *" ${participant} "*) ;;
            *)
                echo "Unknown participating CRAM target: ${participant}" >&2
                return 2
                ;;
        esac
    done

    umask 077
    install -d -m 0700 "${state_dir}"
    for target in "${devices[@]}"; do
        is_participant "${target}" "$@" && continue
        state_file="${state_dir}/${target}"
        output="$(printf '%s\n' \
            "set +e" \
            "ba-cli 'X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable?'" \
            "ba-cli 'WiFi.Radio.*.Enable?'" \
            "if ip -o link show dev br-lan | sed -n 's/^[^<]*<\([^>]*\)>.*/\1/p' | tr ',' '\n' | grep -qx UP; then" \
            "echo 'Network.Interface.br-lan.Enable=1'" \
            "else" \
            "echo 'Network.Interface.br-lan.Enable=0'" \
            "fi" \
            "exit 0" |
            run_serial_command "${target}")" || {
            echo "WARNING: Cannot reach non-participating CRAM device ${target}; assuming prplMesh is disabled" >&2
            continue
        }
        printf '%s\n' "${output}" | sed -n \
            -e '/^X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=[01]$/p' \
            -e '/^WiFi.Radio\.[0-9][0-9]*\.Enable=[01]$/p' \
            -e '/^Network.Interface.br-lan.Enable=[01]$/p' >"${state_file}"
        if [ "$(grep -c '^X_PRPLWARE-COM_ProcessManager.PrplMesh.Enable=' "${state_file}")" -ne 1 ] ||
            ! grep -q '^WiFi.Radio\.[0-9][0-9]*\.Enable=' "${state_file}" ||
            [ "$(grep -c '^Network.Interface.br-lan.Enable=' "${state_file}")" -ne 1 ]; then
            echo "Could not capture CRAM testbed state for ${target}" >&2
            restore
            return 1
        fi
        disable_device "${target}" || {
            restore
            return 1
        }
    done
}

case "${1:-}" in
    isolate)
        shift
        isolate "$@"
        ;;
    enforce)
        enforce
        ;;
    restore)
        restore
        ;;
    *)
        echo "Usage: $0 {isolate <participant-target>...|enforce|restore}" >&2
        exit 2
        ;;
esac
