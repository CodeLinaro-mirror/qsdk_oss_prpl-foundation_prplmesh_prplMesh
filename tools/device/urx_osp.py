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


class URXOSP(GenericPrplOS):
    """An MXL Open Service Platform URX device running prplOS.

    A tftp server must be running to serve images, and both 'serverip'
    and 'ipaddr' should already be set in the bootloader.
    """

    initialization_time = 280
    """The time (in seconds) the device needs to initialize when it boots
    for the first time after flashing a new image."""

    bootloader_prompt = r"Lightning # "
    """The u-boot prompt on the target."""

    bootloader_reboot_command = "run bootcmd"
    """The command to reboot the device in u-boot"""

    update_script = "openwrt-intel_x86-lgm-PRPL_OSP_v2-update_script.itb"
    """The name of the u-boot update script file"""

    def upgrade_from_u_boot(self, shell: pexpect.fdpexpect.fdspawn):
        """Upgrade from u-boot and remove the overlay.

        Parameters
        ----------
        shell: pexpect.fdpexpect.fdspawn
            The serial console to send commands to.
            It's assumed that the console is already stopped in u-boot.
        """
        shell.sendline("")
        shell.expect(self.bootloader_prompt)
        # Give the ethernet interfaces some time to initialize:
        time.sleep(5)

        shell.sendline("setenv serverip 192.165.100.199")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv ipaddr 192.165.100.160")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv loadaddr 0x8200000")
        shell.expect(self.bootloader_prompt)
        time.sleep(5)

        shell.sendline(f"tftpboot ${{loadaddr}} {self.update_script}")
        shell.expect("done")
        shell.expect(self.bootloader_prompt)
        shell.sendline("source ${loadaddr}:update-script")
        shell.expect("run update_prpl")
        shell.expect("========================================================")
        shell.expect(self.bootloader_prompt)

        shell.sendline(f"setenv img_kernel {self.image}")
        shell.expect(self.bootloader_prompt)
        shell.sendline(f"setenv img_rootfs")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv img_uboot")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv img_rbe")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv img_smd")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv update_rescue_bank yes")
        shell.expect(self.bootloader_prompt)

        shell.sendline("mmc dev 0")
        shell.expect(self.bootloader_prompt)
        shell.sendline("mmc erase 0x86c00 0x10000")
        shell.expect("blocks erased: OK")
        shell.expect(self.bootloader_prompt)

        shell.sendline("run update_prpl")
        shell.expect("image.ext4 written successfully to kernel-active.", timeout=45)
        shell.expect("image.ext4 written successfully to kernel-inactive", timeout=45)
        shell.expect("img_uboot not set")
        shell.expect("img_rbe not set")
        shell.expect("img_smd not set")
        shell.expect("Done.")
        shell.expect(self.bootloader_prompt)
        print("Flashing completed successfully")
