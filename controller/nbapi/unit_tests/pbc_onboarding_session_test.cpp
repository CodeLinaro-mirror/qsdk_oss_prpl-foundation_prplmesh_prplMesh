/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_test_fixture.h"

namespace {

using namespace mesh_pbc_test;

// cppcheck-suppress syntaxError
TEST_F(MeshPbcSessionTest, start_sends_one_trigger_per_agent)
{
    auto result = start_session();
    ASSERT_TRUE(result.accepted);
    EXPECT_EQ(m_session_id.al_mac, g_controller);
    EXPECT_EQ(result.session_id, PBCOnboardingTask::session_id_to_string(m_session_id));
    EXPECT_EQ(
        result.session_id.compare(0, g_controller_al_mac.size() + 1, g_controller_al_mac + "-"), 0)
        << result.session_id;

    ASSERT_EQ(g_stub_sent_cmdus.size(), 2U);
    for (const auto &sent : g_stub_sent_cmdus) {
        EXPECT_EQ(sent.type, ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_TRIGGER_MESSAGE);
        EXPECT_TRUE(sent.trigger);
        EXPECT_TRUE(sent.start);
        EXPECT_TRUE(sent.wps);
        EXPECT_FALSE(sent.dpp);
        EXPECT_EQ(PBCOnboardingTask::session_id_to_string(sent.session_id), result.session_id);
    }
    ASSERT_EQ(triggers(g_agent_mac, true).size(), 1U);
    EXPECT_EQ(triggers(g_agent_mac, true)[0].bssids, std::vector<sMacAddr>{g_bssid_1});
    ASSERT_EQ(triggers(g_other_agent, true).size(), 1U);
    EXPECT_EQ(triggers(g_other_agent, true)[0].bssids.size(), 2U);
    EXPECT_EQ(bssid_set(triggers(g_other_agent, true)[0].bssids),
              bssid_set({g_bssid_3, g_bssid_4}));

    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_EQ(status("PBCSessionID"), result.session_id);
    for (const auto &bss : {m_bss, m_bss_3, m_bss_4}) {
        EXPECT_EQ(bss_param(*bss, "State"), "Requested") << bss->bssid;
        EXPECT_EQ(bss_param(*bss, "PBCSessionID"), result.session_id) << bss->bssid;
    }
    EXPECT_TRUE(m_completions.empty());

    const uint16_t first_mid = m_session_id.mid;
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    ASSERT_TRUE(start_session().accepted);
    EXPECT_EQ(m_session_id.al_mac, g_controller);
    EXPECT_EQ(m_session_id.mid, uint16_t(first_mid + 1));
}

TEST_F(MeshPbcSessionTest, button_session_id_is_the_request_al_mac_and_mid)
{
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    ASSERT_EQ(trigger_count(true), 2U);
    for (const auto &sent : g_stub_sent_cmdus) {
        EXPECT_EQ(sent.session_id.al_mac, g_agent_mac);
        EXPECT_EQ(sent.session_id.mid, 0x1234);
    }
    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_EQ(status("PBCSessionID"), "02:00:00:00:00:02-1234");
}

TEST_F(MeshPbcSessionTest, mesh_wide_button_session_arms_all_agents)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ButtonPressScope", "MeshWide"));
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_other_agent));
    ASSERT_EQ(trigger_count(true), 2U);
    EXPECT_EQ(triggers(g_agent_mac, true).size(), 1U);
    EXPECT_EQ(triggers(g_other_agent, true).size(), 1U);
    EXPECT_EQ(status("PBCSessionID"), "02:00:00:00:00:03-1234");
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "ButtonPressScope"), 0U);
}

