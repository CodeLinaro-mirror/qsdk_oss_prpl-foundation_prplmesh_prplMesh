# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

import time

from boardfarm.exceptions import SkipTest
from capi import tlv
import environment as env
from opts import debug

from .prplmesh_base_test import PrplMeshBaseTest


class ApMetricsReportingInterval(PrplMeshBaseTest):
    """Exercise periodic AP metrics policy and explicit metric queries."""

    @env.process_faults_check
    def runTest(self):
        try:
            agent = self.dev.DUT.agent_entity
            controller = self.dev.lan.controller_entity
            vap = agent.radios[0].vaps[0]
        except AttributeError as error:
            raise SkipTest(error)

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)
        configuration = "X_PRPLWARE-COM_Controller.Configuration"
        parameter = "LinkMetricsRequestIntervalSec"
        original_interval = controller.nbapi_get_parameter(configuration, parameter)
        try:
            self.device_reset_then_set_config()
            for interval, expected_count in ((2, 6), (6, 2)):
                self.checkpoint()
                controller.nbapi_set_parameters(configuration, {parameter: interval})
                assert controller.nbapi_get_parameter(configuration, parameter) == interval
                self.configure_ssids([f"ApMetricsReportingInterval-{interval}"])
                ack = self.wait_for_controller_policy(controller, agent, interval)
                self.check_interval(controller, agent, vap.bssid, ack, interval, expected_count)
        finally:
            controller.nbapi_set_parameters(configuration, {parameter: original_interval})

    def wait_for_controller_policy(self, controller, agent, interval):
        """Wait for the policy sent at the end of autoconfiguration."""
        sniffer = self.dev.DUT.wired_sniffer
        message_types = self.ieee1905["eMessageType"]
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            policies = sniffer.get_cmdu_capture_type(
                message_types["MULTI_AP_POLICY_CONFIG_REQUEST_MESSAGE"], controller.mac, agent.mac,
            )
            for policy in reversed(policies):
                if not any(getattr(entry, "sta_metric_policy_ap_interval", None) == str(interval)
                           for entry in policy.ieee1905_tlvs):
                    continue
                acknowledgments = sniffer.get_cmdu_capture_type(
                    message_types["ACK_MESSAGE"], agent.mac, controller.mac, policy.ieee1905_mid,
                )
                if acknowledgments:
                    return acknowledgments[0]
            time.sleep(0.25)
        self.fail("Controller did not finish sending the autoconfiguration policy")

    def check_interval(self, controller, agent, bssid, ack, interval, expected_count):
        message_types = self.ieee1905["eMessageType"]
        ap_mid = controller.dev_send_1905(
            agent.mac, message_types["AP_METRICS_QUERY_MESSAGE"],
            tlv(self.ieee1905["eTlvTypeMap"]["TLV_AP_METRIC_QUERY"], "0x01 {%s}" % bssid),
        )
        link_mid = controller.dev_send_1905(
            agent.mac, message_types["LINK_METRIC_QUERY_MESSAGE"],
            tlv(self.ieee1905["eTlvType"]["TLV_LINK_METRIC_QUERY"], "0x00 0x02"),
        )

        # Observe six short intervals or two long intervals in the same capture window.
        window = 13
        time.sleep(window)
        query = self.check_cmdu_type_single(
            "explicit AP metrics query", message_types["AP_METRICS_QUERY_MESSAGE"],
            controller.mac, agent.mac, ap_mid,
        )
        response = self.check_cmdu_type_single(
            "explicit AP metrics response", message_types["AP_METRICS_RESPONSE_MESSAGE"],
            agent.mac, controller.mac, ap_mid,
        )
        response_delay = response.frame_time_epoch - query.frame_time_epoch
        assert 0 <= response_delay <= 1, f"AP Metrics response took {response_delay:.3f}s"
        self.check_cmdu_type_single(
            "explicit link metrics response", message_types["LINK_METRIC_RESPONSE_MESSAGE"],
            agent.mac, controller.mac, link_mid,
        )

        periodic_reports = self.dev.DUT.wired_sniffer.get_cmdu_capture_type(
            message_types["AP_METRICS_RESPONSE_MESSAGE"], agent.mac, controller.mac,
        )
        start = ack.frame_time_epoch
        times = [report.frame_time_epoch - start for report in periodic_reports
                 if report.frame_number != response.frame_number
                 and start < report.frame_time_epoch <= start + window]
        assert len(times) == expected_count, f"Interval {interval}s produced reports at {times}"
        gaps = [second - first for first, second in zip(times, times[1:])]
        assert all(abs(gap - interval) < 0.75 for gap in gaps), f"Unexpected report gaps {gaps}"
        debug(f"Interval {interval}s: {len(times)} periodic reports in {window}s, "
              f"explicit AP response after {response_delay:.3f}s")
