# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from opts import debug

import time
import environment as env


class NbapiChannelSelectionRequest(PrplMeshBaseTest):
    '''
    Convenience helper. Waits for channel on a given radio
    to match on the provided lambda function.
    '''
    def waitForChannel(self, radio, match_fn, timeout_sec=5):
        deadline = time.monotonic() + timeout_sec
        while time.monotonic() < deadline:
            time.sleep(1)
            chan = radio.get_current_channel()
            debug(chan)
            if chan and match_fn(chan.channel):
                return True

        return False

    '''
    Test NBAPI Device.WiFi.DataElements.Network.Device.{i}.Radio.{i}.ChannelSelectionRequest.

    Flow:
    - Set 2.4GHz radio of the controller's agent to 1 20MHz
    - Tell it that the channel is inoperable (pref=0)
    - Wait for it to move away to anything other than the disabled channel

    Assumptions:
    - radio[0] == 2.4GHz
    - bandwidth isn't verified because both opclasses get blocked
    '''
    @env.process_faults_check
    def runTest(self):
        try:
            # The test wants control over radio. Extender
            # agents are bound to the EndPoint radio used
            # for backhaul. Only the controller's agent is
            # guaranteed to have the freedom to change any
            # radio parameters freely.
            agent = self.dev.lan.controller_entity
            controller = self.dev.lan.controller_entity
        except AttributeError as ae:
            raise SkipTest(ae)

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)
        self.configure_ssids_clear()

        debug("Waiting for topology")
        topology = self.get_topology()
        repeater = topology[agent.mac]
        agent_radio = agent.radios[0]
        agent_radio_path = repeater.radios[agent.radios[0].mac].path

        dut_channel = 1
        non_oper_pref = 0
        cs_args = "{} {} 20 0 2412".format(agent.radios[0].mac, dut_channel)

        debug("Setting up: preparing radio0 to run on channel 1 @ 20MHz")
        controller.beerocks_cli_command('ap_channel_switch {}'.format(cs_args))
        assert self.waitForChannel(agent_radio, lambda c: c == 1)

        nb_func = "ChannelSelectionRequest"
        nb_args = {
            "Class": {
                # This is the: channel 1 @ HT20
                "1": {
                    "OpClass": "81",
                    "Channel": {
                        "1": {
                            "Channel": str(dut_channel),
                            "Preference": str(non_oper_pref)
                        }
                    }
                },

                # This is the: channel 1 @ HT40+
                "2": {
                    "OpClass": "83",
                    "Channel": {
                        "1": {
                            "Channel": str(dut_channel),
                            "Preference": str(non_oper_pref)
                        }
                    }
                },

                # There's no channel 1 @ HT40-, hence no
                # entry for op_class 84.
            }
        }

        debug(f"Triggering ChannelSelectionRequest to switch away from channel"
              f" {dut_channel} to something else")
        controller.nbapi_command(agent_radio_path, nb_func, nb_args)

        debug("Waiting for channel to change")
        switched = self.waitForChannel(agent_radio, lambda c: c != 1)
        assert switched, "Failed to switch away from the channel"

        chan = agent_radio.get_current_channel()
        debug(f"NBAPI ChannelSelectionRequest succeeded. Radio switched away from channel"
              f" {dut_channel} to {chan.channel}")