TEST_F(MeshPbcSessionTest, requesting_device_button_session_arms_only_the_requester)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ButtonPressScope", "RequestingDevice"));
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_other_agent));
    ASSERT_EQ(g_stub_sent_cmdus.size(), 1U);
    const auto &sent = g_stub_sent_cmdus[0];
    EXPECT_TRUE(sent.trigger);
    EXPECT_TRUE(sent.start);
    EXPECT_EQ(sent.destination, g_other_agent);
    EXPECT_EQ(bssid_set(sent.bssids), bssid_set({g_bssid_3, g_bssid_4}));
    EXPECT_EQ(sent.session_id.al_mac, g_other_agent);
    EXPECT_EQ(sent.session_id.mid, 0x1234);
    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_EQ(status("PBCSessionID"), "02:00:00:00:00:03-1234");
    EXPECT_EQ(bss_param(*m_bss, "State"), "Idle");
    EXPECT_EQ(bss_param(*m_bss_3, "State"), "Requested");
    EXPECT_EQ(bss_param(*m_bss_4, "State"), "Requested");
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "1 Agent(s), 2 BSS(s)"), 1U);
    EXPECT_EQ(stats(), "Sessions=1");

    // A change during the session does not change the armed BSSs.
    ASSERT_TRUE(set(mesh_pbc_path(1), "ButtonPressScope", "MeshWide"));
    advance(std::chrono::seconds(120));
    EXPECT_EQ(status("Result"), "Error_Timeout");
    EXPECT_EQ(trigger_count(true), 1U);
    expect_one_stop(g_other_agent, {g_bssid_3, g_bssid_4});
    EXPECT_TRUE(triggers(g_agent_mac, false).empty());
    EXPECT_EQ(stats(), "Sessions=1,Timeouts=1");

    // The next session reads the new value; the button session moves to History.
    g_stub_sent_cmdus.clear();
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    EXPECT_EQ(trigger_count(true), 2U);
    EXPECT_EQ(history(), "02:00:00:00:00:03-1234");
    EXPECT_EQ(history(1, "Result"), "Error_Timeout");
    EXPECT_EQ(stats(), "Sessions=2,Timeouts=1");
}

TEST_F(MeshPbcSessionTest, requesting_device_button_session_on_the_controller_agent)
{
    auto local = m_db->add_agent(g_controller);
    ASSERT_TRUE(local);
    local->state                            = beerocks::STATE_CONNECTED;
    local->mesh_wide_pbc_onboarding_support = true;
    const sMacAddr local_ruid               = tlvf::mac_from_string("02:00:00:00:40:00");
    const sMacAddr local_bssid              = tlvf::mac_from_string("02:00:00:00:40:01");
    ASSERT_TRUE(m_db->add_radio(local_ruid, g_controller));
    auto radio = m_db->get_radio(g_controller, local_ruid);
    ASSERT_TRUE(radio);
    auto bss = m_db->add_bss(*radio, local_bssid, g_link_ssid, 1);
    ASSERT_TRUE(bss);
    bss->fronthaul = true;
    son::wireless_utils::sBssInfoConf link{};
    link.bss_index             = 1;
    link.ssid                  = g_link_ssid;
    link.fronthaul             = true;
    link.mesh_pbc_template_ref = mesh_pbc_path(1);
    m_db->add_configured_bss_info(local_ruid, link);

    ASSERT_TRUE(set(mesh_pbc_path(1), "ButtonPressScope", "RequestingDevice"));
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_controller));
    ASSERT_EQ(g_stub_sent_cmdus.size(), 1U);
    EXPECT_EQ(g_stub_sent_cmdus[0].destination, g_controller);
    EXPECT_EQ(g_stub_sent_cmdus[0].bssids, std::vector<sMacAddr>{local_bssid});
    EXPECT_EQ(status("PBCSessionID"), g_controller_al_mac + "-1234");
    EXPECT_EQ(bss_param(*m_bss, "State"), "Idle");
    EXPECT_EQ(bss_param(*m_bss_3, "State"), "Idle");
}

TEST_F(MeshPbcSessionTest, second_start_is_rejected_and_keeps_the_status)
{
    ASSERT_TRUE(start_session().accepted);
    const auto session_id = status("PBCSessionID");
    const auto time_stamp = status("TimeStamp");

    auto second = start_session();
    EXPECT_FALSE(second.accepted);
    EXPECT_EQ(int(second.error), int(eSessionResult::ERROR_NOT_READY));
    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_EQ(status("PBCSessionID"), session_id);
    EXPECT_EQ(status("Result"), "");
    EXPECT_EQ(status("TimeStamp"), time_stamp);
    EXPECT_EQ(trigger_count(true), 2U);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "rejected (Error_Not_Ready): session"), 1U);
}

TEST_F(MeshPbcSessionTest, started_moves_the_bss_to_in_progress_once)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_agent_mac, g_bssid_1, eStatusResult::STARTED);
    EXPECT_EQ(bss_param(*m_bss, "State"), "InProgress");
    report(g_agent_mac, g_bssid_1, eStatusResult::STARTED);
    EXPECT_EQ(bss_param(*m_bss, "State"), "InProgress");
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "ignored: the BSS is InProgress"), 1U);
    EXPECT_TRUE(m_completions.empty());
}

