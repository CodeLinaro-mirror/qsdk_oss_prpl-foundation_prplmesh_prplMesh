###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

import re
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

        self.dev.DUT.wired_sniffer.start(self.__class__.__name__ + "-" + self.dev.DUT.name)
        self.check_capability_and_uuid_e()
        try:
            self.stage_templates()
            self.check_renew_with_unchanged_values()
            self.check_trigger_flows()
            self.check_not_started_flows()
            self.check_mesh_wide_entry_point()
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
