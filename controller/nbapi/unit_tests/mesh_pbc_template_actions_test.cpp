/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_test_fixture.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {

using namespace mesh_pbc_test;

// cppcheck-suppress syntaxError
TEST_F(MeshPbcTemplateTest, template_id_validation)
{
    EXPECT_FALSE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t1"}}));
    EXPECT_EQ(get(mesh_pbc_path(2), "PBCTemplateID"), "<no object>");
    EXPECT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    EXPECT_EQ(get(mesh_pbc_path(2), "PBCTemplateID"), "t2");

    EXPECT_FALSE(set(mesh_pbc_path(2), "PBCTemplateID", ""));
    EXPECT_EQ(get(mesh_pbc_path(2), "PBCTemplateID"), "t2");

    // An instance added with PBCTemplateID "" gets no other value. libamxd 6.11.2 (prplWare
    // 5.2.0) accepts it: the value equals the default, so the validator does not run. libamxd
    // 6.5.1 (builder image) rejects it.
    if (add(g_mesh_pbc_table, 3, {{"PBCTemplateID", ""}})) {
        EXPECT_EQ(get(mesh_pbc_path(3), "PBCTemplateID"), "");
    }
}

TEST_F(MeshPbcTemplateTest, push_button_method_validation)
{
    // libamxd check_enum accepts a repeated entry.
    check_values(mesh_pbc_path(1), "PushButtonMethod", {"DPP", "DPP,WPS", "WPS,WPS", "WPS"},
                 {"", "FOO", "WPS,FOO"});
}

TEST_F(MeshPbcTemplateTest, listen_duration_validation)
{
    // libamxd rejects -5 and 65536 by the uint16 type.
    check_values(mesh_pbc_path(1), "ListenDuration", {"1", "65535", "120"}, {"0", "-5", "65536"});
}

TEST_F(MeshPbcTemplateTest, button_press_scope_validation)
{
    EXPECT_EQ(get(mesh_pbc_path(1), "ButtonPressScope"), "MeshWide");
    EXPECT_EQ(get_type(mesh_pbc_path(1), "ButtonPressScope"), AMXC_VAR_ID_CSTRING);
    EXPECT_TRUE(amxd_param_is_attr_set(
        amxd_object_get_param_def(amxd_dm_findf(dm(), "%s.", mesh_pbc_path(1).c_str()),
                                  "ButtonPressScope"),
        amxd_pattr_persistent));
    check_values(mesh_pbc_path(1), "ButtonPressScope", {"RequestingDevice", "MeshWide"},
                 {"", "Local", "Device", "meshwide", "MeshWide,RequestingDevice"});
}

TEST_F(MeshPbcTemplateTest, mesh_uuid_e_validation)
{
    check_values(g_network, g_uuid_e_param,
                 {g_uuid_e, "ABCDEF01-2345-6789-ABCD-EF0123456789", g_uuid_e},
                 {"", "12345678", "12345678-9abc-def0-1234-56789abcdef",
                  "12345678x9abc-def0-1234-56789abcdef0", "g2345678-9abc-def0-1234-56789abcdef0",
                  "12345678-9abc-def0-1234-56789abcdef00", "123456789abcdef0123456789abcdef0"});
    EXPECT_GE(LogRecorder::count(el::Level::Warning, g_uuid_e_param + " rejected"), 7U);
}

TEST_F(MeshPbcTemplateNoUuidTest, mesh_uuid_e_default_can_be_rewritten)
{
    check_values(g_network, g_uuid_e_param, {"", g_uuid_e}, {""});
}

TEST_F(MeshPbcTemplateTest, template_references_accept_any_string)
{
    for (const auto &value :
         {std::string(""), mesh_pbc_path(9), ssc_path(1), std::string("not a reference"),
          g_tr181_root + ".X_PRPLWARE-COM_MeshPBCTemplate.1", mesh_pbc_path(1)}) {
        EXPECT_TRUE(set(g_network, g_primary, value)) << value;
        EXPECT_EQ(get(g_network, g_primary), value);
        EXPECT_TRUE(set(ssc_path(2), g_template_ref, value)) << value;
        EXPECT_EQ(get(ssc_path(2), g_template_ref), value);
    }
}

TEST_F(MeshPbcTemplateNoUuidTest, saved_mesh_uuid_e_must_be_a_uuid)
{
    auto parse = [](const std::string &uuid_e) {
        const std::string odl = "%populate { object '" + g_network + "' { parameter '" +
                                g_uuid_e_param + "' = \"" + uuid_e + "\"; } }";
        return amxo_parser_parse_string(beerocks::nbapi::Amxrt::getParser(), odl.c_str(),
                                        amxd_dm_get_root(dm()));
    };
    EXPECT_NE(0, parse("not-a-uuid"));
    EXPECT_EQ(get(g_network, g_uuid_e_param), "");
    EXPECT_EQ(0, parse(g_uuid_e));
    EXPECT_EQ(get(g_network, g_uuid_e_param), g_uuid_e);
}

TEST_F(MeshPbcTemplateTest, bss_config_found)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "300"));
    ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    EXPECT_EQ(m_config.template_path, mesh_pbc_path(1));
    EXPECT_EQ(m_config.listen_duration, 300);
    const std::array<uint8_t, 16> uuid_e = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                                            0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
    EXPECT_EQ(m_config.uuid_e, uuid_e);
    EXPECT_EQ(m_config.uuid_e_text, g_uuid_e);
    EXPECT_EQ(m_config.device_password_id, 4);
    EXPECT_EQ(m_config.device_password, "");
    EXPECT_FALSE(m_config.device_password_missing);

    ASSERT_TRUE(
        set(ssc_path(1), g_template_ref, g_tr181_root + ".X_PRPLWARE-COM_MeshPBCTemplate.1"));
    ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    EXPECT_EQ(m_config.template_path, mesh_pbc_path(1));

    ASSERT_TRUE(set(g_network, g_uuid_e_param, "ABCDEF01-2345-6789-ABCD-EF0123456789"));
    ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    const std::array<uint8_t, 16> upper = {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
                                           0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89};
    EXPECT_EQ(m_config.uuid_e, upper);
}

TEST_F(MeshPbcTemplateTest, bss_config_two_ssc_templates_share_one_template)
{
    ASSERT_TRUE(set(ssc_path(3), g_template_ref, mesh_pbc_path(1)));
    ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    EXPECT_EQ(m_config.template_path, mesh_pbc_path(1));
    ASSERT_EQ(get_config(ssc_ref(3)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    EXPECT_EQ(m_config.template_path, mesh_pbc_path(1));
}

TEST_F(MeshPbcTemplateTest, bss_config_primary_reference_does_not_change_the_result)
{
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}, {"ListenDuration", "30"}}));
    for (const auto &primary : {std::string(""), mesh_pbc_path(2), mesh_pbc_path(9)}) {
        ASSERT_TRUE(set(g_network, g_primary, primary));
        ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
        EXPECT_EQ(m_config.template_path, mesh_pbc_path(1)) << primary;
        EXPECT_EQ(get_config(ssc_ref(3)), eMeshPbcBssConfigResult::SKIP) << primary;
    }
}