TEST_F(MeshPbcSessionTest, second_final_report_is_ignored)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_other_agent, g_bssid_3, eStatusResult::NOT_STARTED, 14);
    EXPECT_EQ(bss_param(*m_bss_3, "State"), "Idle");
    EXPECT_EQ(bss_param(*m_bss_3, "Result"), "Error_Not_Ready");
    report(g_other_agent, g_bssid_3, eStatusResult::SUCCESS, 0, g_enrollee);
    EXPECT_EQ(bss_param(*m_bss_3, "Result"), "Error_Not_Ready");
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "already has its final report"), 1U);
    EXPECT_TRUE(m_completions.empty());
    EXPECT_EQ(status("State"), "InProgress");
}

TEST_F(MeshPbcSessionTest, report_of_another_session_or_agent_does_not_change_the_bss)
{
    ASSERT_TRUE(start_session().accepted);
    wfa_map::sPbcSessionId other_id = m_session_id;
    other_id.mid++;
    report(g_agent_mac, other_id, g_bssid_1, eStatusResult::STARTED);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "does not change the BSS"), 1U);
    report(g_agent_mac, g_bssid_3, eStatusResult::STARTED);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "it is not a BSS of"), 1U);
    report(g_other_agent, g_bssid_1, eStatusResult::SUCCESS, 0, g_enrollee);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "it is not a BSS of"), 2U);
    EXPECT_EQ(bss_param(*m_bss, "State"), "Requested");
    EXPECT_EQ(bss_param(*m_bss_3, "State"), "Requested");
    EXPECT_TRUE(m_completions.empty());
}

TEST_F(MeshPbcSessionTest, success_ends_the_session_and_stops_the_armed_bsses_per_agent)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_agent_mac, g_bssid_1, eStatusResult::STARTED);
    report(g_agent_mac, g_bssid_1, eStatusResult::SUCCESS, 0, g_enrollee);
    expect_completion(eSessionResult::SUCCESS, 0, g_enrollee, g_bssid_1);
    EXPECT_EQ(status("State"), "Idle");
    EXPECT_EQ(status("EnrolleeMACAddress"), "11:22:33:44:55:66");
    EXPECT_EQ(status("BSSID"), "02:00:00:00:10:01");
    EXPECT_EQ(bss_param(*m_bss, "Result"), "Success");
    EXPECT_EQ(bss_param(*m_bss, "EnrolleeMACAddress"), "11:22:33:44:55:66");
    EXPECT_TRUE(triggers(g_agent_mac, false).empty());
    expect_one_stop(g_other_agent, {g_bssid_3, g_bssid_4});

    report(g_other_agent, g_bssid_3, eStatusResult::FAILURE_ABORTED, 0);
    EXPECT_EQ(bss_param(*m_bss_3, "State"), "Idle");
    EXPECT_EQ(bss_param(*m_bss_3, "Result"), "Error_Other");
    report(g_other_agent, g_bssid_4, eStatusResult::SUCCESS, 0, g_enrollee);
    EXPECT_EQ(bss_param(*m_bss_4, "Result"), "Success");
    EXPECT_EQ(m_completions.size(), 1U);
}

TEST_F(MeshPbcSessionTest, success_stops_only_the_bsses_still_armed)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_other_agent, g_bssid_4, eStatusResult::NOT_STARTED, 14);
    report(g_other_agent, g_bssid_3, eStatusResult::SUCCESS, 0, g_enrollee);
    expect_completion(eSessionResult::SUCCESS, 0, g_enrollee, g_bssid_3);
    expect_one_stop(g_agent_mac, {g_bssid_1});
    EXPECT_TRUE(triggers(g_other_agent, false).empty());
}

TEST_F(MeshPbcSessionTest, overlap_ends_the_session_at_once)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_other_agent, g_bssid_3, eStatusResult::FAILURE_ABORTED, 12);
    expect_completion(eSessionResult::ERROR_OTHER, 12);
    EXPECT_EQ(status("FailureReason"), "12");
    EXPECT_EQ(bss_param(*m_bss_3, "Result"), "Error_Other");
    expect_one_stop(g_agent_mac, {g_bssid_1});
    expect_one_stop(g_other_agent, {g_bssid_4});
}

