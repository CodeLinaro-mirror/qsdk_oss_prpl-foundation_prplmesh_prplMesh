# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from capi import tlv

from opts import debug

from time import sleep
import environment as env


class NbapiStaticPuncturing(PrplMeshBaseTest):
    '''
    Testing Handling of Static Puncturing in Agent
    '''
    def find_bss(self, ssid: str, radio, controller):
        for bss in radio.vaps.values():
            if bss.ssid == ssid:
                debug("self ssid")
                return bss
            if controller.nbapi_get_parameter(bss.path, "SSID") == ssid:
                debug("nbapi ssid")
                return bss

    @env.process_faults_check
    def runTest(self):
        try:
            agent = self.dev.DUT.agent_entity
            controller = self.dev.lan.controller_entity
        except AttributeError as ae:
            raise SkipTest(ae)

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)

        # Add Access Point object and set up parameters for it
        self.configure_ssids_clear()
        debug("roll logs for agent")
        agent.command("killall", "-SIGUSR1", "beerocks_agent")
        agent.command("killall", "-SIGUSR1", "beerocks_fronthaul")
        sleep(2)
        # update [logfile: size] dict with new values
        self.checkpoint()

        ssid_val = "ehtSSID"
        de_network = "Device.WiFi.DataElements.Network"
        ssid_nbapi_input = self.configure_ssid(ssid_val, "Fronthaul",
                                               {"Band5GL": True, "Band5GH": True})
        ssid_nbapi_sec_path = ssid_nbapi_input + ".Security"
        controller.nbapi_set_parameters(ssid_nbapi_sec_path, {"ModeEnabled": "WPA2-Personal"})
        controller.nbapi_set_parameters(ssid_nbapi_sec_path, {"KeyPassphrase": "password"})
        controller.nbapi_command(de_network, "AccessPointCommit")
        sleep(1)

        # set 5GHz radio on 160MHz BW
        chan_sel_payload = env.ChannelTlvs.CHANNEL_36_160.value
        chan_sel_tlv = tlv(self.ieee1905['eTlvTypeMap']['TLV_CHANNEL_PREFERENCE'],
                           '{} {}'.format(agent.radios[1].mac, chan_sel_payload))

        # bwl::dummy has a hardcoded list of possible channels
        # "36,40,44,48,52,56,60,64"
        expected_results = {
            "000000": "",
            "000001": "36",
            "000010": "40",
            "000011": "36,40",
            "000100": "44",
            "001000": "48",
            "001111": "36,40,44,48",
            "110000": "52,56",
        }

        topology = self.get_topology()
        repeater = topology[agent.mac]
        radio = repeater.radios[agent.radios[1].mac]
        bss = self.find_bss(ssid_val, radio, controller)
        debug("found BSS {}".format(bss.path))

        function_name = "SetEHTOperations"
        for input, result in expected_results.items():
            # as seen in ChannelSelection test; 0x8006 is ChannelSelectionRequest
            controller.dev_send_1905(agent.mac, 0x8006, chan_sel_tlv)
            sleep(1)

            function_args = {"DisabledSubchannelBitmap": int(input, 2)}
            controller.nbapi_command_not_fail(bss.path, function_name, function_args)

            sleep(15)
            self.check_log(agent.radios[1], "disabling: {} exactly".format(result))
