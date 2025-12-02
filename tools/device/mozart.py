###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

# Standard library
import time

# Third party
import pexpect
import pexpect.fdpexpect
import pexpect.pxssh
from device.prplos import GenericPrplOS
from device.serial import SerialDevice
from device.utils import ShellType


class Mozart(GenericPrplOS):
    """An Arcadyan Mozart running prplOS.

    A tftp server must be running to serve images, and both 'serverip'
    and 'ipaddr' should already be set in the bootloader.
    """

    """The u-boot prompt on the target."""
    bootloader_prompt = r"MT7988> "
    boot_stop_expression = "ESC to quit"
    boot_stop_sequence = "0"
    bootloader_reboot_command = "run boot_production"

    def upgrade_from_u_boot(self, shell: pexpect.fdpexpect.fdspawn):
        shell.sendline("")
        shell.expect(self.bootloader_prompt)
        # Give the ethernet interfaces some time to initialize:
        time.sleep(10)
        shell.sendline(f"setenv noboot 1; setenv replacevol 1; run boot_tftp_production")
        shell.sendline("")
        shell.expect("Loading: ")
        shell.expect("done")
        shell.expect("blocks erased: OK")
        shell.expect("blocks written: OK")
        shell.expect(self.bootloader_prompt)

    def reboot(self, serial_type: ShellType, stop_in_bootloader: bool = False):
        with SerialDevice(self.baudrate, self.name,
                          self.serial_prompt, expect_prompt_on_connect=False) as shell:
            print("Reset board.")

            if serial_type == ShellType.UBOOT:
                if stop_in_bootloader:
                    shell.sendline("reset")
                else:
                    shell.sendline(self.bootloader_reboot_command)
            elif serial_type in [ShellType.PRPLOS, ShellType.RDKB, ShellType.LINUX_UNKNOWN]:
                shell.sendline("reboot ; sleep 15 && echo force rebooting && reboot -f")
            if stop_in_bootloader:
                print("Device will be stopped in its bootloader.")
                max_wait = 180  # total seconds to wait for boot menu
                start_time = time.time()
                boot_prompt_detected = False
                while time.time() - start_time < max_wait:
                    try:
                        shell.expect(self.boot_stop_expression, timeout=1)
                        shell.sendline(self.boot_stop_sequence)
                    except pexpect.TIMEOUT:
                        shell.sendline(self.boot_stop_sequence)
                    try:
                        shell.expect(self.bootloader_prompt, timeout=1)
                        boot_prompt_detected = True
                        print("Device stopped in bootloader.")
                        break
                    except pexpect.TIMEOUT:
                        continue
                if not boot_prompt_detected:
                    raise TimeoutError("Failed to stop device in bootloader within timeout.")