TEST_F(MeshPbcSessionTest, all_not_started_gives_error_not_ready)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_agent_mac, g_bssid_1, eStatusResult::NOT_STARTED, 14);
    report(g_other_agent, g_bssid_3, eStatusResult::NOT_STARTED, 14);
    EXPECT_TRUE(m_completions.empty());
    report(g_other_agent, g_bssid_4, eStatusResult::NOT_STARTED, 14);
    expect_completion(eSessionResult::ERROR_NOT_READY, 14);
    EXPECT_EQ(trigger_count(false), 0U);
}

TEST_F(MeshPbcSessionTest, all_failed_gives_error_other_with_the_last_failure_reason)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 3);
    report(g_other_agent, g_bssid_3, eStatusResult::NOT_STARTED, 14);
    report(g_other_agent, g_bssid_4, eStatusResult::FAILURE_ABORTED, 0);
    expect_completion(eSessionResult::ERROR_OTHER, 14);
    EXPECT_EQ(bss_param(*m_bss, "Result"), "Error_Other");
    EXPECT_EQ(bss_param(*m_bss_4, "Result"), "Error_Timeout");
}

TEST_F(MeshPbcSessionTest, all_aborted_without_failure_reason_gives_error_timeout)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    report(g_other_agent, g_bssid_3, eStatusResult::FAILURE_ABORTED, 0);
    report(g_other_agent, g_bssid_4, eStatusResult::FAILURE_ABORTED, 0);
    expect_completion(eSessionResult::ERROR_TIMEOUT, 0);
    EXPECT_EQ(trigger_count(false), 0U);
}

TEST_F(MeshPbcSessionTest, session_timer_ends_the_session_with_error_timeout)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "10"));
    ASSERT_TRUE(start_session().accepted);
    report(g_agent_mac, g_bssid_1, eStatusResult::STARTED);

    advance(std::chrono::seconds(10) - std::chrono::milliseconds(1));
    EXPECT_TRUE(m_completions.empty());
    EXPECT_EQ(trigger_count(false), 0U);

    advance(std::chrono::milliseconds(1));
    expect_completion(eSessionResult::ERROR_TIMEOUT, 0);
    expect_one_stop(g_agent_mac, {g_bssid_1});
    expect_one_stop(g_other_agent, {g_bssid_3, g_bssid_4});

    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    EXPECT_EQ(bss_param(*m_bss, "State"), "Idle");
    EXPECT_EQ(bss_param(*m_bss, "Result"), "Error_Timeout");
}

TEST_F(MeshPbcSessionTest, stopped_bss_without_report_becomes_idle_after_10_s)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "10"));
    ASSERT_TRUE(start_session().accepted);
    advance(std::chrono::seconds(10));
    ASSERT_EQ(m_completions.size(), 1U);

    m_now += std::chrono::seconds(10) - std::chrono::milliseconds(1);
    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    EXPECT_EQ(bss_param(*m_bss, "Result"), "Error_Timeout");
    m_task->execute();
    EXPECT_EQ(bss_param(*m_bss_3, "State"), "Requested");
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "sent no final report"), 0U);

    advance(std::chrono::milliseconds(1));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "sent no final report"), 2U);
    for (const auto &bss : {m_bss_3, m_bss_4}) {
        EXPECT_EQ(bss_param(*bss, "State"), "Idle") << bss->bssid;
        EXPECT_EQ(bss_param(*bss, "Result"), "Error_Timeout") << bss->bssid;
    }
    advance(std::chrono::seconds(10));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "sent no final report"), 2U);
}

TEST_F(MeshPbcSessionTest, button_session_id_cannot_be_used_again_for_60_s)
{
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    ASSERT_EQ(status("PBCSessionID"), "02:00:00:00:00:02-1234");
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    g_stub_sent_cmdus.clear();

    m_now += std::chrono::seconds(60) - std::chrono::milliseconds(1);
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "was used in the last 60 s"), 1U);
    EXPECT_EQ(status("Result"), "Error_Not_Ready");
    EXPECT_EQ(trigger_count(true), 0U);

    m_now += std::chrono::milliseconds(1);
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    EXPECT_EQ(trigger_count(true), 2U);
    EXPECT_EQ(status("State"), "InProgress");
}

TEST_F(MeshPbcSessionTest, bss_armed_again_drops_its_stop_wait)
{
    ASSERT_TRUE(start_session().accepted);
    const auto first_id = m_session_id;
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    ASSERT_TRUE(start_session().accepted);
    EXPECT_EQ(
        LogRecorder::count(el::Level::Info, "armed again: the final report of session " +
                                                PBCOnboardingTask::session_id_to_string(first_id)),
        3U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "BSS 02:00:00:00:10:01 armed again"), 1U);

    report(g_agent_mac, first_id, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "does not change the BSS"), 1U);
    EXPECT_EQ(bss_param(*m_bss, "State"), "Requested");

    advance(std::chrono::seconds(10));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "sent no final report"), 0U);
    EXPECT_EQ(bss_param(*m_bss, "State"), "Requested");
}