TEST_F(MeshPbcTemplateTest, bss_config_device_password_rules)
{
    ASSERT_TRUE(set(mesh_pbc_path(1) + ".WPS", "DevicePassword", "12345670"));
    ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    EXPECT_EQ(m_config.device_password, "");
    EXPECT_FALSE(m_config.device_password_missing);

    ASSERT_TRUE(set(mesh_pbc_path(1) + ".WPS", "DevicePasswordID", "0"));
    ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    EXPECT_EQ(m_config.device_password_id, 0);
    EXPECT_EQ(m_config.device_password, "12345670");
    EXPECT_FALSE(m_config.device_password_missing);

    ASSERT_TRUE(set(mesh_pbc_path(1) + ".WPS", "DevicePassword", std::string(32, 'x')));
    ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    EXPECT_EQ(m_config.device_password, std::string(32, 'x'));

    ASSERT_TRUE(set(mesh_pbc_path(1) + ".WPS", "DevicePassword", ""));
    ASSERT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    EXPECT_EQ(m_config.device_password, "");
    EXPECT_TRUE(m_config.device_password_missing);

    ASSERT_TRUE(set(mesh_pbc_path(1) + ".WPS", "DevicePassword", std::string(33, 'x')));
    EXPECT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::INVALID);
}

TEST_F(MeshPbcTemplateTest, bss_config_skip)
{
    EXPECT_EQ(get_config(""), eMeshPbcBssConfigResult::SKIP);
    EXPECT_EQ(get_config(ssc_ref(3)), eMeshPbcBssConfigResult::SKIP);

    ASSERT_TRUE(set(mesh_pbc_path(1), "Enable", "false"));
    EXPECT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::SKIP);
    EXPECT_NE(m_reason.find("disabled"), std::string::npos) << m_reason;
    ASSERT_TRUE(set(mesh_pbc_path(1), "Enable", "true"));

    ASSERT_TRUE(set(g_network, "Enable", "false"));
    EXPECT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::SKIP);
    EXPECT_NE(m_reason.find("Templates.Network.Enable is false"), std::string::npos) << m_reason;
}

TEST_F(MeshPbcTemplateTest, bss_config_invalid)
{
    for (const auto &reference :
         {mesh_pbc_path(9), mesh_pbc_path(1) + ".", g_mesh_pbc_table + ".01", ssc_path(1)}) {
        ASSERT_TRUE(set(ssc_path(1), g_template_ref, reference));
        EXPECT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::INVALID) << reference;
        EXPECT_NE(m_reason.find("is not an X_PRPLWARE-COM_MeshPBCTemplate instance"),
                  std::string::npos)
            << m_reason;
    }
    ASSERT_TRUE(set(ssc_path(1), g_template_ref, mesh_pbc_path(1)));

    ASSERT_TRUE(add_template_without_id(2));
    ASSERT_TRUE(set(ssc_path(3), g_template_ref, mesh_pbc_path(2)));
    EXPECT_EQ(get_config(ssc_ref(3)), eMeshPbcBssConfigResult::INVALID);
    EXPECT_NE(m_reason.find("PBCTemplateID is empty"), std::string::npos) << m_reason;

    ASSERT_TRUE(set(mesh_pbc_path(1), "PushButtonMethod", "WPS,WPS"));
    EXPECT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
    ASSERT_TRUE(set(mesh_pbc_path(1), "PushButtonMethod", "DPP"));
    EXPECT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::INVALID);
    EXPECT_NE(m_reason.find("does not contain \"WPS\""), std::string::npos) << m_reason;
    ASSERT_TRUE(set(mesh_pbc_path(1), "PushButtonMethod", "DPP,WPS"));
    EXPECT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::FOUND) << m_reason;
}

TEST_F(MeshPbcTemplateNoUuidTest, bss_config_invalid_without_mesh_uuid_e)
{
    EXPECT_EQ(get_config(ssc_ref(1)), eMeshPbcBssConfigResult::INVALID);
    EXPECT_NE(m_reason.find(g_uuid_e_param + " is empty"), std::string::npos) << m_reason;
}

TEST_F(MeshPbcTemplateTest, session_template)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "20"));
    ASSERT_EQ(get_session_template(g_tr181_root + ".X_PRPLWARE-COM_MeshPBCTemplate.1"),
              eMeshPbcSessionTemplateResult::VALID)
        << m_reason;
    EXPECT_EQ(m_session_template.template_path, mesh_pbc_path(1));
    EXPECT_TRUE(m_session_template.wps);
    EXPECT_FALSE(m_session_template.dpp);
    EXPECT_EQ(m_session_template.listen_duration, 20);

    EXPECT_EQ(get_session_template(mesh_pbc_path(9)), eMeshPbcSessionTemplateResult::NOT_FOUND);
    EXPECT_EQ(get_session_template(""), eMeshPbcSessionTemplateResult::NOT_FOUND);

    ASSERT_TRUE(set(mesh_pbc_path(1), "PushButtonMethod", "WPS,WPS"));
    ASSERT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::VALID)
        << m_reason;
    EXPECT_TRUE(m_session_template.wps);
    EXPECT_FALSE(m_session_template.dpp);
    ASSERT_TRUE(set(mesh_pbc_path(1), "PushButtonMethod", "DPP,WPS"));
    ASSERT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::VALID);
    EXPECT_TRUE(m_session_template.dpp);

    ASSERT_TRUE(set(mesh_pbc_path(1), "Enable", "false"));
    EXPECT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::INVALID);
    ASSERT_TRUE(set(mesh_pbc_path(1), "Enable", "true"));

    ASSERT_TRUE(add_template_without_id(2));
    EXPECT_EQ(get_session_template(mesh_pbc_path(2)), eMeshPbcSessionTemplateResult::INVALID);

    ASSERT_TRUE(set(g_network, "Enable", "false"));
    EXPECT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::NOT_READY);
    EXPECT_EQ(m_session_template.template_path, mesh_pbc_path(1));

    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    EXPECT_EQ(prplmesh::controller::actions::mesh_pbc_get_primary_template_reference(),
              mesh_pbc_path(1));
}

TEST_F(MeshPbcTemplateTest, session_template_button_press_scope)
{
    using prplmesh::controller::actions::eMeshPbcButtonPressScope;
    ASSERT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::VALID);
    EXPECT_EQ(m_session_template.button_press_scope, eMeshPbcButtonPressScope::MESH_WIDE);

    ASSERT_TRUE(set(mesh_pbc_path(1), "ButtonPressScope", "RequestingDevice"));
    ASSERT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::VALID);
    EXPECT_EQ(m_session_template.button_press_scope, eMeshPbcButtonPressScope::REQUESTING_DEVICE);

    // A value that cannot be read does not change the result of the check.
    ASSERT_TRUE(delete_param(mesh_pbc_path(1), "ButtonPressScope"));
    ASSERT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::VALID)
        << m_reason;
    EXPECT_EQ(m_session_template.button_press_scope, eMeshPbcButtonPressScope::UNKNOWN);
}

TEST_F(MeshPbcTemplateNoUuidTest, session_template_without_mesh_uuid_e)
{
    EXPECT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::INVALID);
    ASSERT_TRUE(set(mesh_pbc_path(1), "PushButtonMethod", "DPP"));
    EXPECT_EQ(get_session_template(mesh_pbc_path(1)), eMeshPbcSessionTemplateResult::VALID)
        << m_reason;
}

