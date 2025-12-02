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


class Mozart(GenericPrplOS):
    """An Arcadyan Mozart running prplOS.

    A tftp server must be running to serve images, and both 'serverip'
    and 'ipaddr' should already be set in the bootloader.
    """

    """The u-boot prompt on the target."""
    bootloader_prompt = r"MT7988> "
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