TEST_F(MeshPbcSessionTest, template_terminate_stops_all_armed_bsses)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_other_agent, g_bssid_4, eStatusResult::NOT_STARTED, 14);
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    expect_completion(eSessionResult::CANCELED, 0);
    EXPECT_EQ(status("FailureReason"), "0");
    expect_one_stop(g_agent_mac, {g_bssid_1});
    expect_one_stop(g_other_agent, {g_bssid_3});

    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    report(g_other_agent, g_bssid_3, eStatusResult::FAILURE_ABORTED, 0);
    for (const auto &bss : {m_bss, m_bss_3}) {
        EXPECT_EQ(bss_param(*bss, "State"), "Idle") << bss->bssid;
        EXPECT_EQ(bss_param(*bss, "Result"), "Canceled") << bss->bssid;
        EXPECT_EQ(bss_param(*bss, "FailureReason"), "0") << bss->bssid;
    }
    EXPECT_EQ(bss_param(*m_bss_4, "Result"), "Error_Not_Ready");
}

TEST_F(MeshPbcSessionTest, terminate_stop_without_report_gives_canceled_after_10_s)
{
    ASSERT_TRUE(start_session().accepted);
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    advance(std::chrono::seconds(10) - std::chrono::milliseconds(1));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "sent no final report"), 0U);

    advance(std::chrono::milliseconds(1));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "s after State=STOP: set to Idle, Canceled"),
              3U);
    for (const auto &bss : {m_bss, m_bss_3, m_bss_4}) {
        EXPECT_EQ(bss_param(*bss, "State"), "Idle") << bss->bssid;
        EXPECT_EQ(bss_param(*bss, "Result"), "Canceled") << bss->bssid;
    }
}

TEST_F(MeshPbcSessionTest, other_reports_after_a_terminate_stop_keep_their_result)
{
    ASSERT_TRUE(start_session().accepted);
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 3);
    report(g_other_agent, g_bssid_3, eStatusResult::SUCCESS, 0, g_enrollee);
    report(g_other_agent, g_bssid_4, eStatusResult::NOT_STARTED, 14);
    EXPECT_EQ(bss_param(*m_bss, "Result"), "Error_Other");
    EXPECT_EQ(bss_param(*m_bss, "FailureReason"), "3");
    EXPECT_EQ(bss_param(*m_bss_3, "Result"), "Success");
    EXPECT_EQ(bss_param(*m_bss_4, "Result"), "Error_Not_Ready");
    expect_completion(eSessionResult::CANCELED, 0);
}

TEST_F(MeshPbcSessionTest, success_stop_gives_error_other_and_a_terminate_stop_canceled)
{
    ASSERT_TRUE(start_session().accepted);
    ASSERT_TRUE(
        m_task->terminate(PBCOnboardingTask::session_id_to_string(m_session_id), g_agent_mac));
    expect_one_stop(g_agent_mac, {g_bssid_1});
    report(g_other_agent, g_bssid_3, eStatusResult::SUCCESS, 0, g_enrollee);
    expect_completion(eSessionResult::SUCCESS, 0, g_enrollee, g_bssid_3);
    expect_one_stop(g_other_agent, {g_bssid_4});

    report(g_other_agent, g_bssid_4, eStatusResult::FAILURE_ABORTED, 0);
    EXPECT_EQ(bss_param(*m_bss_4, "Result"), "Error_Other");
    advance(std::chrono::seconds(10));
    EXPECT_EQ(bss_param(*m_bss, "State"), "Idle");
    EXPECT_EQ(bss_param(*m_bss, "Result"), "Canceled");
}

TEST_F(MeshPbcSessionTest, agent_terminate_stops_only_that_agent)
{
    ASSERT_TRUE(start_session().accepted);
    const auto session_id = PBCOnboardingTask::session_id_to_string(m_session_id);
    ASSERT_TRUE(m_task->terminate(session_id, g_other_agent));
    expect_one_stop(g_other_agent, {g_bssid_3, g_bssid_4});
    EXPECT_TRUE(triggers(g_agent_mac, false).empty());
    EXPECT_TRUE(m_completions.empty());
    EXPECT_EQ(bss_param(*m_bss, "State"), "Requested");
    EXPECT_EQ(status("State"), "InProgress");

    report(g_other_agent, g_bssid_3, eStatusResult::FAILURE_ABORTED, 0);
    EXPECT_EQ(bss_param(*m_bss_3, "Result"), "Canceled");
    EXPECT_TRUE(m_completions.empty());

    report(g_agent_mac, g_bssid_1, eStatusResult::NOT_STARTED, 14);
    expect_completion(eSessionResult::ERROR_OTHER, 14);
}

