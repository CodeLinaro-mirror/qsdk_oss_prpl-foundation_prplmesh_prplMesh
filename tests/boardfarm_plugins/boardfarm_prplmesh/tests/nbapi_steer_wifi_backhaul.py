
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2021 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from opts import debug

import time
import environment as env

class NbapiSteerWifiBackhaul(PrplMeshBaseTest):
    '''
    Test NBAPI Device.WiFi.DataElements.Network.Device.{i}.MultiAPDevice.Backhaul.SteerWiFiBackhaul.

    Flow:
    - Verify initial star topology (both extenders connected to gateway).
    - Find Agent1 backhaul BSS (BackhaulUse == True) to use as TargetBSS.
    - Trigger steer of Agent2 backhaul to Agent1's backhaul BSS.
    - Wait for topology to become linear (Agent2's BackhaulALID == Agent1 ALID).
    '''
    @env.process_faults_check
    def runTest(self):
        try:
            agent1 = self.dev.DUT.agent_entity
            agent2 = self.dev.lan2.agent_entity
            controller = self.dev.lan.controller_entity
        except AttributeError as ae:
            raise SkipTest(ae)

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)

        debug("Initial topology (expect star: both extenders -> gateway)")
        topology = self.get_topology()
        for device in topology.values():
            debug(device)

        repeater1 = topology[agent1.mac]
        repeater2 = topology[agent2.mac]
        backhaul_path_1 = repeater1.path + ".MultiAPDevice.Backhaul"
        backhaul_path_2 = repeater2.path + ".MultiAPDevice.Backhaul"

        # Derive current backhaul ALIDs to verify star topology
        bh_alid_1 = controller.nbapi_get_parameter(backhaul_path_1, "BackhaulALID")
        bh_alid_2 = controller.nbapi_get_parameter(backhaul_path_2, "BackhaulALID")
        assert bh_alid_1 == bh_alid_2, \
            f"Extenders should initially share the same parent ALID (star). " \
            f"Agent1 parent: {bh_alid_1}, Agent2 parent: {bh_alid_2}"

        # Derive Agent1 backhaul BSSID to be used as TargetBSS for Agent2
        target_bssid = None
        for radio in repeater1.radios.values():
            for bss in radio.vaps.values():
                backhaul_use = controller.nbapi_get_parameter(bss.path, "BackhaulUse")
                if str(backhaul_use).title() == "True":
                    target_bssid = bss.bssid
                    break
            if target_bssid:
                break

        assert target_bssid, "Failed to find Agent1 backhaul BSS to steer to"
        debug(f"Using Agent1 backhaul BSSID as TargetBSS: {target_bssid}")

        # Trigger steer on Agent2 to Agent1 to reform topology into linear
        debug("Trigger NBAPI SteerWiFiBackhaul on Agent2")
        ret = controller.nbapi_command(backhaul_path_2, "SteerWiFiBackhaul",
                                        {"TargetBSS": target_bssid,
                                        "Channel": "42",
                                        "TimeOut": "30"})
        # NBAPI function is deferred; ret may be empty or contain status, don't assert here.

        # Wait for Agent2 to update its parent to Agent1 (linear topology)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            time.sleep(2)
            bh_alid_2_new = controller.nbapi_get_parameter(backhaul_path_2, "BackhaulALID")
            bh_bssid_2_new = controller.nbapi_get_parameter(backhaul_path_2, "BackhaulMACAddress")
            if bh_alid_2_new == agent1.mac and bh_bssid_2_new.lower() == target_bssid.lower():
                break
        else:
            self.fail("Backhaul steer did not complete to Agent1 within timeout")

        # Verify final topology is linear: Agent2 parent becomes Agent1
        debug("Topology after steer (expect linear: gateway -> agent1 -> agent2)")
        topology = self.get_topology()
        for device in topology.values():
            debug(device)

        final_bh_alid_2 = controller.nbapi_get_parameter(backhaul_path_2, "BackhaulALID")
        final_bh_bssid_2 = controller.nbapi_get_parameter(backhaul_path_2, "BackhaulMACAddress")

        assert final_bh_alid_2 == agent1.mac, \
            f"Agent2 BackhaulALID should be Agent1 ALID after steer. " \
            f"Got {final_bh_alid_2}, expected {agent1.mac}"
        assert final_bh_bssid_2.lower() == target_bssid.lower(), \
            f"Agent2 BackhaulMACAddress should match TargetBSS after steer. " \
            f"Got {final_bh_bssid_2}, expected {target_bssid}"

        debug("NBAPI SteerWiFiBackhaul succeeded; topology moved star -> linear")