TEST(MeshPbcUuidE, make_uuid_e_follows_the_pwhm_algorithm)
{
    using prplmesh::controller::actions::mesh_pbc_make_uuid_e;
    // Values of libswlc swl_uuid_fromMacAddress() with offset 0 and swl_uuid_toChar().
    EXPECT_EQ(mesh_pbc_make_uuid_e(tlvf::mac_from_string(g_controller_al_mac)),
              g_controller_al_mac_uuid_e);
    EXPECT_EQ(mesh_pbc_make_uuid_e(tlvf::mac_from_string("a4:91:b1:2c:3d:ef")),
              "61343931-6231-f263-f364-656661343931");
    EXPECT_EQ(mesh_pbc_make_uuid_e(tlvf::mac_from_string("46:55:66:77:00:00")),
              "34363535-3636-f737-f030-303034363535");
    EXPECT_EQ(mesh_pbc_make_uuid_e(beerocks::net::network_utils::ZERO_MAC), "");
}

TEST_F(MeshPbcTemplateNoUuidTest, init_uuid_e_writes_the_generated_value_once)
{
    EXPECT_TRUE(prplmesh::controller::actions::mesh_pbc_init_uuid_e());
    EXPECT_EQ(get(g_network, g_uuid_e_param), g_controller_al_mac_uuid_e);
    EXPECT_EQ(LogRecorder::count(el::Level::Info,
                                 g_uuid_e_param + " set to " + g_controller_al_mac_uuid_e),
              1U);

    EXPECT_TRUE(prplmesh::controller::actions::mesh_pbc_init_uuid_e());
    EXPECT_EQ(get(g_network, g_uuid_e_param), g_controller_al_mac_uuid_e);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, " set to "), 1U);
}

TEST_F(MeshPbcTemplateTest, init_uuid_e_keeps_an_existing_value)
{
    EXPECT_TRUE(prplmesh::controller::actions::mesh_pbc_init_uuid_e());
    EXPECT_EQ(get(g_network, g_uuid_e_param), g_uuid_e);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, " set to "), 0U);
}

class MeshPbcStatusTest : public MeshPbcTemplateTest {
protected:
    void SetUp() override
    {
        MeshPbcTemplateTest::SetUp();
        m_status_events.first = mesh_pbc_path(1) + ".Status.";
        m_bss_events.first    = g_bss_dm_path + ".X_PRPLWARE-COM_MeshPBC.";
        ASSERT_EQ(0, amxp_slot_connect(&dm()->sigmngr, "dm:object-changed", nullptr,
                                       count_object_changed, &m_status_events));
        ASSERT_EQ(0, amxp_slot_connect(&dm()->sigmngr, "dm:object-changed", nullptr,
                                       count_object_changed, &m_bss_events));
    }

    void TearDown() override
    {
        amxp_slot_disconnect_all(count_object_changed);
        MeshPbcTemplateTest::TearDown();
    }

    std::string status(const std::string &param) const
    {
        return get(mesh_pbc_path(1) + ".Status", param);
    }

    std::string bss_status(const std::string &param) const
    {
        return get(g_bss_dm_path + ".X_PRPLWARE-COM_MeshPBC", param);
    }

    /** Status object path prefix and its number of dm:object-changed events. */
    std::pair<std::string, unsigned> m_status_events{"", 0};
    /** BSS push-button object path prefix and its number of dm:object-changed events. */
    std::pair<std::string, unsigned> m_bss_events{"", 0};
};

TEST_F(MeshPbcStatusTest, template_status_started_writes_one_transaction)
{
    using prplmesh::controller::actions::mesh_pbc_set_template_status_ended;
    using prplmesh::controller::actions::mesh_pbc_set_template_status_started;
    ASSERT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Success", 0,
                                                   "11:22:33:44:55:66", "02:00:00:00:10:01"));
    deliver_events();
    m_status_events.second = 0;

    EXPECT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), "02:00:00:00:00:01-0101"));
    deliver_events();
    EXPECT_EQ(m_status_events.second, 1U);
    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_EQ(status("PBCSessionID"), "02:00:00:00:00:01-0101");
    EXPECT_EQ(status("Result"), "");
    EXPECT_EQ(status("FailureReason"), "0");
    EXPECT_EQ(status("EnrolleeMACAddress"), "");
    EXPECT_EQ(status("BSSID"), "");
    EXPECT_NE(status("TimeStamp"), g_default_time_stamp);
    EXPECT_EQ(get_type(mesh_pbc_path(1) + ".Status", "TimeStamp"), AMXC_VAR_ID_TIMESTAMP);
}

TEST_F(MeshPbcStatusTest, template_status_ended_sets_the_enrollee_only_for_success)
{
    using prplmesh::controller::actions::mesh_pbc_set_template_status_ended;
    using prplmesh::controller::actions::mesh_pbc_set_template_status_started;
    ASSERT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), "02:00:00:00:00:01-0101"));
    const std::string time_stamp = status("TimeStamp");
    deliver_events();
    m_status_events.second = 0;

    EXPECT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Success", 0,
                                                   "11:22:33:44:55:66", "02:00:00:00:10:01"));
    deliver_events();
    EXPECT_EQ(m_status_events.second, 1U);
    EXPECT_EQ(status("State"), "Idle");
    EXPECT_EQ(status("Result"), "Success");
    EXPECT_EQ(status("FailureReason"), "0");
    EXPECT_EQ(status("EnrolleeMACAddress"), "11:22:33:44:55:66");
    EXPECT_EQ(status("BSSID"), "02:00:00:00:10:01");
    EXPECT_EQ(status("PBCSessionID"), "02:00:00:00:00:01-0101");
    EXPECT_EQ(status("TimeStamp"), time_stamp);

    EXPECT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Error_Other", 12,
                                                   "11:22:33:44:55:66", "02:00:00:00:10:01"));
    EXPECT_EQ(status("Result"), "Error_Other");
    EXPECT_EQ(status("FailureReason"), "12");
    EXPECT_EQ(status("EnrolleeMACAddress"), "");
    EXPECT_EQ(status("BSSID"), "");
}

TEST_F(MeshPbcStatusTest, template_status_rejected_keeps_the_session_values)
{
    using prplmesh::controller::actions::mesh_pbc_set_template_status_rejected;
    using prplmesh::controller::actions::mesh_pbc_set_template_status_started;
    ASSERT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), "02:00:00:00:00:01-0101"));
    const std::string time_stamp = status("TimeStamp");
    deliver_events();
    m_status_events.second = 0;

    EXPECT_TRUE(mesh_pbc_set_template_status_rejected(mesh_pbc_path(1), "Error_Not_Ready", 0));
    deliver_events();
    EXPECT_EQ(m_status_events.second, 1U);
    EXPECT_EQ(status("Result"), "Error_Not_Ready");
    EXPECT_EQ(status("FailureReason"), "0");
    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_EQ(status("TimeStamp"), time_stamp);
    EXPECT_EQ(status("PBCSessionID"), "02:00:00:00:00:01-0101");
}

TEST_F(MeshPbcStatusTest, status_of_a_missing_object_is_not_written)
{
    using namespace prplmesh::controller::actions;
    EXPECT_FALSE(mesh_pbc_set_template_status_started(mesh_pbc_path(9), "02:00:00:00:00:01-0101"));
    EXPECT_FALSE(mesh_pbc_set_template_status_ended(mesh_pbc_path(9), "Success", 0, "", ""));
    EXPECT_FALSE(mesh_pbc_set_template_status_rejected(mesh_pbc_path(9), "Error_Other", 0));
    EXPECT_FALSE(mesh_pbc_set_bss_status(g_bss_dm_path, sMeshPbcBssStatus{}));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "is not found, not written"), 4U);
}