TEST_F(MeshPbcSessionTest, agent_terminate_of_the_last_armed_bsses_ends_the_session)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 3);
    EXPECT_TRUE(m_completions.empty());
    ASSERT_TRUE(
        m_task->terminate(PBCOnboardingTask::session_id_to_string(m_session_id), g_other_agent));
    expect_completion(eSessionResult::CANCELED, 0);
    expect_one_stop(g_other_agent, {g_bssid_3, g_bssid_4});
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "no BSS is Requested or InProgress"), 1U);
}

TEST_F(MeshPbcSessionTest, templates_disabled_ends_the_session)
{
    ASSERT_TRUE(start_session().accepted);
    m_task->handle_templates_network_disabled();
    expect_completion(eSessionResult::ERROR_OTHER, 0);
    expect_one_stop(g_agent_mac, {g_bssid_1});
    expect_one_stop(g_other_agent, {g_bssid_3, g_bssid_4});
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "Templates.Network.Enable changed to false"),
              1U);

    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    EXPECT_EQ(bss_param(*m_bss, "Result"), "Error_Other");
    EXPECT_EQ(bss_param(*m_bss, "FailureReason"), "0");
}

TEST_F(MeshPbcSessionTest, internal_failure_ends_the_session_with_error_other)
{
    ASSERT_TRUE(start_session().accepted);
    EXPECT_FALSE(m_task->end_with_internal_failure(mesh_pbc_path(2)));
    EXPECT_TRUE(m_completions.empty());

    ASSERT_TRUE(m_task->end_with_internal_failure(mesh_pbc_path(1)));
    expect_completion(eSessionResult::ERROR_OTHER, 0);
    EXPECT_EQ(status("Result"), "Error_Other");
    EXPECT_EQ(status("FailureReason"), "0");
    expect_one_stop(g_agent_mac, {g_bssid_1});
    expect_one_stop(g_other_agent, {g_bssid_3, g_bssid_4});

    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    EXPECT_EQ(bss_param(*m_bss, "Result"), "Error_Other");
    advance(std::chrono::seconds(10));
    EXPECT_EQ(bss_param(*m_bss_3, "Result"), "Error_Timeout");
}

TEST_F(MeshPbcSessionTest, trigger_not_sent_ends_the_bsses_of_that_agent)
{
    g_stub_refused_destination = g_other_agent;
    ASSERT_TRUE(start_session().accepted);
    EXPECT_EQ(
        LogRecorder::count(el::Level::Error, "was not sent, its 2 BSS(s) end with Error_Other"),
        1U);
    for (const auto &bss : {m_bss_3, m_bss_4}) {
        EXPECT_EQ(bss_param(*bss, "State"), "Idle") << bss->bssid;
        EXPECT_EQ(bss_param(*bss, "Result"), "Error_Other") << bss->bssid;
    }
    EXPECT_EQ(bss_param(*m_bss, "State"), "Requested");
    m_task->execute();
    EXPECT_TRUE(m_completions.empty());

    report(g_agent_mac, g_bssid_1, eStatusResult::SUCCESS, 0, g_enrollee);
    expect_completion(eSessionResult::SUCCESS, 0, g_enrollee, g_bssid_1);
    EXPECT_EQ(trigger_count(false), 0U);
}

TEST_F(MeshPbcSessionTest, session_without_any_trigger_sent_ends_at_the_next_run)
{
    g_stub_refused_destination = g_other_agent;
    ASSERT_TRUE(start_session(g_other_agent).accepted);
    EXPECT_TRUE(m_completions.empty());
    EXPECT_TRUE(g_stub_sent_cmdus.empty());
    m_task->execute();
    ASSERT_EQ(m_completions.size(), 1U);
    EXPECT_EQ(int(m_completions[0].status), int(eSessionResult::ERROR_OTHER));
    EXPECT_EQ(m_completions[0].failure_reason, 0);
    EXPECT_EQ(status("Result"), "Error_Other");
    m_task->execute();
    EXPECT_EQ(m_completions.size(), 1U);
}

