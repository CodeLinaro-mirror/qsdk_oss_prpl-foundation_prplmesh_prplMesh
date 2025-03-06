# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from capi import tlv
from opts import debug
import environment as env
import time


class ClientSteeringMandate(PrplMeshBaseTest):
    """
        Devices used in test setup:
        AP1 - Agent1 [DUT]
        AP2 - Agent2 [LAN2]

        GW - Controller
    """

    def runTest(self):
        # Locate test participants
        try:
            sta = self.dev.wifi
            agent1 = self.dev.DUT.agent_entity
            agent2 = self.dev.lan2.agent_entity

            controller = self.dev.lan.controller_entity
        except AttributeError as ae:
            raise SkipTest(ae)

        debug("Send topology request to agent 1")
        controller.dev_send_1905(
            agent1.mac, self.ieee1905['eMessageType']['TOPOLOGY_QUERY_MESSAGE'])
        time.sleep(1)
        debug("Confirming topology query was received")
        self.check_log(agent1, "TOPOLOGY_QUERY_MESSAGE")

        debug("Send topology request to agent 2")
        controller.dev_send_1905(
            agent2.mac, self.ieee1905['eMessageType']['TOPOLOGY_QUERY_MESSAGE'])
        time.sleep(1)
        debug("Confirming topology query was received")
        self.check_log(agent2, "TOPOLOGY_QUERY_MESSAGE")

        sniffer = self.dev.DUT.wired_sniffer
        sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)

        self.configure_ssids(['ClientSteeringMandate'])
        debug("Connect dummy STA to wlan0")
        sta.wifi_connect(agent1.radios[0].vaps[0])
        time.sleep(1)
        debug("Check dummy STA connected to repeater1 radio")
        self.check_topology_notification(agent1.mac, [controller.mac, agent2.mac], sta,
                                         env.StationEvent.CONNECT, agent1.radios[0].vaps[0].bssid)

        debug(
            "Send Client Steering Request message for Steering Mandate to CTT Agent1")
        controller.dev_send_1905(agent1.mac, self.ieee1905['eMessageType']
        ['CLIENT_STEERING_REQUEST_MESSAGE'], tlv(self.ieee1905['eTlvTypeMap']
        ['TLV_STEERING_REQUEST'],
                 "{%s 0xe0 0x0000 0x1388 0x01 {%s} 0x01 {%s 0x73 0x24}}" % (  # noqa
                     agent1.radios[0].mac,
                     sta.mac,
                     agent2.radios[0].mac)))
        time.sleep(1)
        debug(
            "Confirming Client Steering Request message was received - mandate")
        self.check_log(agent1.radios[0], "Got steer request")

        debug("Confirming BTM Report message was received")
        self.check_log(controller,
                       "CLIENT_STEERING_BTM_REPORT_MESSAGE")

        debug("Checking BTM Report source bssid")
        self.check_log(controller,
                       "BTM_REPORT from source bssid %s" %
                       agent1.radios[0].mac)

        debug("Confirming ACK message was received")
        self.check_log(agent1.radios[0], "ACK_MESSAGE")

        controller.dev_send_1905(agent1.mac, self.ieee1905['eMessageType']
        ['CLIENT_STEERING_REQUEST_MESSAGE'], tlv(self.ieee1905['eTlvTypeMap']
        ['TLV_STEERING_REQUEST'],
        "{%s 0x00 0x000A 0x0000 0x00}" % agent1.radios[0].mac))  # noqa E501
        time.sleep(1)
        debug(
            "Confirming Client Steering Request message was received - Opportunity")
        self.check_log(agent1.radios[0],
                       "CLIENT_STEERING_REQUEST_MESSAGE")

        debug("Confirming ACK message was received")
        self.check_log(controller, "ACK_MESSAGE")

        debug("Confirming steering completed message was received")
        self.check_log(controller, "STEERING_COMPLETED_MESSAGE")

        debug("Confirming ACK message was received")
        self.check_log(agent1.radios[0], "ACK_MESSAGE")