TEST_F(MeshPbcStatusTest, stats_of_a_new_template_are_0_and_read_only)
{
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    for (const auto &counter : g_stats_counters) {
        EXPECT_EQ(get(mesh_pbc_path(2) + ".Stats", counter), "0") << counter;
        EXPECT_EQ(get_type(mesh_pbc_path(2) + ".Stats", counter), AMXC_VAR_ID_UINT64) << counter;
        EXPECT_FALSE(set(mesh_pbc_path(2) + ".Stats", counter, "5")) << counter;
        EXPECT_EQ(get(mesh_pbc_path(2) + ".Stats", counter), "0") << counter;
    }
    EXPECT_EQ(stats(2), "");
}

TEST_F(MeshPbcStatusTest, stats_count_each_status_write)
{
    using namespace prplmesh::controller::actions;
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    EXPECT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), "02:00:00:00:00:01-0101"));
    EXPECT_EQ(stats(), "Sessions=1");
    EXPECT_TRUE(mesh_pbc_set_template_status_rejected(mesh_pbc_path(1), "Error_Not_Ready", 0));
    EXPECT_EQ(stats(), "Sessions=1,NotReady=1");
    EXPECT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Success", 0,
                                                   "11:22:33:44:55:66", "02:00:00:00:10:01"));
    EXPECT_EQ(stats(), "Sessions=1,Successes=1,NotReady=1");

    for (const char *result : {"Error_Timeout", "Error_Not_Ready", "Error_Other", "Canceled"}) {
        EXPECT_TRUE(
            mesh_pbc_set_template_status_started(mesh_pbc_path(1), "02:00:00:00:00:01-0102"));
        EXPECT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), result, 0, "", ""));
    }
    EXPECT_TRUE(mesh_pbc_set_template_status_rejected(mesh_pbc_path(1), "Error_Invalid_Input", 0));
    EXPECT_EQ(stats(), "Sessions=5,Successes=1,Timeouts=1,NotReady=2,InvalidInput=1,"
                       "OtherFailures=1,Canceled=1");
    EXPECT_EQ(stats(2), "");

    ASSERT_TRUE(set(mesh_pbc_path(1) + ".Status", "MaxHistoryEntries", "0"));
    deliver_events();
    m_status_events.second = 0;
    EXPECT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), "02:00:00:00:00:01-0103"));
    deliver_events();
    EXPECT_EQ(m_status_events.second, 1U);
    EXPECT_EQ(get(mesh_pbc_path(1) + ".Stats", "Sessions"), "6");
}

TEST_F(MeshPbcStatusTest, stats_of_a_deleted_template_are_not_written)
{
    using namespace prplmesh::controller::actions;
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    ASSERT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(2), "02:00:00:00:00:01-0101"));
    EXPECT_EQ(stats(2), "Sessions=1");
    ASSERT_TRUE(remove(g_mesh_pbc_table, 2));
    EXPECT_EQ(get(mesh_pbc_path(2) + ".Stats", "Sessions"), "<no object>");
    LogRecorder::lines.clear();

    EXPECT_FALSE(mesh_pbc_set_template_status_ended(mesh_pbc_path(2), "Error_Timeout", 0, "", ""));
    EXPECT_FALSE(mesh_pbc_set_template_status_rejected(mesh_pbc_path(2), "Error_Not_Ready", 0));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "is not found, not written"), 2U);
    EXPECT_EQ(LogRecorder::count(el::Level::Error), 0U);
    EXPECT_EQ(stats(), "");
}

TEST_F(MeshPbcStatusTest, bss_status_writes_one_transaction)
{
    ASSERT_TRUE(add(g_device_table, 1, {}, true));
    ASSERT_TRUE(add(g_device_table + ".1.Radio", 1, {}, true));
    ASSERT_TRUE(add(g_device_table + ".1.Radio.1.BSS", 1, {}, true));
    deliver_events();
    m_bss_events.second = 0;

    sMeshPbcBssStatus bss;
    bss.state          = "InProgress";
    bss.session_id     = "02:00:00:00:00:01-0101";
    bss.result         = "";
    bss.failure_reason = 0;
    EXPECT_TRUE(prplmesh::controller::actions::mesh_pbc_set_bss_status(g_bss_dm_path, bss));
    deliver_events();
    EXPECT_EQ(m_bss_events.second, 1U);
    EXPECT_EQ(bss_status("State"), "InProgress");
    EXPECT_EQ(bss_status("PBCSessionID"), "02:00:00:00:00:01-0101");
    EXPECT_EQ(bss_status("Result"), "");
    EXPECT_NE(bss_status("TimeStamp"), g_default_time_stamp);
    EXPECT_EQ(get_type(g_bss_dm_path + ".X_PRPLWARE-COM_MeshPBC", "TimeStamp"),
              AMXC_VAR_ID_TIMESTAMP);

    bss.state        = "Idle";
    bss.result       = "Success";
    bss.enrollee_mac = "11:22:33:44:55:66";
    EXPECT_TRUE(prplmesh::controller::actions::mesh_pbc_set_bss_status(g_bss_dm_path, bss));
    EXPECT_EQ(bss_status("State"), "Idle");
    EXPECT_EQ(bss_status("Result"), "Success");
    EXPECT_EQ(bss_status("EnrolleeMACAddress"), "11:22:33:44:55:66");

    bss.state = "Armed";
    EXPECT_FALSE(prplmesh::controller::actions::mesh_pbc_set_bss_status(g_bss_dm_path, bss));
    EXPECT_EQ(bss_status("State"), "Idle");
}

class MeshPbcHistoryTest : public MeshPbcStatusTest {
protected:
    /** Starts the Controller session with this MID on the template, and ends it. */
    static void run_session(unsigned mid, const std::string &result = "Error_Timeout",
                            unsigned index = 1)
    {
        using namespace prplmesh::controller::actions;
        ASSERT_TRUE(
            mesh_pbc_set_template_status_started(mesh_pbc_path(index), session_id_text(mid)));
        ASSERT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(index), result, 0, "", ""));
    }

    /** Writes values of an object as the Controller does: also read-only parameters. */
    static bool set_read_only(const std::string &object, const params_t &params)
    {
        amxd_trans_t trans;
        amxd_trans_init(&trans);
        amxd_trans_set_attr(&trans, amxd_tattr_change_ro, true);
        amxd_trans_select_pathf(&trans, "%s.", object.c_str());
        for (const auto &param : params) {
            amxd_trans_set_value(cstring_t, &trans, param.first.c_str(), param.second.c_str());
        }
        auto status = amxd_trans_apply(&trans, dm());
        amxd_trans_clean(&trans);
        return status == amxd_status_ok;
    }

    /** @return The value of Status.HistoryNumberOfEntries of the template. */
    static std::string history_count(unsigned index = 1)
    {
        return get(mesh_pbc_path(index) + ".Status", "HistoryNumberOfEntries");
    }

    /** @return The path of a new empty file with a unique name, or "" if it was not created. */
    static std::string make_temp_file()
    {
        char name[]  = "/tmp/mesh_pbc_history_test_XXXXXX.odl";
        const int fd = mkstemps(name, 4);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return "";
        }
        close(fd);
        return name;
    }

    /** @return The MeshPBCTemplate table as the Controller saves it in its store. */
    static std::string save_templates()
    {
        const std::string file = make_temp_file();
        amxd_object_t *table   = amxd_dm_findf(dm(), "%s.", g_mesh_pbc_table.c_str());
        EXPECT_TRUE(table);
        EXPECT_EQ(amxo_parser_save_object(beerocks::nbapi::Amxrt::getParser(), file.c_str(), table,
                                          false),
                  0);
        std::ifstream stream(file);
        std::stringstream content;
        content << stream.rdbuf();
        std::remove(file.c_str());
        return content.str();
    }

    /**
     * @brief Deletes the template 1 and loads a store, as at a Controller start.
     *
     * @return true if the store loaded.
     */
    static bool load_templates(const std::string &store)
    {
        const std::string file = make_temp_file();
        {
            std::ofstream stream(file);
            stream << store;
        }
        EXPECT_TRUE(remove(g_mesh_pbc_table, 1));
        const int status = amxo_parser_parse_file(beerocks::nbapi::Amxrt::getParser(), file.c_str(),
                                                  amxd_dm_get_root(dm()));
        std::remove(file.c_str());
        return status == 0;
    }

    /** @return The IDs of the Controller sessions with these MIDs, separated by ",". */
    static std::string session_ids(const std::vector<unsigned> &mids)
    {
        std::string text;
        for (const auto mid : mids) {
            text += (text.empty() ? "" : ",") + session_id_text(mid);
        }
        return text;
    }
};