TEST_F(MeshPbcSessionTest, bss_of_a_disconnected_agent_is_stopped_without_a_message)
{
    ASSERT_TRUE(start_session().accepted);
    m_agent_2->state = beerocks::STATE_DISCONNECTED;
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    expect_completion(eSessionResult::CANCELED, 0);
    expect_one_stop(g_agent_mac, {g_bssid_1});
    EXPECT_TRUE(triggers(g_other_agent, false).empty());
    for (const auto &bss : {m_bss_3, m_bss_4}) {
        EXPECT_EQ(bss_param(*bss, "State"), "Idle") << bss->bssid;
        EXPECT_EQ(bss_param(*bss, "Result"), "Canceled") << bss->bssid;
    }
    advance(std::chrono::seconds(10));
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "sent no final report"), 1U);
}

TEST_F(MeshPbcSessionTest, agent_wps_status_follows_the_session)
{
    ASSERT_TRUE(start_session().accepted);
    EXPECT_EQ(get(m_agent->dm_path + ".WPS", "Status"), "InProgress");
    EXPECT_EQ(get(m_agent_2->dm_path + ".WPS", "Status"), "InProgress");

    report(g_other_agent, g_bssid_3, eStatusResult::NOT_STARTED, 14);
    EXPECT_EQ(get(m_agent_2->dm_path + ".WPS", "Status"), "InProgress");
    report(g_other_agent, g_bssid_4, eStatusResult::NOT_STARTED, 14);
    report(g_agent_mac, g_bssid_1, eStatusResult::SUCCESS, 0, g_enrollee);
    ASSERT_EQ(m_completions.size(), 1U);
    m_task->execute();
    EXPECT_EQ(get(m_agent->dm_path + ".WPS", "Status"), "Configured");
    EXPECT_EQ(get(m_agent_2->dm_path + ".WPS", "Status"), "Error");

    ASSERT_TRUE(start_session().accepted);
    EXPECT_EQ(get(m_agent_2->dm_path + ".WPS", "Status"), "InProgress");
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    report(g_other_agent, g_bssid_3, eStatusResult::FAILURE_ABORTED, 0);
    report(g_other_agent, g_bssid_4, eStatusResult::FAILURE_ABORTED, 0);
    m_task->execute();
    EXPECT_EQ(get(m_agent_2->dm_path + ".WPS", "Status"), "Configured");
}

TEST_F(MeshPbcSessionTest, stats_count_a_success)
{
    ASSERT_TRUE(start_session().accepted);
    EXPECT_EQ(stats(), "Sessions=1");
    report(g_agent_mac, g_bssid_1, eStatusResult::SUCCESS, 0, g_enrollee);
    expect_completion(eSessionResult::SUCCESS, 0, g_enrollee, g_bssid_1);
    EXPECT_EQ(stats(), "Sessions=1,Successes=1");
}

TEST_F(MeshPbcSessionTest, stats_count_a_timeout)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "10"));
    ASSERT_TRUE(start_session().accepted);
    advance(std::chrono::seconds(10));
    expect_completion(eSessionResult::ERROR_TIMEOUT, 0);
    EXPECT_EQ(stats(), "Sessions=1,Timeouts=1");
}

TEST_F(MeshPbcSessionTest, stats_count_a_session_that_is_not_ready)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_agent_mac, g_bssid_1, eStatusResult::NOT_STARTED, 14);
    report(g_other_agent, g_bssid_3, eStatusResult::NOT_STARTED, 14);
    report(g_other_agent, g_bssid_4, eStatusResult::NOT_STARTED, 14);
    expect_completion(eSessionResult::ERROR_NOT_READY, 14);
    EXPECT_EQ(stats(), "Sessions=1,NotReady=1");
}

TEST_F(MeshPbcSessionTest, stats_count_an_other_failure)
{
    ASSERT_TRUE(start_session().accepted);
    report(g_other_agent, g_bssid_3, eStatusResult::FAILURE_ABORTED, 12);
    expect_completion(eSessionResult::ERROR_OTHER, 12);
    EXPECT_EQ(stats(), "Sessions=1,OtherFailures=1");
}

TEST_F(MeshPbcSessionTest, stats_count_a_canceled_session)
{
    ASSERT_TRUE(start_session().accepted);
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    expect_completion(eSessionResult::CANCELED, 0);
    EXPECT_EQ(stats(), "Sessions=1,Canceled=1");
    EXPECT_FALSE(m_task->terminate(mesh_pbc_path(1)));
    EXPECT_EQ(stats(), "Sessions=1,Canceled=1");
}

