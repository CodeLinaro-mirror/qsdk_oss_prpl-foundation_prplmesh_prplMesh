#!/usr/bin/env python3
###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

"""Run a shell command through a target's serial console."""

import argparse
import shlex
import sys
import uuid

from device.generic import GenericDevice
from device.serial import SerialDevice


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("target", help="Target name for the /dev serial endpoint")
    args = parser.parse_args()
    command = sys.stdin.read()
    if not command:
        parser.error("a shell command is required on standard input")

    marker = f"PRPLMESH_SERIAL_COMMAND_{uuid.uuid4().hex}"
    serial_command = (
        f"printf '\\n{marker}_BEGIN\\n'; sh -c {shlex.quote(command)}; status=$?; "
        f"printf '\\n{marker}_END:%s\\n' \"$status\""
    )

    with SerialDevice(GenericDevice.baudrate, args.target,
                      GenericDevice.serial_prompt,
                      expect_prompt_on_connect=False,
                      logfile=None) as shell:
        shell.sendline("")
        shell.expect(GenericDevice.serial_prompt, timeout=60)
        shell.sendline(serial_command)
        shell.expect(f"{marker}_END:([0-9]+)", timeout=60)
        output = shell.before.decode(errors="replace").replace("\r\n", "\n")
        status = int(shell.match.group(1))

    begin = output.rfind(f"{marker}_BEGIN\n")
    if begin < 0:
        raise RuntimeError("serial command result markers were not received")
    begin += len(f"{marker}_BEGIN\n")
    sys.stdout.write(output[begin:])
    sys.exit(status)


if __name__ == "__main__":
    main()
