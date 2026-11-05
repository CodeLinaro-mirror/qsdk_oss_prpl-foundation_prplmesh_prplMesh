/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_test_fixture.h"

#include <array>

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
    ASSERT_TRUE(prplmesh::controller::actions::mesh_pbc_set_template_status_started(
        mesh_pbc_path(1), "02:00:00:00:00:01-0101"));
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    expect_applies(0);
}

TEST_F(MeshPbcTemplateRestageTest, no_apply_without_data_elements_vap_configs)
{
    m_master_conf.use_dataelements_vap_configs = false;
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "60"));
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    ASSERT_TRUE(remove(g_mesh_pbc_table, 2));
    expect_applies(0);
}

} // namespace