TEST_F(MeshPbcSessionTest, stats_do_not_count_a_request_during_a_session_of_the_same_template)
{
    ASSERT_TRUE(start_session().accepted);
    EXPECT_FALSE(start_session().accepted);
    EXPECT_FALSE(start_session(g_agent_mac).accepted);
    EXPECT_EQ(stats(), "Sessions=1");
}

TEST_F(MeshPbcSessionTest, stats_count_a_request_during_a_session_of_another_template)
{
    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    ASSERT_TRUE(start_session().accepted);
    expect_rejected(start(mesh_pbc_path(2)), eSessionResult::ERROR_NOT_READY);
    EXPECT_EQ(stats(2), "NotReady=1");
    EXPECT_EQ(stats(), "Sessions=1");
}

TEST_F(MeshPbcSessionTest, stats_count_a_rejection_without_a_session)
{
    expect_rejected(start(mesh_pbc_path(1), g_agent_mac, {g_bssid_3}),
                    eSessionResult::ERROR_INVALID_INPUT);
    EXPECT_EQ(stats(), "InvalidInput=1");
    expect_rejected(start(mesh_pbc_path(9)), eSessionResult::ERROR_INVALID_INPUT);
    ASSERT_TRUE(set(g_network, "Enable", "false"));
    expect_rejected(start(mesh_pbc_path(1)), eSessionResult::ERROR_NOT_READY);
    EXPECT_EQ(stats(), "NotReady=1,InvalidInput=1");
}

TEST_F(MeshPbcSessionTest, stats_of_a_template_deleted_during_its_session)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "10"));
    ASSERT_TRUE(start_session().accepted);
    EXPECT_EQ(stats(), "Sessions=1");
    ASSERT_TRUE(remove(g_mesh_pbc_table, 1));
    EXPECT_EQ(get(mesh_pbc_path(1) + ".Stats", "Sessions"), "<no object>");
    LogRecorder::lines.clear();
    advance(std::chrono::seconds(10));
    ASSERT_EQ(m_completions.size(), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, ".Status is not found, not written"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Error, ".Stats is not found"), 0U);
}

TEST_F(MeshPbcSessionTest, history_gets_the_previous_session_at_the_next_start)
{
    ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "10"));
    ASSERT_TRUE(start_session().accepted);
    const std::string first = status("PBCSessionID");
    EXPECT_EQ(history(), "");
    advance(std::chrono::seconds(10));
    expect_completion(eSessionResult::ERROR_TIMEOUT, 0);
    EXPECT_EQ(history(), "");

    expect_rejected(start(mesh_pbc_path(1), g_agent_mac, {g_bssid_3}),
                    eSessionResult::ERROR_INVALID_INPUT);
    EXPECT_EQ(status("Result"), "Error_Invalid_Input");
    EXPECT_EQ(history(), "");

    m_completions.clear();
    ASSERT_TRUE(start_session().accepted);
    const std::string second = status("PBCSessionID");
    EXPECT_NE(second, first);
    EXPECT_EQ(history(), first);
    EXPECT_EQ(history(1, "Result"), "Error_Timeout");
    EXPECT_EQ(history(1, "FailureReason"), "0");

    EXPECT_FALSE(start_session().accepted);
    EXPECT_EQ(history(), first);
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    EXPECT_EQ(history(), first);
    EXPECT_EQ(status("Result"), "Canceled");
    EXPECT_EQ(status("PBCSessionID"), second);
}

TEST_F(MeshPbcSessionTest, history_entry_of_a_success)
{
    ASSERT_TRUE(start_session().accepted);
    const std::string first = status("PBCSessionID");
    report(g_agent_mac, g_bssid_1, eStatusResult::SUCCESS, 0, g_enrollee);
    expect_completion(eSessionResult::SUCCESS, 0, g_enrollee, g_bssid_1);
    EXPECT_EQ(history(), "");

    ASSERT_TRUE(start_session().accepted);
    EXPECT_EQ(history(), first);
    EXPECT_EQ(history(1, "Result"), "Success");
    EXPECT_EQ(history(1, "EnrolleeMACAddress"), tlvf::mac_to_string(g_enrollee));
    EXPECT_EQ(history(1, "BSSID"), tlvf::mac_to_string(g_bssid_1));
}

} // namespace
