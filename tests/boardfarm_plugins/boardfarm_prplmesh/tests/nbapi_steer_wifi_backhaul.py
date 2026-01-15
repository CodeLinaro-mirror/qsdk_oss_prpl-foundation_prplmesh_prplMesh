
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2021 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from opts import debug, err
from capi import tlv

import time
import environment as env


class NbapiSteerWifiBackhaul(PrplMeshBaseTest):
    '''
    Test NBAPI Device.WiFi.DataElements.Network.Device.{i}.MultiAPDevice.Backhaul.SteerWiFiBackhaul.

    Flow:
    - Onboard repeater to gateway via wireless onboarding.
    - Verify initial topology (repeater connected to gateway).
    - Identify current backhaul BSS and find alternative backhaul BSS on gateway (different radio).
    - Trigger steer of repeater backhaul to gateway's alternative backhaul BSS.
    - Wait for backhaul to switch to target BSS while maintaining same parent (gateway).
    '''
    @env.process_faults_check
    def runTest(self):
        try:
            agent = self.dev.DUT.agent_entity
            controller = self.dev.lan.controller_entity
        except AttributeError as ae:
            raise SkipTest(ae)

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)

        # Onboard repeater to gateway via wireless onboarding
        debug("Starting wireless onboarding of repeater to gateway")
        
        gateway_radios = [r for r in controller.radios.values() if r.band == "5GHz"]
        if not gateway_radios:
            gateway_radios = list(controller.radios.values())
        if not gateway_radios:
            raise SkipTest("Gateway has no radios for onboarding")
        
        gateway_radio = gateway_radios[0]
        
        # Find fronthaul BSS on gateway radio
        gateway_bss = None
        for vap in gateway_radio.vaps.values():
            fronthaul_ssid = controller.nbapi_get_parameter(vap.path, "SSID")
            fronthaul_use = controller.nbapi_get_parameter(vap.path, "FronthaulUse")
            if str(fronthaul_use).title() == "True" and fronthaul_ssid:
                gateway_bss = vap
                break
        
        if not gateway_bss:
            raise SkipTest("Gateway has no fronthaul BSS for onboarding")
        
        debug(f"Using gateway BSS: {gateway_bss.bssid}, SSID: {fronthaul_ssid}")
        
        # Configure agent for wireless onboarding
        agent_radios = list(agent.radios.values())
        if not agent_radios:
            raise SkipTest("Agent has no radios")
        
        agent_radio = agent_radios[0]
        
        # Get credentials
        fronthaul_key = controller.nbapi_get_parameter(gateway_bss.path, "KeyPassphrase")
        fronthaul_akm = controller.nbapi_get_parameter(gateway_bss.path, 
                                                       "Security.ModeEnabled")
        
        # Configure agent radio for wireless backhaul connection
        agent.capi_set_config(
            agent_radio.uid,
            tlv(0x0107, 0x10, gateway_bss.bssid),  # BSS Configuration
        )
        
        # Wait for wireless onboarding to complete
        self.check_topology([controller.mac], 180, 2)

        debug("Initial topology (expect repeater connected to gateway)")
        topology = self.get_topology()
        for device in topology.values():
            debug(device)

        repeater = topology[agent.mac]
        backhaul_path = repeater.path

        # Get current backhaul information
        current_bh_alid = controller.nbapi_get_parameter(backhaul_path, "BackhaulALID")
        current_bh_bssid = controller.nbapi_get_parameter(backhaul_path, "BackhaulMACAddress")
        debug(f"Current backhaul: ALID={current_bh_alid}, BSSID={current_bh_bssid}")

        # Validate backhaul is established
        assert current_bh_alid and current_bh_alid.strip(), \
            f"Repeater does not have a valid BackhaulALID. Got: '{current_bh_alid}'"
        assert current_bh_bssid and current_bh_bssid != "00:00:00:00:00:00", \
            f"Repeater does not have a valid BackhaulMACAddress. Got: '{current_bh_bssid}'"

        # Find gateway device in topology
        assert current_bh_alid in topology, \
            (f"BackhaulALID {current_bh_alid} not found in topology. "
             f"Available devices: {list(topology.keys())}")
        gateway = topology[current_bh_alid]

        # Find alternative backhaul BSS on gateway (different from current backhaul)
        target_bssid = None
        for radio in gateway.radios.values():
            for bss in radio.vaps.values():
                backhaul_use = controller.nbapi_get_parameter(bss.path, "BackhaulUse")
                if (str(backhaul_use).title() == "True" and
                        bss.bssid.lower() != current_bh_bssid.lower()):
                    target_bssid = bss.bssid
                    break
            if target_bssid:
                break

        assert target_bssid, "Failed to find alternative backhaul BSS on gateway to steer to"
        debug(f"Using gateway's alternative backhaul BSSID as TargetBSS: {target_bssid}")

        # Trigger steer to alternative backhaul BSS on same parent (gateway)
        debug("Trigger NBAPI SteerWiFiBackhaul to alternative gateway BSS")
        controller.nbapi_command(backhaul_path + ".MultiAPDevice.Backhaul", "SteerWiFiBackhaul",
                                 {"TargetBSS": target_bssid,
                                  "TimeOut": "30"})

        # Wait for backhaul to switch to target BSS while maintaining same parent
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            time.sleep(2)
            bh_alid_new = controller.nbapi_get_parameter(backhaul_path, "BackhaulALID")
            bh_bssid_new = controller.nbapi_get_parameter(backhaul_path, "BackhaulMACAddress")
            if bh_alid_new == current_bh_alid and bh_bssid_new.lower() == target_bssid.lower():
                break
        else:
            self.fail("Backhaul steer did not complete to target BSS within timeout")

        # Verify final state: same parent, different BSS
        debug("Topology after steer (expect same parent, different backhaul BSS)")
        topology = self.get_topology()
        for device in topology.values():
            debug(device)

        final_bh_alid = controller.nbapi_get_parameter(backhaul_path, "BackhaulALID")
        final_bh_bssid = controller.nbapi_get_parameter(backhaul_path, "BackhaulMACAddress")

        assert final_bh_alid == current_bh_alid, \
            f"Repeater BackhaulALID should remain the same (gateway) after steer. " \
            f"Got {final_bh_alid}, expected {current_bh_alid}"
        assert final_bh_bssid.lower() == target_bssid.lower(), \
            (f"Repeater BackhaulMACAddress should match TargetBSS after steer. "
             f"Got {final_bh_bssid}, expected {target_bssid}")

        debug("NBAPI SteerWiFiBackhaul succeeded; "
              "backhaul switched to alternative BSS on same parent")

