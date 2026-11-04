###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

import json
import re
import subprocess
import threading
import time

import yaml

from .prplmesh_base_test import PrplMeshBaseTest
from boardfarm.exceptions import SkipTest
from capi import tlv
import environment as env

TEMPLATES = "X_PRPLWARE-COM_Templates"
MESH_UUID_E = "X_PRPLWARE-COM_MeshPBCUUID_E"
FRONTHAUL_SSID = "Mesh-PBC-Fronthaul"
OTHER_SSID = "Mesh-PBC-Other"
LISTEN_DURATION = 10
ENROLLEE_MAC = "11:22:33:44:55:66"
ZERO_MAC = "00:00:00:00:00:00"
UUID_REGEX = r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"

MSG_PUSH_BUTTON_REQUEST = 0x804E
MSG_PUSH_BUTTON_TRIGGER = 0x804F
TLV_PUSH_BUTTON_TRIGGER = 0xEF

TRIGGER_METHOD_DPP = 0x01
TRIGGER_METHOD_WPS = 0x02
TRIGGER_STATE_STOP = 0x00
TRIGGER_STATE_START = 0x01


def uuid_e_from_mac(mac):
    """Make the UUID-E of a MAC address with the WPS UUID algorithm of pwhm (offset 0).

    The octets are the ASCII codes of the 12 hexadecimal digits of the MAC address, repeated,
    with the version nibble and the variant bits set to 0xf.
    """
    digits = mac.lower().replace(":", "")
    octets = [ord(digits[i % len(digits)]) for i in range(16)]
    octets[6] = (octets[6] & 0x0f) | 0xf0
    octets[8] = (octets[8] & 0x3f) | 0xf0
    text = "".join("{:02x}".format(octet) for octet in octets)
    return "-".join([text[0:8], text[8:12], text[12:16], text[16:20], text[20:32]])