TEST_F(MeshPbcHistoryTest, defaults_and_access)
{
    const std::string status_path = mesh_pbc_path(1) + ".Status";
    EXPECT_EQ(get(status_path, "MaxHistoryEntries"), "10");
    EXPECT_EQ(get_type(status_path, "MaxHistoryEntries"), AMXC_VAR_ID_UINT32);
    EXPECT_EQ(history_count(), "0");
    EXPECT_EQ(history(), "");
    check_values(status_path, "MaxHistoryEntries", {"0", "1", "300", "4294967295", "10"}, {"ten"});
    EXPECT_FALSE(set(status_path, "HistoryNumberOfEntries", "3"));

    run_session(1);
    run_session(2);
    ASSERT_EQ(history_count(), "1");
    const std::string entry = status_path + ".History.1";
    EXPECT_FALSE(set(entry, "Result", "Success"));
    EXPECT_EQ(get(entry, "Result"), "Error_Timeout");
    EXPECT_FALSE(add(status_path + ".History", 0, {{"PBCSessionID", "x"}}));
    EXPECT_FALSE(remove(status_path + ".History", 1));
    EXPECT_EQ(history(), session_id_text(1));
    for (const auto &param :
         {"PBCSessionID", "Result", "FailureReason", "EnrolleeMACAddress", "BSSID", "TimeStamp"}) {
        EXPECT_EQ(amxd_param_is_attr_set(
                      amxd_object_get_param_def(amxd_dm_findf(dm(), "%s.", entry.c_str()), param),
                      amxd_pattr_persistent),
                  false)
            << param;
    }
}

TEST_F(MeshPbcHistoryTest, first_session_moves_nothing)
{
    using namespace prplmesh::controller::actions;
    EXPECT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), session_id_text(1)));
    EXPECT_EQ(history_count(), "0");
    EXPECT_EQ(status("PBCSessionID"), session_id_text(1));
}

TEST_F(MeshPbcHistoryTest, session_creation_moves_the_previous_session)
{
    using namespace prplmesh::controller::actions;
    ASSERT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), session_id_text(1)));
    const std::string time_stamp = status("TimeStamp");
    ASSERT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Success", 0,
                                                   "11:22:33:44:55:66", "02:00:00:00:10:01"));
    EXPECT_EQ(history_count(), "0");

    EXPECT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), session_id_text(2)));
    EXPECT_EQ(history_count(), "1");
    EXPECT_EQ(history(), session_id_text(1));
    EXPECT_EQ(history(1, "Result"), "Success");
    EXPECT_EQ(history(1, "FailureReason"), "0");
    EXPECT_EQ(history(1, "EnrolleeMACAddress"), "11:22:33:44:55:66");
    EXPECT_EQ(history(1, "BSSID"), "02:00:00:00:10:01");
    EXPECT_EQ(history(1, "TimeStamp"), time_stamp);
    EXPECT_EQ(status("PBCSessionID"), session_id_text(2));
    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_EQ(status("Result"), "");
    EXPECT_EQ(status("EnrolleeMACAddress"), "");

    ASSERT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Error_Other", 12, "", ""));
    EXPECT_EQ(history_count(), "1");
    EXPECT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), session_id_text(3)));
    EXPECT_EQ(history(), session_ids({1, 2}));
    EXPECT_EQ(history(1, "Result"), "Success,Error_Other");
    EXPECT_EQ(history(1, "FailureReason"), "0,12");
    EXPECT_EQ(history(1, "EnrolleeMACAddress"), "11:22:33:44:55:66,");
    EXPECT_EQ(stats(), "Sessions=3,Successes=1,OtherFailures=1");
}

TEST_F(MeshPbcHistoryTest, rejection_moves_nothing_and_does_not_change_the_entry)
{
    using namespace prplmesh::controller::actions;
    run_session(1, "Error_Timeout");
    EXPECT_TRUE(mesh_pbc_set_template_status_rejected(mesh_pbc_path(1), "Error_Not_Ready", 0));
    EXPECT_TRUE(mesh_pbc_set_template_status_rejected(mesh_pbc_path(1), "Error_Invalid_Input", 0));
    EXPECT_EQ(history_count(), "0");
    EXPECT_EQ(status("Result"), "Error_Invalid_Input");
    EXPECT_EQ(status("PBCSessionID"), session_id_text(1));

    EXPECT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), session_id_text(2)));
    EXPECT_EQ(history(), session_id_text(1));
    EXPECT_EQ(history(1, "Result"), "Error_Timeout");
    EXPECT_EQ(history(1, "FailureReason"), "0");
}

TEST_F(MeshPbcHistoryTest, removed_template_does_not_keep_the_ended_session)
{
    using namespace prplmesh::controller::actions;
    ASSERT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), session_id_text(1)));
    ASSERT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Success", 0,
                                                   "11:22:33:44:55:66", "02:00:00:00:10:01"));
    ASSERT_TRUE(remove(g_mesh_pbc_table, 1));
    deliver_events();
    ASSERT_TRUE(add(g_mesh_pbc_table, 1, {{"PBCTemplateID", "t1"}}));
    ASSERT_TRUE(set_read_only(mesh_pbc_path(1) + ".Status",
                              {{"PBCSessionID", session_id_text(1)}, {"Result", "Error_Timeout"}}));
    EXPECT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), session_id_text(2)));
    EXPECT_EQ(history(), session_id_text(1));
    EXPECT_EQ(history(1, "Result"), "Error_Timeout");
    EXPECT_EQ(history(1, "EnrolleeMACAddress"), "");
    EXPECT_EQ(history(1, "BSSID"), "");
}

TEST_F(MeshPbcHistoryTest, session_end_moves_nothing)
{
    run_session(1);
    run_session(2, "Canceled");
    EXPECT_EQ(history(), session_id_text(1));
    EXPECT_EQ(status("Result"), "Canceled");
    EXPECT_EQ(status("PBCSessionID"), session_id_text(2));
}

TEST_F(MeshPbcHistoryTest, entries_added_first_are_deleted_above_the_limit)
{
    ASSERT_TRUE(set(mesh_pbc_path(1) + ".Status", "MaxHistoryEntries", "3"));
    for (unsigned mid = 1; mid <= 4; mid++) {
        run_session(mid);
    }
    EXPECT_EQ(history(), session_ids({1, 2, 3}));
    run_session(5);
    EXPECT_EQ(history(), session_ids({2, 3, 4}));
    EXPECT_EQ(history_count(), "3");
    run_session(6);
    EXPECT_EQ(history(), session_ids({3, 4, 5}));
    EXPECT_EQ(status("PBCSessionID"), session_id_text(6));
}

