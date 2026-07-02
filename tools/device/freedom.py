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


class Freedom(GenericPrplOS):
    """A WNC Freedom running prplOS.

    A tftp server must be running to serve images, and both 'serverip'
    and 'ipaddr' should already be set in the bootloader.
    """

    bootloader_prompt = r"IPQ9574# "
    """The u-boot prompt on the target."""

    update_script = "openwrt-ipq95xx-generic-prpl_freedom-update_script.itb"
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

        shell.sendline("setenv serverip 192.168.250.199")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv ipaddr 192.168.250.150")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv loadaddr 0x50000000")
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
        if self.rootfs is not None:
            shell.sendline(f"setenv img_rootfs {self.rootfs}")
        else:
            shell.sendline(f"setenv img_rootfs")
        shell.expect(self.bootloader_prompt)

        shell.sendline("setenv img_uboot")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv img_xbl")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv img_smd")
        shell.expect(self.bootloader_prompt)
        shell.sendline("setenv update_rescue_bank yes")
        shell.expect(self.bootloader_prompt)

        shell.sendline("mmc dev 0")
        shell.expect(self.bootloader_prompt)
        shell.sendline("mmc erase 0x534022 0x40000")
        shell.expect("blocks erased: OK")
        shell.expect(self.bootloader_prompt)

        shell.sendline("run update_prpl")
        shell.expect("kernel.itb written successfully to kernel-active", timeout=45)
        shell.expect("kernel.itb written successfully to kernel-inactive", timeout=45)
        shell.expect("rootfs.itb written successfully to rootfs-active", timeout=45)
        shell.expect("rootfs.itb written successfully to rootfs-inactive", timeout=45)
        shell.expect("img_uboot not set")
        shell.expect("img_xbl not set")
        shell.expect("img_smd not set")
        shell.expect("Done.")
        shell.expect(self.bootloader_prompt)
        print("Flashing completed successfully")