class MeshPbcOnboarding(PrplMeshBaseTest):
    """Mesh-wide push-button onboarding on the dummy Wi-Fi backend.

    Checks the Agent side of the Multi-AP push-button messages:
        - The Controller data model shows the push-button capability of the repeater and the
          UUID-E made from the Controller AL MAC address.
        - A BSS staged from a BSSTemplate whose SSCTemplate references a MeshPBCTemplate gets
          the Push-Button Config TLV in its M2, and the dummy backend gets the push-button
          settings, also again in a later Renew with unchanged values.
        - The Agent answers each Multi-AP Push-Button Trigger with an ACK and the Multi-AP
          Push-Button Status messages, for the WPS pairing events that the dummy backend
          simulates, and ends a started session at the Listen Duration.
        - The mesh-wide push-button entry point of the repeater sends a Multi-AP Push-Button
          Request, because the Controller sets its push-button support bit.
        - With two repeaters, a template InitiatePBC() on the Controller arms the push-button
          BSS of each repeater, a Success on one repeater stops the other one and gives the
          Success reply, and the Agent-level InitiatePBC() and TerminatePBC() of the second
          repeater reach only that repeater.

    The test checks the Agent logs, the data model and the captured CMDUs.
    """

    @env.process_faults_check
    def runTest(self):
        try:
            agent = self.dev.DUT.agent_entity
            controller = self.dev.lan.controller_entity
        except AttributeError as ae:
            raise SkipTest(ae)

        self.agent = agent
        self.controller = controller
        self.radio = agent.radios[1]
        self.other_radio = agent.radios[0]
        self.session_mid = 0x0100
        self.added_templates = []
        self.network_enable = self.controller.nbapi_get_parameter(
            "{}.Network".format(TEMPLATES), "Enable")
        try:
            self.agent2 = self.dev.lan2.agent_entity
        except AttributeError as ae:
            raise SkipTest(ae)
        self.radio2 = self.agent2.radios[1]

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)
        self.check_capability_and_uuid_e()
        try:
            self.refresh_ap_capability()
            self.agent2.checkpoint()
            self.stage_templates()
            self.check_second_repeater_staged()
            self.check_renew_with_unchanged_values()
            self.check_trigger_flows()
            self.check_not_started_flows()
            self.check_mesh_wide_entry_point()
            self.check_template_session_on_two_repeaters()
            self.check_agent_session_on_second_repeater()
        finally:
            self.restore_configuration()

    def check_capability_and_uuid_e(self):
        """The Controller data model shows the capability of the repeater and the UUID-E."""
        devices = self.controller.nbapi_get_list_instances(
            "Device.WiFi.DataElements.Network.Device")
        for device in devices:
            if self.controller.nbapi_get_parameter(device, "ID").lower() == self.agent.mac:
                support = self.controller.nbapi_get_parameter(
                    device, "X_PRPLWARE-COM_MeshWidePBCOnboardingSupport")
                if support is not True:
                    self.fail("{}.X_PRPLWARE-COM_MeshWidePBCOnboardingSupport is {}"
                              .format(device, support))
                break
        else:
            self.fail("No Device of the repeater {} in the data model".format(self.agent.mac))

        self.uuid_e = self.controller.nbapi_get_parameter("{}.Network".format(TEMPLATES),
                                                          MESH_UUID_E)
        if not re.fullmatch(UUID_REGEX, self.uuid_e):
            self.fail("{}.Network.{} is not a canonical UUID: '{}'"
                      .format(TEMPLATES, MESH_UUID_E, self.uuid_e))
        expected = uuid_e_from_mac(self.controller.mac)
        if self.uuid_e != expected:
            self.fail("{}.Network.{} is {}, expected {} from the Controller AL MAC {}"
                      .format(TEMPLATES, MESH_UUID_E, self.uuid_e, expected,
                              self.controller.mac))

    def templates_add(self, table, parameters):
        """Add an instance of a Templates table on the Controller.

        Returns the path of the new instance.
        """
        new_instance = self.controller.nbapi_command("{}.{}".format(TEMPLATES, table), "_add",
                                                     {"parameters": parameters})
        self.added_templates.append((table, new_instance["index"]))
        return "{}.{}.{}".format(TEMPLATES, table, new_instance["index"])

    def check_push_button_bss(self):
        """Check the Push-Button Config TLV and the dummy settings of the push-button BSS.

        Returns the BSSID of the BSS.
        """
        self.check_log(self.radio,
                       r"Push-Button Config TLV \(radio " + self.radio.iface_name
                       + r", BSS_Index [1-9][0-9]*\) attached to the M2 of SSID '"
                       + FRONTHAUL_SSID + "': UUID-E " + self.uuid_e + ", Listen Duration "
                       + str(LISTEN_DURATION) + " s", timeout=90)
        _, _, match = self.check_log(
            self.radio, r"WPS push button armed on bssid ([0-9a-f:]{17}): UUID-E " + self.uuid_e
            + r", Device Password ID 4, Device Password length 0", timeout=30)
        return match[0].decode()

    def stage_templates(self):
        """Stage one push-button BSS and one other fronthaul BSS from Templates."""
        self.agent.checkpoint()
        radio_template = self.templates_add("RadioTemplate", {
            "RadioTemplateID": "mesh-pbc-rt-5", "Enable": True, "Priority": 200,
            "BandFlag": "5"})
        other_radio_template = self.templates_add("RadioTemplate", {
            "RadioTemplateID": "mesh-pbc-rt-24", "Enable": True, "Priority": 200,
            "BandFlag": "2.4"})
        ssc_template = self.templates_add("SSCTemplate", {
            "SSCTemplateID": "mesh-pbc-ssc", "SSCEnable": True, "HaulType": "Fronthaul"})
        other_ssc_template = self.templates_add("SSCTemplate", {
            "SSCTemplateID": "mesh-pbc-ssc-other", "SSCEnable": True, "HaulType": "Fronthaul"})
        security_template = self.templates_add("SecurityTemplate", {
            "SecurityTemplateID": "mesh-pbc-sec", "Enable": True, "Priority": 200,
            "RSNOSupportFlag": "null"})
        self.controller.nbapi_set_parameters(
            security_template + ".RSNE",
            {"GroupDataCipherSuite": "ccmp", "PairwiseCipherSuite": "ccmp", "AKMSuite": "psk",
             "MFP": "Capable"})
        security_group = self.templates_add("SecurityGroup", {
            "SecurityGroupID": "mesh-pbc-sg", "SecurityTemplateReferences": security_template})
        self.templates_add("BSSTemplate", {
            "BSSTemplateID": FRONTHAUL_SSID, "Enable": True, "SSID": FRONTHAUL_SSID,
            "KeyPassphrase": "password", "X_PRPLWARE-COM_VapType": "home",
            "RadioTemplateReference": radio_template,
            "SSCTemplateReference": ssc_template,
            "SecurityGroupReference": security_group})
        self.templates_add("BSSTemplate", {
            "BSSTemplateID": OTHER_SSID, "Enable": True, "SSID": OTHER_SSID,
            "KeyPassphrase": "password", "X_PRPLWARE-COM_VapType": "home",
            "RadioTemplateReference": other_radio_template,
            "SSCTemplateReference": other_ssc_template,
            "SecurityGroupReference": security_group})
        self.mesh_pbc_template = self.templates_add("X_PRPLWARE-COM_MeshPBCTemplate", {
            "PBCTemplateID": "mesh-pbc-t1", "ListenDuration": LISTEN_DURATION})
        self.controller.nbapi_set_parameters(
            ssc_template, {"X_PRPLWARE-COM_MeshPBCTemplateReference": self.mesh_pbc_template})
        self.controller.nbapi_set_parameters("{}.Network".format(TEMPLATES), {"Enable": True})

        self.bssid = self.check_push_button_bss()
        self.bss_iface = self.get_bss_iface(self.radio, self.bssid)
        self.other_bssid = self.get_bssid(self.other_radio, OTHER_SSID)

    def check_renew_with_unchanged_values(self):
        """A Renew with unchanged push-button values gives the dummy the settings again.

        A Device Password with Device Password ID 4 (PushButton) is not sent, so the change
        gives a Renew without a change of the Push-Button Config TLV.
        """
        self.agent.checkpoint()
        self.controller.nbapi_set_parameters(self.mesh_pbc_template + ".WPS",
                                             {"DevicePassword": "12345670"})
        bssid = self.check_push_button_bss()
        if bssid != self.bssid:
            self.fail("Push-button BSS {} after the Renew, {} before".format(bssid, self.bssid))

    def restore_configuration(self):
        """Restore the configuration of the start of the test.

        Removes the Templates instances of the test, sets Templates.Network.Enable back, and
        applies the SSID configuration of the start of every test again (no SSID), so that the
        Agents get it in an AP-Autoconfiguration Renew.
        """
        self.controller.nbapi_set_parameters_no_exception("{}.Network".format(TEMPLATES),
                                                          {"Enable": self.network_enable})
        for table, index in reversed(self.added_templates):
            self.controller.nbapi_command_not_fail(
                "{}.{}.{}".format(TEMPLATES, table, index), "_del")
        self.added_templates = []
        self.configure_ssids([])

    def get_bss_iface(self, radio, bssid):
        """Get the interface name of a BSS of a radio from the dummy status file."""
        for vap in yaml.safe_load(radio.read_tmp_file("vap")):
            if vap['bssid'] == bssid:
                return vap['iface']
        self.fail("No VAP with BSSID {} on {}".format(bssid, radio.iface_name))

    def get_bssid(self, radio, ssid, timeout=30):
        """Get the BSSID of the BSS of a radio with an SSID from the dummy status file."""
        deadline = time.time() + timeout
        while True:
            for vap in yaml.safe_load(radio.read_tmp_file("vap")):
                if vap['ssid'] == ssid and vap['fronthaul']:
                    return vap['bssid']
            if time.time() > deadline:
                self.fail("No fronthaul VAP with SSID {} on {}".format(ssid, radio.iface_name))
            time.sleep(1)

    def send_trigger(self, bssids, method=TRIGGER_METHOD_WPS, state=TRIGGER_STATE_START,
                     session_mid=None):
        """Send a Multi-AP Push-Button Trigger from the Controller UCC.

        Returns the MID of the message and the PBC Session ID MID.
        """
        if session_mid is None:
            self.session_mid += 1
            session_mid = self.session_mid
        value = "{{{} 0x{:04x} 0x{:02x} 0x{:02x} 0x{:02x}{}}}".format(
            self.controller.mac, session_mid, method, state, len(bssids),
            "".join(" " + bssid for bssid in bssids))
        mid = self.controller.dev_send_1905(self.agent.mac, MSG_PUSH_BUTTON_TRIGGER,
                                            tlv(TLV_PUSH_BUTTON_TRIGGER, value))
        return mid, session_mid

    def check_ack(self, mid):
        self.check_log(self.radio, r"Sending ACK message for the Multi-AP Push-Button Trigger "
                       r"mid=0x{:x} ".format(mid), timeout=10)

    def status_regex(self, bssid, session_mid, result, failure_reason=None, enrollee=ZERO_MAC):
        regex = r"Multi-AP Push-Button Status: BSSID {}, PBC Session ID {}/0x{:04x}, " \
                r"Result 0x0{} \([A-Z_]+\)".format(bssid, self.controller.mac, session_mid, result)
        if failure_reason is not None:
            regex += r", Failure Reason {}".format(failure_reason)
        return regex + r", Enrollee MAC " + enrollee

    def check_status(self, *args, timeout=10, **kwargs):
        self.check_log(self.radio, self.status_regex(*args, **kwargs), timeout=timeout)

    def check_no_status(self, bssid, session_mid):
        result, _, _ = self.check_log(
            self.radio, r"Multi-AP Push-Button Status: BSSID {}, PBC Session ID {}/0x{:04x}"
            .format(bssid, self.controller.mac, session_mid), timeout=3, fail_on_mismatch=False)
        if result:
            self.fail("Unexpected Status for BSSID {} and PBC Session ID MID 0x{:04x}"
                      .format(bssid, session_mid))

    def inject(self, opcode, reason=None, enrollee=None):
        """Simulate a WPS pairing event of the armed BSS in the dummy backend."""
        event = "EVENT {} {}".format(opcode, self.bss_iface)
        if reason:
            event += " reason=" + reason
        if enrollee:
            event += " macAddress=" + enrollee
        self.radio.send_bwl_event(event)

    def start_session(self):
        """Arm the push-button BSS: ACK, then Started. Returns the PBC Session ID MID."""
        self.agent.checkpoint()
        mid, session = self.send_trigger([self.bssid])
        self.check_ack(mid)
        self.check_status(self.bssid, session, 3)
        self.agent.checkpoint()
        return session

    def check_trigger_flows(self):
        bssid = self.bssid

        session = self.start_session()
        self.inject("WPS-PAIRING-DONE", "Success", ENROLLEE_MAC)
        self.check_status(bssid, session, 0, enrollee=ENROLLEE_MAC)

        session = self.start_session()
        self.inject("WPS-PAIRING-DONE", "Timeout")
        self.check_status(bssid, session, 1, 0)

        session = self.start_session()
        self.inject("WPS-PAIRING-DONE", "Overlap")
        self.check_status(bssid, session, 1, 12)

        session = self.start_session()
        stop_mid, _ = self.send_trigger([bssid], state=TRIGGER_STATE_STOP, session_mid=session)
        self.check_ack(stop_mid)
        self.check_log(self.radio, r"Cancel WPS PBC on " + bssid, timeout=10)
        self.check_status(bssid, session, 1, 0)

        session = self.start_session()
        stop_mid, stop_session = self.send_trigger([bssid], state=TRIGGER_STATE_STOP)
        self.check_ack(stop_mid)
        self.check_no_status(bssid, session)
        self.check_no_status(bssid, stop_session)
        if self.check_log(self.radio, r"Cancel WPS PBC on " + bssid, timeout=1,
                          fail_on_mismatch=False)[0]:
            self.fail("Cancel for a STOP with another PBC Session ID")
        self.inject("WPS-PAIRING-DONE", "Timeout")
        self.check_status(bssid, session, 1, 0)

        session = self.start_session()
        started = time.time()
        self.check_status(bssid, session, 1, 0, timeout=LISTEN_DURATION + 5)
        elapsed = time.time() - started
        if elapsed < LISTEN_DURATION - 3:
            self.fail("Session ended after {:.1f} s, Listen Duration {} s"
                      .format(elapsed, LISTEN_DURATION))
        self.check_log(self.radio, r"Mesh PBC session on " + bssid
                       + r" ended: Listen Duration expired", timeout=1)

    def check_not_started_flows(self):
        bssid = self.bssid

        self.agent.checkpoint()
        mid, session = self.send_trigger([bssid], method=TRIGGER_METHOD_DPP)
        self.check_ack(mid)
        self.check_status(bssid, session, 2, 0)

        unknown_bssid = "02:de:ad:be:ef:01"
        mid, session = self.send_trigger([unknown_bssid])
        self.check_status(unknown_bssid, session, 2, 0)

        self.agent.checkpoint()
        mid, session = self.send_trigger([self.other_bssid])
        self.check_log(self.other_radio, self.status_regex(self.other_bssid, session, 2, 0),
                       timeout=10)
        self.check_log(self.other_radio, r"Mesh PBC start on " + self.other_bssid
                       + r" not started: no push-button configuration with Listen Duration",
                       timeout=1)

        self.agent.checkpoint()
        mid, session = self.send_trigger([])
        self.check_ack(mid)
        self.check_log(self.radio, r"Multi-AP Push-Button Trigger mid=0x{:x} .* rejected: "
                       r"Num_BSSID is 0".format(mid), timeout=10)
        self.check_no_status("[0-9a-f:]{17}", session)

        session = self.start_session()
        duplicate_mid, _ = self.send_trigger([bssid], session_mid=session)
        self.check_ack(duplicate_mid)
        self.check_log(self.radio, r"already has an answer for this BSSID, duplicate Trigger "
                       r"ignored", timeout=10)
        self.check_no_status(bssid, session)
        _, busy_session = self.send_trigger([bssid])
        self.check_status(bssid, busy_session, 2, 14)
        self.inject("WPS-PAIRING-DONE", "Timeout")
        self.check_status(bssid, session, 1, 0)

    def check_mesh_wide_entry_point(self):
        """The entry point sends a Request: the Controller sets its push-button support bit."""
        self.agent.checkpoint()
        self.agent.nbapi_command("X_PRPLWARE-COM_Agent.MeshWidePBCOnboarding", "InitiatePBC")
        self.check_log(self.agent, r"Mesh-wide PBC: path=Request, "
                       r"controller_push_button_support=true", timeout=10)
        time.sleep(1)
        self.check_cmdu_type_single("Multi-AP Push-Button Request", MSG_PUSH_BUTTON_REQUEST,
                                    self.agent.mac, self.controller.mac)
        for radio in self.agent.radios:
            if self.check_log(radio, r"Start WPS PBC$", timeout=1, fail_on_mismatch=False)[0]:
                self.fail("Local WPS PBC start on the Request path")

    def refresh_ap_capability(self):
        """Make the Controller read the AP Capability TLV of each repeater again.

        The Controller gives a Push-Button Config TLV only to an Agent with RSN Overriding
        support, and reads that support only from an AP Capability TLV. A repeater sends its
        Early AP Capability Report once per connection, so an Agent object that the Controller
        creates again in an earlier test has no RSN Overriding support.
        """
        for agent in (self.agent, self.agent2):
            mid = self.controller.dev_send_1905(
                agent.mac, self.ieee1905['eMessageType']['AP_CAPABILITY_QUERY_MESSAGE'])
            self.check_log(self.controller,
                           r"Received AP_CAPABILITY_REPORT_MESSAGE, mid={}$".format(mid),
                           timeout=10)

    def check_second_repeater_staged(self):
        """The second repeater gets the push-button BSS on its 5 GHz radio too.

        Then waits until both repeaters end the configuration that follows the M2: the Agent
        ignores a Renew that arrives before that, and the next check needs a new Renew.
        """
        attached = r"Push-Button Config TLV \(radio {}, BSS_Index [1-9][0-9]*\) attached to " \
                   r"the M2 of SSID '" + FRONTHAUL_SSID + "'"
        _, line2, _ = self.check_log(self.radio2, attached.format(self.radio2.iface_name),
                                     timeout=90)
        _, _, match = self.check_log(
            self.radio2, r"WPS push button armed on bssid ([0-9a-f:]{17}): UUID-E " + self.uuid_e,
            timeout=30)
        self.bssid2 = match[0].decode()
        _, line, _ = self.check_log(self.radio, attached.format(self.radio.iface_name))
        self.check_log(self.radio, r"Finished configuration on " + self.radio.iface_name,
                       start_line=line, timeout=30)
        self.check_log(self.radio2, r"Finished configuration on " + self.radio2.iface_name,
                       start_line=line2, timeout=30)

    def device_path(self, al_mac):
        """Get the path of the Controller Device object of an Agent."""
        for device in self.controller.nbapi_get_list_instances(
                "Device.WiFi.DataElements.Network.Device"):
            if self.controller.nbapi_get_parameter(device, "ID").lower() == al_mac:
                return device
        self.fail("No Device of {} in the data model".format(al_mac))

    def bss_path(self, bssid):
        """Get the path of the Controller BSS object of a BSSID."""
        for device in self.controller.nbapi_get_list_instances(
                "Device.WiFi.DataElements.Network.Device"):
            for radio in self.controller.nbapi_get_list_instances(device + ".Radio"):
                for bss in self.controller.nbapi_get_list_instances(radio + ".BSS"):
                    if self.controller.nbapi_get_parameter(bss, "BSSID").lower() == bssid:
                        return bss
        self.fail("No BSS {} in the data model".format(bssid))

    def check_bss_result(self, bssid, result, timeout=15):
        """Wait until X_PRPLWARE-COM_MeshPBC.Result of a BSS is result; fail after timeout s."""
        path = self.bss_path(bssid) + ".X_PRPLWARE-COM_MeshPBC"
        deadline = time.time() + timeout
        while True:
            value = self.controller.nbapi_get_parameter(path, "Result")
            if value == result:
                return
            if time.time() > deadline:
                self.fail("{}.Result is '{}', expected '{}'".format(path, value, result))
            time.sleep(1)

    def start_call(self, path, method, args=None):
        """Start a Controller method whose reply comes at the session end.

        The ubus call runs in a thread. Returns the thread and a dict that gets the output of
        the call in "output", or the error in "error".
        """
        call = {}
        command = ["ubus", "-t", str(LISTEN_DURATION + 20), "call", path, method,
                   json.dumps(args or {})]

        def run():
            try:
                call["output"] = self.controller.command(*command)
            except subprocess.CalledProcessError as error:
                call["error"] = error

        thread = threading.Thread(target=run, daemon=True)
        thread.start()
        return thread, call

    def call_reply(self, thread, call):
        """Wait for the reply of a call of start_call(). Returns its out arguments."""
        thread.join(LISTEN_DURATION + 30)
        if thread.is_alive() or "error" in call:
            self.fail("No reply of the call: {}".format(call.get("error", "timeout")))
        decoder = json.JSONDecoder()
        text = call["output"].strip()
        values = []
        while text:
            value, end = decoder.raw_decode(text)
            values.append(value)
            text = text[end:].strip()
        while values:
            value = values.pop(0)
            if isinstance(value, dict):
                if "Status" in value:
                    return value
                values.extend(value.values())
            elif isinstance(value, list):
                values.extend(value)
        self.fail("No Status in the reply: {}".format(call["output"]))

    def check_trigger_start(self, radio, bssid):
        """The repeater Acks one Trigger with START for its own BSSID, then sends Started.

        Returns the PBC Session ID MID.
        """
        _, _, match = self.check_log(
            radio, r"Multi-AP Push-Button Trigger mid=0x([0-9a-f]+) from " + self.controller.mac
            + r": PBC Session ID " + self.controller.mac + r"/0x([0-9a-f]{4}), method DPP=0 WPS=1, "
            + "State={}, Num_BSSID=1".format(TRIGGER_STATE_START), timeout=10)
        mid = int(match[0].decode(), 16)
        session = int(match[1].decode(), 16)
        self.check_log(radio, r"Sending ACK message for the Multi-AP Push-Button Trigger "
                       r"mid=0x{:x} ".format(mid), timeout=10)
        self.check_log(radio, self.status_regex(bssid, session, 3), timeout=10)
        return session

    def check_no_trigger(self, radio, session, state):
        """Fail if radio logs a Trigger with this State for the PBC Session ID MID within 3 s."""
        result, _, _ = self.check_log(
            radio, r"Multi-AP Push-Button Trigger mid=0x[0-9a-f]+ from .*: PBC Session ID {}/0x"
            r"{:04x}, .*State={},".format(self.controller.mac, session, state), timeout=3,
            fail_on_mismatch=False)
        if result:
            self.fail("Unexpected Trigger with State={} for PBC Session ID MID 0x{:04x}"
                      .format(state, session))

    def check_template_session_on_two_repeaters(self):
        """A template InitiatePBC() arms both repeaters; a Success stops the other one."""
        self.agent.checkpoint()
        self.agent2.checkpoint()
        thread, call = self.start_call(self.mesh_pbc_template, "InitiatePBC")
        session = self.check_trigger_start(self.radio, self.bssid)
        if self.check_trigger_start(self.radio2, self.bssid2) != session:
            self.fail("The repeaters got Triggers with different PBC Session IDs")

        self.inject("WPS-PAIRING-DONE", "Success", ENROLLEE_MAC)
        self.check_status(self.bssid, session, 0, enrollee=ENROLLEE_MAC)
        self.check_log(self.radio2, r"Cancel WPS PBC on " + self.bssid2, timeout=10)
        self.check_log(self.radio2, self.status_regex(self.bssid2, session, 1, 0), timeout=10)
        self.check_no_trigger(self.radio, session, TRIGGER_STATE_STOP)

        reply = self.call_reply(thread, call)
        expected = {"Status": "Success", "EnrolleeMACAddress": ENROLLEE_MAC,
                    "BSSID": self.bssid}
        for name, value in expected.items():
            if str(reply.get(name, "")).lower() != value.lower():
                self.fail("InitiatePBC() reply {} is '{}', expected '{}' (reply {})"
                          .format(name, reply.get(name), value, reply))
        result = self.controller.nbapi_get_parameter(self.mesh_pbc_template + ".Status",
                                                     "Result")
        if result != "Success":
            self.fail("{}.Status.Result is '{}'".format(self.mesh_pbc_template, result))
        self.check_bss_result(self.bssid, "Success")
        self.check_bss_result(self.bssid2, "Error_Other")

    def check_agent_session_on_second_repeater(self):
        """The Agent-level methods of the second repeater reach only that repeater."""
        device = self.device_path(self.agent2.mac)
        self.agent.checkpoint()
        self.agent2.checkpoint()
        thread, call = self.start_call(device, "X_PRPLWARE-COM_InitiatePBC",
                                       {"PBCTemplateID": "mesh-pbc-t1"})
        session = self.check_trigger_start(self.radio2, self.bssid2)
        session_id = self.controller.nbapi_get_parameter(self.mesh_pbc_template + ".Status",
                                                         "PBCSessionID")
        if session_id != "{}-{:04x}".format(self.controller.mac, session):
            self.fail("{}.Status.PBCSessionID is '{}', the Trigger has MID 0x{:04x}"
                      .format(self.mesh_pbc_template, session_id, session))
        self.check_no_trigger(self.radio, session, TRIGGER_STATE_START)

        self.controller.command("ubus", "call", device, "X_PRPLWARE-COM_TerminatePBC",
                                json.dumps({"PBCSessionID": session_id}))
        stop = r"Multi-AP Push-Button Trigger mid=0x[0-9a-f]+ from .*: PBC Session ID {}/0x" \
               r"{:04x}, .*State={},".format(self.controller.mac, session, TRIGGER_STATE_STOP)
        self.check_log(self.radio2, stop, timeout=10)
        self.check_log(self.radio2, r"Cancel WPS PBC on " + self.bssid2, timeout=10)
        self.check_log(self.radio2, self.status_regex(self.bssid2, session, 1, 0), timeout=10)
        self.check_no_trigger(self.radio, session, TRIGGER_STATE_STOP)

        reply = self.call_reply(thread, call)
        if reply.get("Status") != "Error_Other":
            self.fail("X_PRPLWARE-COM_InitiatePBC() reply: {}".format(reply))