TEST_F(MeshPbcHistoryTest, deletion_follows_the_insertion_order_not_time_stamp_or_index)
{
    ASSERT_TRUE(set(mesh_pbc_path(1) + ".Status", "MaxHistoryEntries", "2"));
    const std::string table = mesh_pbc_path(1) + ".Status.History";
    ASSERT_TRUE(
        add(table, 50, {{"PBCSessionID", "a"}, {"TimeStamp", "2030-01-03T00:00:00Z"}}, true));
    ASSERT_TRUE(
        add(table, 10, {{"PBCSessionID", "b"}, {"TimeStamp", "2030-01-02T00:00:00Z"}}, true));
    run_session(1);
    ASSERT_TRUE(
        set_read_only(mesh_pbc_path(1) + ".Status", {{"TimeStamp", "2030-01-01T00:00:00Z"}}));
    EXPECT_EQ(history(), "a,b");

    run_session(2);
    EXPECT_EQ(history(), "b," + session_id_text(1));
    EXPECT_EQ(history(1, "TimeStamp"), "2030-01-02T00:00:00Z,2030-01-01T00:00:00Z");
    run_session(3);
    EXPECT_EQ(history(), session_ids({1, 2}));
}

TEST_F(MeshPbcHistoryTest, limit_change_deletes_at_once)
{
    const std::string status_path = mesh_pbc_path(1) + ".Status";
    for (unsigned mid = 1; mid <= 6; mid++) {
        run_session(mid);
    }
    ASSERT_EQ(history(), session_ids({1, 2, 3, 4, 5}));
    deliver_events();

    ASSERT_TRUE(set(status_path, "MaxHistoryEntries", "2"));
    deliver_events();
    EXPECT_EQ(history(), session_ids({4, 5}));
    EXPECT_EQ(history_count(), "2");

    ASSERT_TRUE(set(status_path, "MaxHistoryEntries", "10"));
    deliver_events();
    EXPECT_EQ(history(), session_ids({4, 5}));

    ASSERT_TRUE(set(status_path, "MaxHistoryEntries", "0"));
    deliver_events();
    EXPECT_EQ(history(), "");
    EXPECT_EQ(history_count(), "0");
    EXPECT_EQ(status("PBCSessionID"), session_id_text(6));

    run_session(7);
    run_session(8);
    EXPECT_EQ(history_count(), "0");
    EXPECT_EQ(status("PBCSessionID"), session_id_text(8));

    ASSERT_TRUE(set(status_path, "MaxHistoryEntries", "1"));
    deliver_events();
    run_session(9);
    EXPECT_EQ(history(), session_id_text(8));
    EXPECT_EQ(LogRecorder::count(el::Level::Error), 0U);
}

TEST_F(MeshPbcHistoryTest, limit_above_255)
{
    const std::string status_path = mesh_pbc_path(1) + ".Status";
    ASSERT_TRUE(set(status_path, "MaxHistoryEntries", "300"));
    for (unsigned mid = 1; mid <= 302; mid++) {
        run_session(mid);
    }
    EXPECT_EQ(history_count(), "300");
    amxd_object_t *table = history_table();
    ASSERT_TRUE(table);
    EXPECT_EQ(history().substr(0, session_id_text(2).size()), session_id_text(2));

    ASSERT_TRUE(set(status_path, "MaxHistoryEntries", "256"));
    deliver_events();
    EXPECT_EQ(history_count(), "256");
    EXPECT_EQ(history().substr(0, session_id_text(46).size()), session_id_text(46));
}

TEST_F(MeshPbcHistoryTest, reused_session_id_replaces_its_older_entry)
{
    run_session(1);
    run_session(2);
    run_session(1);
    EXPECT_EQ(history(), session_ids({1, 2}));
    run_session(3);
    EXPECT_EQ(history(), session_ids({2, 1}));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "is used again"), 1U);
    EXPECT_EQ(status("PBCSessionID"), session_id_text(3));
}

TEST_F(MeshPbcHistoryTest, history_is_per_template_and_deleted_with_it)
{
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    run_session(1, "Error_Timeout", 2);
    run_session(2, "Error_Timeout", 2);
    run_session(3);
    EXPECT_EQ(history(2), session_id_text(1));
    EXPECT_EQ(history(1), "");

    ASSERT_TRUE(remove(g_mesh_pbc_table, 2));
    EXPECT_EQ(history(2), "<no object>");
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    EXPECT_EQ(history(2), "");
    EXPECT_EQ(history_count(2), "0");
    EXPECT_EQ(get(mesh_pbc_path(2) + ".Status", "PBCSessionID"), "");
}

TEST_F(MeshPbcHistoryTest, saved_store_has_only_max_history_entries)
{
    const std::string status_path = mesh_pbc_path(1) + ".Status";
    ASSERT_TRUE(set(status_path, "MaxHistoryEntries", "3"));
    run_session(1, "Success");
    run_session(2);
    ASSERT_EQ(history_count(), "1");

    const std::string store = save_templates();
    EXPECT_NE(store.find("parameter 'MaxHistoryEntries' = 3"), std::string::npos) << store;
    EXPECT_EQ(store.find(session_id_text(1)), std::string::npos) << store;
    EXPECT_EQ(store.find(session_id_text(2)), std::string::npos) << store;
    for (const auto &param : {"'State'", "'Result'", "'PBCSessionID'", "'TimeStamp'",
                              "'HistoryNumberOfEntries'", "'Sessions'"}) {
        EXPECT_EQ(store.find(param), std::string::npos) << param << "\n" << store;
    }

    ASSERT_TRUE(load_templates(store));
    EXPECT_EQ(get(mesh_pbc_path(1), "PBCTemplateID"), "t1");
    EXPECT_EQ(get(status_path, "MaxHistoryEntries"), "3");
    EXPECT_EQ(history_count(), "0");
    EXPECT_EQ(status("State"), "Idle");
    EXPECT_EQ(status("Result"), "");
    EXPECT_EQ(status("PBCSessionID"), "");
}

TEST_F(MeshPbcHistoryTest, store_saved_without_status_loads_with_the_default)
{
    ASSERT_TRUE(set(mesh_pbc_path(1) + ".Status", "MaxHistoryEntries", "3"));
    std::string store = save_templates();
    // A store saved before Status was persistent has no Status object.
    const size_t begin = store.find("object 'Status' {");
    ASSERT_NE(begin, std::string::npos) << store;
    size_t end = store.find('{', begin);
    int depth  = 0;
    do {
        if (store[end] == '{') {
            depth++;
        } else if (store[end] == '}') {
            depth--;
        }
        end++;
    } while (depth > 0 && end < store.size());
    store.erase(store.rfind('\n', begin) + 1, end - store.rfind('\n', begin));
    ASSERT_EQ(store.find("Status"), std::string::npos) << store;

    ASSERT_TRUE(load_templates(store));
    EXPECT_EQ(get(mesh_pbc_path(1), "PBCTemplateID"), "t1");
    EXPECT_EQ(get(mesh_pbc_path(1) + ".Status", "MaxHistoryEntries"), "10");
    EXPECT_EQ(history_count(), "0");
}

