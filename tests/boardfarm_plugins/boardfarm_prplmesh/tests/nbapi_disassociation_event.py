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


class NbapiDisassociationEvent(PrplMeshBaseTest):
    '''
    Test for the USP event Device.WiFi.DataElements.DisassociationEvent.Disassociated!.
    The Controller emits the event when a STA disassociates from a BSS.
    '''
    @env.process_faults_check
    def runTest(self):
        try:
            controller = self.dev.lan.controller_entity
            agent = self.dev.DUT.agent_entity
            vap = agent.radios[0].vaps[0]
            sta = self.dev.wifi
        except AttributeError as ae:
            raise SkipTest(ae)

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)
        self.configure_ssids(["NbapiDisassociationEvent"])
        time.sleep(3)

        sta.wifi_connect(vap)
        self.check_log(
            controller, f"client connected, client_mac={sta.mac}, bssid={vap.bssid}", timeout=10)

        output = self.start_event_capture(controller,
                                          "Device.WiFi.DataElements.DisassociationEvent")

        debug(f"Client [{sta.mac}] disconnects from [{vap.bssid}].")
        sta.wifi_disconnect(vap)
        self.check_log(
            controller, f"client disconnected, client_mac={sta.mac}, bssid={vap.bssid}",
            timeout=10)

        events = self.wait_for_events(controller, output, "Disassociated!", sta.mac)
        debug(f"Disassociated! events of {sta.mac}: {events}")
        assert len(events) == 1, \
            f"Wrong amount [{len(events)}] of Disassociated! events for one disassociation."

        event = events[0]
        assert event.get("BSSID") == vap.bssid, \
            f"Wrong value for BSSID {event.get('BSSID')}, expect {vap.bssid}"
        assert "ReasonCode" in event, "Disassociated! has no ReasonCode"
        time_stamp = event.get("TimeStamp", "")
        time_format = r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(.\d+)?(Z|[-+]\d{2}:\d{2})'
        if re.match(time_format, time_stamp) is None:
            self.fail(f'Fail. Disassociated! time stamp has incorrect format: {time_stamp}')
