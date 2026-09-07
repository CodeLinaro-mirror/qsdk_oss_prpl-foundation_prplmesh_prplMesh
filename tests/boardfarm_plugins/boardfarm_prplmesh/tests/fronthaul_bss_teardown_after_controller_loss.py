# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

import json
import re
import subprocess
import time

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
import environment as env
from opts import debug


class FronthaulBssTeardownAfterControllerLoss(PrplMeshBaseTest):
    """Verify that confirmed Controller loss tears down the Agent fronthaul BSSs."""

    SSID = "Boardfarm-Controller-Loss"

    @staticmethod
    def _wait_until_operational(agent, timeout: int = 240) -> None:
        deadline = time.monotonic() + timeout
        last_status = "prplmesh_cli did not return a status"
        last_observation = None
        while time.monotonic() < deadline:
            try:
                if isinstance(agent, env.ALEntityDocker):
                    last_status = agent.prplmesh_command(
                        "bin/prplmesh_cli", "-c", "status", "-o", "json")
                else:
                    last_status = agent.command(
                        "/opt/prplmesh/bin/prplmesh_cli", "-c", "status", "-o", "json")
                agent_status = json.loads(last_status).get("Agent", {})
                current_state = agent_status.get("CurrentState")
                controller_connected = agent_status.get("ControllerConnected")
                observation = (current_state, controller_connected)
                if observation != last_observation:
                    debug("Agent readiness: state={}, ControllerConnected={}".format(
                        current_state, controller_connected))
                    last_observation = observation
                if current_state == "OPERATIONAL" and controller_connected is True:
                    debug("Agent is OPERATIONAL with Controller connectivity")
                    return
            except (json.JSONDecodeError, subprocess.CalledProcessError) as error:
                last_status = str(error)
                observation = ("error", last_status)
                if observation != last_observation:
                    debug("Unable to read Agent status: {}".format(last_status))
                    last_observation = observation
            time.sleep(2)
        raise AssertionError(
            "Agent did not recover to OPERATIONAL with Controller connectivity. "
            f"Last status:\n{last_status}")

    @staticmethod
    def _restart_extender_prplmesh(agent, certification_mode: bool) -> None:
        debug("Restarting extender prplMesh with certification mode {}".format(
            "enabled" if certification_mode else "disabled"))
        if isinstance(agent, env.ALEntityDocker):
            agent.prplmesh_command(
                "scripts/prplmesh_utils.sh", "restart",
                "--cert", str(certification_mode).lower(), "--mode", "Multi-AP-Agent")
            return

        process_manager = "X_PRPLWARE-COM_ProcessManager.PrplMesh"
        # Changing CertificationMode makes ProcessManager restart prplMesh.
        agent.command(
            "ba-cli", "-j", "-l",
            process_manager + ".CertificationMode=" + str(int(certification_mode)))

    @staticmethod
    def _is_bss_down(vap) -> bool:
        if not hasattr(vap, "iface"):
            return vap.get_bss_type() == env.BssType.Disabled

        try:
            status = vap.radio.agent.command("hostapd_cli", "-i", vap.iface, "status")
        except subprocess.CalledProcessError:
            # Removing the BSS or its control socket is also a valid teardown result.
            return True
        return re.search(r"^state=DISABLED$", status, re.MULTILINE) is not None

    @env.process_faults_check
    def runTest(self):
        agent = self.dev.DUT.agent_entity
        controller = self.dev.lan.controller_entity

        if not isinstance(agent, (env.ALEntityDocker, env.ALEntityPrplWrt)):
            raise SkipTest("Controller-loss teardown test does not support this Agent type")

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)

        production_mode_enabled = False
        controller_node_isolated = False
        controller_outage_started = False
        try:
            # Boardfarm normally enables certification mode, which intentionally disables
            # ControllerConnectivityTask. Run the extender in production mode for this test and
            # restore certification mode before returning to the rest of the suite.
            production_mode_enabled = True
            self._restart_extender_prplmesh(agent, certification_mode=False)
            self._wait_until_operational(agent)

            debug("Configuring fronthaul test SSID {}".format(self.SSID))
            self.configure_ssids([self.SSID])
            configured_vaps = []
            for radio in agent.radios:
                radio_vaps = [vap for vap in radio.vaps if vap.get_ssid() == self.SSID]
                assert radio_vaps, f"SSID {self.SSID} was not configured on {radio.iface_name}"
                configured_vaps.extend(radio_vaps)

            self.checkpoint()

            # Keep the physical backhaul established while blocking all 1905 traffic from the
            # Controller node. Stopping only beerocks_controller is insufficient on a combined
            # Controller+Agent node because its local Agent can still refresh Controller contact.
            debug("Pausing Controller ieee1905_transport to simulate Controller loss")
            controller.command("killall", "-SIGSTOP", "ieee1905_transport")
            controller_node_isolated = True
            controller_outage_started = True

            for radio in agent.radios:
                self.check_log(radio, rf"BSS teardown completed on {radio.iface_name}", timeout=480)
                debug("Fronthaul BSS teardown completed on {}".format(radio.iface_name))

            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if all(self._is_bss_down(vap) for vap in configured_vaps):
                    debug("All configured fronthaul BSSs are disabled")
                    break
                time.sleep(1)
            else:
                active_bssids = [vap.bssid for vap in configured_vaps if not self._is_bss_down(vap)]
                self.fail("Fronthaul BSSs remained active after Controller loss: " +
                          ", ".join(active_bssids))
        finally:
            if controller_node_isolated:
                debug("Resuming Controller ieee1905_transport")
                controller.command("killall", "-SIGCONT", "ieee1905_transport")
                controller_node_isolated = False

            if production_mode_enabled:
                try:
                    if controller_outage_started:
                        self._wait_until_operational(agent)
                finally:
                    self._restart_extender_prplmesh(agent, certification_mode=True)
                    self._wait_until_operational(agent)

        self.prplmesh_status_check(agent)