TEST_F(MeshPbcHistoryTest, store_keeps_button_press_scope_and_loads_without_it)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ButtonPressScope", "RequestingDevice"));
    std::string store  = save_templates();
    const size_t begin = store.find("parameter 'ButtonPressScope' = \"RequestingDevice\"");
    ASSERT_NE(begin, std::string::npos) << store;
    ASSERT_TRUE(load_templates(store));
    EXPECT_EQ(get(mesh_pbc_path(1), "ButtonPressScope"), "RequestingDevice");

    // A store saved before ButtonPressScope was added has no such line.
    store.erase(store.rfind('\n', begin) + 1, store.find('\n', begin) - store.rfind('\n', begin));
    ASSERT_EQ(store.find("ButtonPressScope"), std::string::npos) << store;
    ASSERT_TRUE(load_templates(store));
    EXPECT_EQ(get(mesh_pbc_path(1), "PBCTemplateID"), "t1");
    EXPECT_EQ(get(mesh_pbc_path(1), "ButtonPressScope"), "MeshWide");
}

class MeshPbcTemplateRestageTest : public MeshPbcTemplateTest {
protected:
    void SetUp() override
    {
        MeshPbcTemplateTest::SetUp();
        m_master_conf.use_dataelements_vap_configs = true;
    }

    static void expect_applies(unsigned count)
    {
        deliver_events();
        EXPECT_EQ(g_stub_templates_apply_count, count);
        g_stub_templates_apply_count = 0;
    }
};

TEST_F(MeshPbcTemplateRestageTest, instance_add_and_delete)
{
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    expect_applies(1);
    ASSERT_TRUE(remove(g_mesh_pbc_table, 2));
    expect_applies(1);
}

TEST_F(MeshPbcTemplateRestageTest, parameter_change)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "60"));
    expect_applies(1);
    ASSERT_TRUE(set(mesh_pbc_path(1) + ".WPS", "DevicePasswordID", "0"));
    expect_applies(1);
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "30"));
    ASSERT_TRUE(set(mesh_pbc_path(1), "Enable", "false"));
    expect_applies(2);
    ASSERT_TRUE(set(mesh_pbc_path(1), {{"ListenDuration", "60"}, {"Enable", "true"}}));
    expect_applies(1);
}

TEST_F(MeshPbcTemplateRestageTest, no_apply_for_status_or_primary_reference)
{
    using namespace prplmesh::controller::actions;
    ASSERT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), "02:00:00:00:00:01-0101"));
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    expect_applies(0);

    ASSERT_TRUE(mesh_pbc_set_template_status_rejected(mesh_pbc_path(1), "Error_Not_Ready", 0));
    expect_applies(0);
    ASSERT_TRUE(mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Error_Timeout", 0, "", ""));
    expect_applies(0);
    EXPECT_EQ(stats(), "Sessions=1,Timeouts=1,NotReady=1");
}

TEST_F(MeshPbcTemplateRestageTest, no_apply_for_history_or_max_history_entries)
{
    using namespace prplmesh::controller::actions;
    for (const auto &session_id : {"02:00:00:00:00:01-0101", "02:00:00:00:00:01-0102"}) {
        ASSERT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), session_id));
        ASSERT_TRUE(
            mesh_pbc_set_template_status_ended(mesh_pbc_path(1), "Error_Timeout", 0, "", ""));
    }
    ASSERT_TRUE(mesh_pbc_set_template_status_started(mesh_pbc_path(1), "02:00:00:00:00:01-0103"));
    ASSERT_EQ(history(), "02:00:00:00:00:01-0101,02:00:00:00:00:01-0102");
    expect_applies(0);

    ASSERT_TRUE(set(mesh_pbc_path(1) + ".Status", "MaxHistoryEntries", "1"));
    expect_applies(0);
    EXPECT_EQ(history(), "02:00:00:00:00:01-0102");
    ASSERT_TRUE(set(mesh_pbc_path(1) + ".Status", "MaxHistoryEntries", "0"));
    expect_applies(0);
    EXPECT_EQ(history(), "");

    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "60"));
    expect_applies(1);
}

TEST_F(MeshPbcTemplateRestageTest, no_apply_without_data_elements_vap_configs)
{
    m_master_conf.use_dataelements_vap_configs = false;
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "60"));
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    ASSERT_TRUE(remove(g_mesh_pbc_table, 2));
    expect_applies(0);
}

TEST(MeshPbcSessionEngineText, result_and_session_id_text)
{
    EXPECT_STREQ(PBCOnboardingTask::result_to_string(PBCOnboardingTask::eResult::SUCCESS),
                 "Success");
    EXPECT_STREQ(PBCOnboardingTask::result_to_string(PBCOnboardingTask::eResult::ERROR_TIMEOUT),
                 "Error_Timeout");
    EXPECT_STREQ(PBCOnboardingTask::result_to_string(PBCOnboardingTask::eResult::ERROR_NOT_READY),
                 "Error_Not_Ready");
    EXPECT_STREQ(
        PBCOnboardingTask::result_to_string(PBCOnboardingTask::eResult::ERROR_INVALID_INPUT),
        "Error_Invalid_Input");
    EXPECT_STREQ(PBCOnboardingTask::result_to_string(PBCOnboardingTask::eResult::ERROR_OTHER),
                 "Error_Other");
    EXPECT_STREQ(PBCOnboardingTask::result_to_string(PBCOnboardingTask::eResult::CANCELED),
                 "Canceled");

    wfa_map::sPbcSessionId session_id{};
    session_id.al_mac = tlvf::mac_from_string("02:00:00:00:00:01");
    session_id.mid    = 0x0a0b;
    EXPECT_EQ(PBCOnboardingTask::session_id_to_string(session_id), "02:00:00:00:00:01-0a0b");
}

TEST_F(MeshPbcSessionEngineTest, start_rejects_a_template_that_is_not_valid)
{
    expect_rejected(start(mesh_pbc_path(9)), PBCOnboardingTask::eResult::ERROR_INVALID_INPUT);
    EXPECT_EQ(template_result(), "");

    ASSERT_TRUE(set(mesh_pbc_path(1), "Enable", "false"));
    expect_rejected(start(mesh_pbc_path(1)), PBCOnboardingTask::eResult::ERROR_INVALID_INPUT);
    EXPECT_EQ(template_result(), "Error_Invalid_Input");
}

TEST_F(MeshPbcSessionEngineTest, start_rejects_while_templates_are_disabled)
{
    ASSERT_TRUE(set(g_network, "Enable", "false"));
    expect_rejected(start(mesh_pbc_path(1)), PBCOnboardingTask::eResult::ERROR_NOT_READY);
    EXPECT_EQ(template_result(), "Error_Not_Ready");
}

TEST_F(MeshPbcSessionEngineTest, start_rejects_request_values_that_are_not_valid)
{
    PBCOnboardingTask::sStartRequest request;
    request.template_reference = mesh_pbc_path(1);
    request.origin             = PBCOnboardingTask::eOrigin::BUTTON;
    expect_rejected(m_task->start(request), PBCOnboardingTask::eResult::ERROR_INVALID_INPUT);

    expect_rejected(start(mesh_pbc_path(1), g_unknown_mac),
                    PBCOnboardingTask::eResult::ERROR_INVALID_INPUT);

    expect_rejected(start(mesh_pbc_path(1), {}, {g_bssid_2}),
                    PBCOnboardingTask::eResult::ERROR_INVALID_INPUT);

    expect_rejected(start(mesh_pbc_path(1), g_other_agent, {g_bssid_1}),
                    PBCOnboardingTask::eResult::ERROR_INVALID_INPUT);
    EXPECT_EQ(LogRecorder::count(el::Level::Error, "rejected (Error_Invalid_Input)"), 4U);
    EXPECT_EQ(template_result(), "Error_Invalid_Input");
}

