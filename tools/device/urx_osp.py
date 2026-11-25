###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

# Standard library
import glob
import gzip
import os
import shutil
import time

# Third party
import pexpect
import pexpect.fdpexpect
import pexpect.pxssh
from device.prplos import GenericPrplOS


class URXOSP(GenericPrplOS):
    """An MXL Open Service Platform URX device running prplOS.

    A tftp server must be running to serve images.
    """

    initialization_time = 280
    """The time (in seconds) the device needs to initialize when it boots
    for the first time after flashing a new image."""

    bootloader_prompt = r"Lightning # "
    """The u-boot prompt on the target."""

    bootloader_reboot_command = "run bootcmd"
    """The command to reboot the device in u-boot"""

    def copy_extra_tftp_files(self):
        """On OSPv2, the ext4.fs rootfs is also needed for the upgrade, gzipped."""
        if self.device != "urx_ospv2":
            return
        rootfs = glob.glob(os.path.join(self.artifacts_dir, "*ext4.fs"))
        if len(rootfs) != 1:
            raise ValueError("Expected exactly one '*ext4.fs' in {}, found: {}".format(
                self.artifacts_dir, rootfs))
        rootfs_gz = os.path.join(self.tftp_dir, os.path.basename(rootfs[0]) + ".gz")
        print("Compressing '{}' to '{}'".format(rootfs[0], rootfs_gz))
        with open(rootfs[0], "rb") as f_in, gzip.open(rootfs_gz, "wb") as f_out:
            shutil.copyfileobj(f_in, f_out)

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

        # Check if the compatibility bootloader is used
        shell.sendline("version")
        shell.expect("prpl-compat OCTOPUS-URX641-EMMC-OPEN_BOOT", timeout=5)
        shell.sendline("setenv prpl_legacy_boot on")
        shell.expect(self.bootloader_prompt)
        shell.sendline(f"setenv serverip {self.serverip}")
        shell.expect(self.bootloader_prompt)
        shell.sendline(f"setenv ipaddr {self.ipaddr}")
        shell.expect(self.bootloader_prompt)
        shell.sendline("saveenv")
        shell.expect("OK", timeout=5)
        shell.expect(self.bootloader_prompt)

        # Changes the default image name to upgrade
        if self.image:
            shell.sendline(f"env set fullimage {self.image}")
            shell.expect(self.bootloader_prompt)

        shell.sendline("setenv loadaddr 0x8200000")
        shell.expect(self.bootloader_prompt)
        time.sleep(5)
        # Image written to MMC
        shell.sendline("mmc dev 0")
        shell.expect(self.bootloader_prompt)
        shell.sendline("mmc erase 0x86c00 0x10000")
        shell.expect("blocks erased: OK")
        shell.expect(self.bootloader_prompt)

        shell.sendline("run update_fullimage")
        shell.expect("done", timeout=45)
        # Image transfer successful
        shell.expect("OK", timeout=15)
        shell.expect("upgrade succeed", timeout=45)
        shell.expect("upgrade succeed", timeout=45)
