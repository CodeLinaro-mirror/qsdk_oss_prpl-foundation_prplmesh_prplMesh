# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from opts import debug

import re
import time
import environment as env


class NbapiAssociationEvent(PrplMeshBaseTest):
    '''
    Test for the USP event Device.WiFi.DataElements.AssociationEvent.Associated!.
    The Controller emits the event once when a STA associates to a BSS.
    '''
    @env.process_faults_check
    def runTest(self):
        try:
            controller = self.dev.lan.controller_entity
            agent = self.dev.DUT.agent_entity
            vap = agent.radios[1].vaps[0]
            sta = self.dev.wifi
        except AttributeError as ae:
            raise SkipTest(ae)

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)
        self.configure_ssids(["NbapiAssociationEvent"])
        time.sleep(3)

        output = self.start_event_capture(controller, "Device.WiFi.DataElements.AssociationEvent")

        debug(f"Client [{sta.mac}] connects to [{vap.bssid}].")
        sta.wifi_connect(vap)
        self.check_log(
            controller, f"client connected, client_mac={sta.mac}, bssid={vap.bssid}", timeout=10)

        events = self.wait_for_events(controller, output, "Associated!", sta.mac)
        # Associated! is emitted at the latest 3 s after the join. Wait longer to see that the
        # join gives only one event.
        time.sleep(4)
        events = [event for event in
                  self.get_captured_events(controller, output, "Associated!")
                  if event.get("MACAddress") == sta.mac]
        debug(f"Associated! events of {sta.mac}: {events}")
        assert len(events) == 1, \
            f"Wrong amount [{len(events)}] of Associated! events for one association."

        event = events[0]
        assert event.get("BSSID") == vap.bssid, \
            f"Wrong value for BSSID {event.get('BSSID')}, expect {vap.bssid}"
        assert event.get("StatusCode") == 0, \
            f"StatusCode should be 0 not {event.get('StatusCode')}"
        time_stamp = event.get("TimeStamp", "")
        time_format = r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(.\d+)?(Z|[-+]\d{2}:\d{2})'
        if re.match(time_format, time_stamp) is None:
            self.fail(f'Fail. Associated! time stamp has incorrect format: {time_stamp}')

        sta.wifi_disconnect(vap)
        self.check_log(
            controller, f"client disconnected, client_mac={sta.mac}, bssid={vap.bssid}",
            timeout=10)