TEST_F(MeshPbcSessionEngineTest, start_without_target_gives_error_not_ready)
{
    auto radio = m_db->get_radio(g_agent_mac, g_ruid);
    ASSERT_TRUE(radio);
    auto backhaul = m_db->add_bss(*radio, g_bssid_backhaul, "backhaul", 3);
    ASSERT_TRUE(backhaul);
    backhaul->backhaul = true;
    ASSERT_TRUE(m_db->add_bss(*radio, g_bssid_2, "other-ssid", 2));
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    son::wireless_utils::sBssInfoConf other_link{};
    other_link.bss_index             = 2;
    other_link.ssid                  = "other-ssid";
    other_link.mesh_pbc_template_ref = mesh_pbc_path(2);
    m_db->add_configured_bss_info(g_ruid, other_link);
    son::wireless_utils::sBssInfoConf unresolved_link{};
    unresolved_link.bss_index             = 4;
    unresolved_link.ssid                  = "no-such-ssid";
    unresolved_link.mesh_pbc_template_ref = mesh_pbc_path(1);
    m_db->add_configured_bss_info(g_ruid, unresolved_link);

    m_agent->mesh_wide_pbc_onboarding_support = false;
    expect_rejected(start(mesh_pbc_path(1)), PBCOnboardingTask::eResult::ERROR_NOT_READY);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "does not report Multi-AP Push-Button Trigger"),
              1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "its push-button link names " + mesh_pbc_path(2)),
              1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "backhaul BSS, no push-button link"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "ssid=no-such-ssid has no BSSID"), 1U);
    EXPECT_EQ(template_result(), "Error_Not_Ready");

    m_agent->mesh_wide_pbc_onboarding_support = true;
    m_agent->state                            = beerocks::STATE_DISCONNECTED;
    expect_rejected(start(mesh_pbc_path(1)), PBCOnboardingTask::eResult::ERROR_NOT_READY);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "the Agent is not connected"), 1U);

    m_agent->state = beerocks::STATE_CONNECTED;
    expect_rejected(start(mesh_pbc_path(1), g_other_agent),
                    PBCOnboardingTask::eResult::ERROR_NOT_READY);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "not a BSS of the requested Agent"), 1U);

    m_agent->mesh_wide_pbc_onboarding_support = false;
    expect_rejected(start(mesh_pbc_path(2)), PBCOnboardingTask::eResult::ERROR_NOT_READY);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "its push-button link names " + mesh_pbc_path(1)),
              1U);
}

TEST_F(MeshPbcSessionEngineTest, request_starts_a_session_with_the_primary_template)
{
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_unknown_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "the sender is not a connected Agent"), 1U);

    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Error, "nothing armed"), 1U);
    EXPECT_EQ(template_result(), "");

    m_agent->mesh_wide_pbc_onboarding_support = false;
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "(origin button"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "rejected: Error_Not_Ready"), 1U);
    EXPECT_EQ(template_result(), "Error_Not_Ready");

    EXPECT_FALSE(receive(ieee1905_1::eMessageType::ACK_MESSAGE, g_agent_mac));
}

TEST_F(MeshPbcSessionEngineTest, requesting_device_request_checks)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ButtonPressScope", "RequestingDevice"));
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_unknown_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "the sender is not a connected Agent"), 1U);
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Error, "nothing armed"), 1U);
    EXPECT_EQ(template_result(), "");
    EXPECT_EQ(stats(), "");

    // The requester has no BSS of the template; the BSS of the other Agent is not armed.
    auto other = m_db->get_agent(g_other_agent);
    ASSERT_TRUE(other);
    other->state                            = beerocks::STATE_CONNECTED;
    other->mesh_wide_pbc_onboarding_support = true;
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    son::g_stub_sent_cmdus.clear();
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_other_agent));
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "ButtonPressScope is RequestingDevice"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "not a BSS of the requested Agent"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "rejected: Error_Not_Ready"), 1U);
    EXPECT_EQ(template_result(), "Error_Not_Ready");
    EXPECT_EQ(get(mesh_pbc_path(1) + ".Status", "PBCSessionID"), "");
    EXPECT_EQ(get(mesh_pbc_path(1) + ".Status", "State"), "Idle");
    EXPECT_EQ(stats(), "NotReady=1");
    EXPECT_TRUE(son::g_stub_sent_cmdus.empty());
    EXPECT_EQ(get(m_bss->dm_path + ".X_PRPLWARE-COM_MeshPBC", "State"), "Idle");
}

TEST_F(MeshPbcSessionEngineTest, request_with_a_button_press_scope_that_cannot_be_read_is_rejected)
{
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    ASSERT_TRUE(delete_param(mesh_pbc_path(1), "ButtonPressScope"));
    son::g_stub_sent_cmdus.clear();
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Error,
                                 ".ButtonPressScope is not MeshWide or RequestingDevice"),
              1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "rejected: Error_Invalid_Input"), 1U);
    EXPECT_EQ(template_result(), "Error_Invalid_Input");
    EXPECT_EQ(stats(), "InvalidInput=1");
    EXPECT_TRUE(son::g_stub_sent_cmdus.empty());

    // A session that a Controller call starts does not use the value.
    auto result = start(mesh_pbc_path(1));
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(stats(), "Sessions=1,InvalidInput=1");
}

TEST_F(MeshPbcSessionEngineTest, status_without_session_changes_nothing)
{
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_STATUS_MESSAGE, g_agent_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Error, "has no Status TLV"), 1U);

    EXPECT_TRUE(receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_STATUS_MESSAGE, g_agent_mac,
                        {std::make_tuple(g_bssid_2, uint8_t(eStatusResult::STARTED), 0),
                         std::make_tuple(g_bssid_1, uint8_t(7), 0),
                         std::make_tuple(g_bssid_1, uint8_t(eStatusResult::FAILURE_ABORTED), 12),
                         std::make_tuple(g_bssid_1, uint8_t(eStatusResult::SUCCESS), 0)}));
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "(stale)"), 4U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "FailureReason=12"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "it is not a BSS of"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "reserved Result value 0x07"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "does not change the BSS"), 2U);
    EXPECT_EQ(get(m_bss->dm_path + ".X_PRPLWARE-COM_MeshPBC", "State"), "Idle");
}

TEST_F(MeshPbcSessionEngineTest, terminate_without_session_fails)
{
    EXPECT_FALSE(m_task->terminate(mesh_pbc_path(1)));
    EXPECT_FALSE(m_task->terminate("02:00:00:00:00:01-0101", g_agent_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "no session of that template is active"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "it is not the active session"), 1U);
    m_task->handle_templates_network_disabled();
    m_task->execute();
    EXPECT_EQ(LogRecorder::count(el::Level::Warning), 2U);
}

TEST_F(MeshPbcSessionEngineTest, work_updates_the_marked_agents)
{
    m_db->mark_mesh_pbc_wps_pending(g_agent_mac);
    m_task->execute();
    EXPECT_EQ(get(m_agent->dm_path + ".WPS", "Status"), "Configured");
}

} // namespace
