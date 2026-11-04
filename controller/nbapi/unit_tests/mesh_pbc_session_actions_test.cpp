/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_session_actions.h"
#include "mesh_pbc_session_task_stub.h"
#include "mesh_pbc_test_fixture.h"

#include <amxd/amxd_function.h>

#include <algorithm>
#include <cctype>
#include <map>

namespace {

using namespace mesh_pbc_test;
using prplmesh::controller::actions::g_stub_pbc_onboarding_task;

using out_args_t = std::map<std::string, std::string>;

/** The reply of a deferred call, as the caller callback gets it. */
struct sReply {
    uint32_t status = amxd_status_ok;
    out_args_t args;
};

/** @return The keys and values of an htable variant, as text. */
out_args_t to_map(const amxc_var_t *args)
{
    out_args_t result;
    if (!args || amxc_var_type_of(args) != AMXC_VAR_ID_HTABLE) {
        return result;
    }
    amxc_var_for_each(arg, args)
    {
        char *value               = amxc_var_dyncast(cstring_t, arg);
        result[amxc_var_key(arg)] = value ? value : "";
        free(value);
    }
    return result;
}

class MeshPbcSessionActionsTest : public MeshPbcSessionTest {
protected:
    void SetUp() override
    {
        m_functions = prplmesh::controller::actions::get_mesh_pbc_session_func_list();
        MeshPbcSessionTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        g_stub_pbc_onboarding_task =
            std::shared_ptr<PBCOnboardingTask>(m_task.get(), [](PBCOnboardingTask *) {});
        ASSERT_TRUE(set(mesh_pbc_path(1), "ListenDuration", "10"));
        LogRecorder::lines.clear();
    }

    void TearDown() override
    {
        prplmesh::controller::actions::mesh_pbc_session_actions_cleanup();
        g_stub_pbc_onboarding_task.reset();
        MeshPbcSessionTest::TearDown();
    }

    /** Caller callback of a deferred call: records its status and out arguments. */
    static void record_reply(const amxc_var_t *const data, void *const priv)
    {
        sReply reply;
        reply.status = GET_UINT32(data, "status");
        reply.args   = to_map(GET_ARG(data, "args"));
        static_cast<MeshPbcSessionActionsTest *>(priv)->m_replies.push_back(reply);
    }

    /**
     * @brief Calls a method of a data model object, as a bus backend does.
     *
     * A deferred call gets record_reply() as its caller callback, and the PBC Session ID of
     * its last Trigger with START becomes the session of report().
     *
     * @param out The arguments after the call: the out arguments of a call that returns at once.
     * @return The amxd status of the call.
     */
    amxd_status_t call(const std::string &object_path, const std::string &method,
                       const params_t &args = {}, out_args_t *out = nullptr,
                       amxc_var_t *list_arg = nullptr)
    {
        amxd_object_t *object = amxd_dm_findf(dm(), "%s.", object_path.c_str());
        EXPECT_TRUE(object) << object_path;
        if (!object) {
            return amxd_status_object_not_found;
        }
        amxc_var_t in_args;
        amxc_var_init(&in_args);
        amxc_var_set_type(&in_args, AMXC_VAR_ID_HTABLE);
        for (const auto &arg : args) {
            amxc_var_add_key(cstring_t, &in_args, arg.first.c_str(), arg.second.c_str());
        }
        if (list_arg) {
            amxc_var_set_key(&in_args, "BSSIDList", list_arg, AMXC_VAR_FLAG_COPY);
        }
        amxc_var_t ret;
        amxc_var_init(&ret);
        amxd_status_t status = amxd_object_invoke_function(object, method.c_str(), &in_args, &ret);
        if (status == amxd_status_deferred) {
            EXPECT_EQ(amxd_function_set_deferred_cb(amxc_var_constcast(uint64_t, &ret),
                                                    record_reply, this),
                      amxd_status_ok);
            for (const auto &sent : g_stub_sent_cmdus) {
                if (sent.trigger && sent.start) {
                    m_session_id = sent.session_id;
                }
            }
        }
        if (out) {
            *out = to_map(&in_args);
        }
        amxc_var_clean(&ret);
        amxc_var_clean(&in_args);
        return status;
    }

    /** Checks the out arguments of a call that the methods rejected at once. */
    static void expect_rejected(amxd_status_t status, const out_args_t &out,
                                const std::string &result)
    {
        EXPECT_EQ(status, amxd_status_ok);
        auto value = [&](const std::string &name) {
            auto it = out.find(name);
            return it == out.end() ? std::string("<none>") : it->second;
        };
        EXPECT_EQ(value("Status"), result);
        EXPECT_EQ(value("FailureReason"), "0");
        EXPECT_EQ(value("PBCSessionID"), "<none>");
        EXPECT_EQ(value("EnrolleeMACAddress"), "<none>");
        EXPECT_EQ(value("BSSID"), "<none>");
    }

    /** Delivers the pending deferred calls and checks that exactly one reply arrived. */
    void expect_one_reply(const out_args_t &args)
    {
        deliver_events();
        ASSERT_EQ(m_replies.size(), 1U);
        EXPECT_EQ(m_replies[0].status, uint32_t(amxd_status_ok));
        EXPECT_EQ(m_replies[0].args, args);
    }

    /** @return The PBCSessionID of the template Status. */
    std::string session_id() const { return status("PBCSessionID"); }

    std::vector<sReply> m_replies;
};

// cppcheck-suppress syntaxError
TEST_F(MeshPbcSessionActionsTest, template_call_gets_the_success_reply_at_the_session_end)
{
    EXPECT_EQ(call(mesh_pbc_path(1), "InitiatePBC"), amxd_status_deferred);
    ASSERT_EQ(trigger_count(true), 2U);
    deliver_events();
    EXPECT_TRUE(m_replies.empty());
    EXPECT_EQ(LogRecorder::count(el::Level::Info, ": accepted, session " + session_id()), 1U);

    report(g_other_agent, g_bssid_3, eStatusResult::SUCCESS, 0, g_enrollee);
    expect_one_reply({{"Status", "Success"},
                      {"PBCSessionID", session_id()},
                      {"EnrolleeMACAddress", "11:22:33:44:55:66"},
                      {"BSSID", "02:00:00:00:2a:01"}});
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "reply for session " + session_id()), 1U);
}

TEST_F(MeshPbcSessionActionsTest, template_call_gets_the_timeout_reply)
{
    EXPECT_EQ(call(mesh_pbc_path(1), "InitiatePBC"), amxd_status_deferred);
    advance(std::chrono::seconds(10));
    expect_one_reply(
        {{"Status", "Error_Timeout"}, {"PBCSessionID", session_id()}, {"FailureReason", "0"}});
}

TEST_F(MeshPbcSessionActionsTest, agent_call_gets_the_overlap_reply)
{
    EXPECT_EQ(call(m_agent_2->dm_path, "X_PRPLWARE-COM_InitiatePBC", {{"PBCTemplateID", "t1"}}),
              amxd_status_deferred);
    report(g_other_agent, g_bssid_4, eStatusResult::FAILURE_ABORTED, 12);
    expect_one_reply(
        {{"Status", "Error_Other"}, {"PBCSessionID", session_id()}, {"FailureReason", "12"}});
}

TEST_F(MeshPbcSessionActionsTest, templates_disable_gives_the_reply)
{
    EXPECT_EQ(call(mesh_pbc_path(1), "InitiatePBC"), amxd_status_deferred);
    prplmesh::controller::actions::mesh_pbc_templates_network_disabled();
    expect_one_reply(
        {{"Status", "Error_Other"}, {"PBCSessionID", session_id()}, {"FailureReason", "0"}});
    expect_one_stop(g_agent_mac, {g_bssid_1});
}

TEST_F(MeshPbcSessionActionsTest, bssid_list_that_is_not_valid_is_rejected)
{
    out_args_t out;
    expect_rejected(call(mesh_pbc_path(1), "InitiatePBC", {{"BSSIDList", "zz:11"}}, &out), out,
                    "Error_Invalid_Input");
    EXPECT_EQ(status("Result"), "Error_Invalid_Input");
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "\"zz:11\" is not a MAC address"), 1U);
    EXPECT_TRUE(g_stub_sent_cmdus.empty());

    ASSERT_EQ(call(m_agent->dm_path, "X_PRPLWARE-COM_InitiatePBC", {{"PBCTemplateID", "t1"}}),
              amxd_status_deferred);
    expect_rejected(call(mesh_pbc_path(1), "InitiatePBC", {{"BSSIDList", "zz:11"}}, &out), out,
                    "Error_Invalid_Input");
    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_EQ(status("Result"), "");
    expect_rejected(call(m_agent_2->dm_path, "X_PRPLWARE-COM_InitiatePBC",
                         {{"PBCTemplateID", "t1"}, {"BSSIDList", "02:00:00:00:2a"}}, &out),
                    out, "Error_Invalid_Input");
    EXPECT_EQ(status("Result"), "");
}

TEST_F(MeshPbcSessionActionsTest, call_during_a_session_is_rejected)
{
    ASSERT_EQ(call(mesh_pbc_path(1), "InitiatePBC"), amxd_status_deferred);
    out_args_t out;
    expect_rejected(call(mesh_pbc_path(1), "InitiatePBC", {}, &out), out, "Error_Not_Ready");
    EXPECT_EQ(trigger_count(true), 2U);
    deliver_events();
    EXPECT_TRUE(m_replies.empty());
}

TEST_F(MeshPbcSessionActionsTest, disabled_template_or_templates_are_rejected)
{
    out_args_t out;
    ASSERT_TRUE(set(mesh_pbc_path(1), "Enable", "false"));
    expect_rejected(call(mesh_pbc_path(1), "InitiatePBC", {}, &out), out, "Error_Invalid_Input");
    ASSERT_TRUE(set(mesh_pbc_path(1), "Enable", "true"));

    ASSERT_TRUE(set(g_network, "Enable", "false"));
    expect_rejected(call(mesh_pbc_path(1), "InitiatePBC", {}, &out), out, "Error_Not_Ready");
    expect_rejected(
        call(m_agent_2->dm_path, "X_PRPLWARE-COM_InitiatePBC", {{"PBCTemplateID", "t1"}}, &out),
        out, "Error_Not_Ready");
    EXPECT_EQ(status("Result"), "Error_Not_Ready");
    EXPECT_TRUE(g_stub_sent_cmdus.empty());
}

TEST_F(MeshPbcSessionActionsTest, call_without_engine_is_rejected)
{
    g_stub_pbc_onboarding_task.reset();
    out_args_t out;
    expect_rejected(call(mesh_pbc_path(1), "InitiatePBC", {}, &out), out, "Error_Not_Ready");
    EXPECT_EQ(status("Result"), "");
    expect_rejected(
        call(m_agent_2->dm_path, "X_PRPLWARE-COM_InitiatePBC", {{"PBCTemplateID", "t1"}}, &out),
        out, "Error_Not_Ready");
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "session engine does not run"), 2U);
    EXPECT_TRUE(g_stub_sent_cmdus.empty());
}

TEST_F(MeshPbcSessionActionsTest, bssid_list_string_gives_each_bssid_once)
{
    ASSERT_EQ(call(mesh_pbc_path(1), "InitiatePBC",
                   {{"BSSIDList", " 02:00:00:00:2A:01 ,02:00:00:00:2a:01 "}}),
              amxd_status_deferred);
    ASSERT_EQ(g_stub_sent_cmdus.size(), 1U);
    EXPECT_EQ(g_stub_sent_cmdus[0].destination, g_other_agent);
    EXPECT_EQ(g_stub_sent_cmdus[0].bssids, std::vector<sMacAddr>{g_bssid_3});
    EXPECT_EQ(
        LogRecorder::count(el::Level::Info, "(BSSIDList \"02:00:00:00:2A:01,02:00:00:00:2a:01\")"),
        1U);
}

TEST_F(MeshPbcSessionActionsTest, bssid_list_variant_gives_each_bssid_once)
{
    amxc_var_t list;
    amxc_var_init(&list);
    amxc_var_set_type(&list, AMXC_VAR_ID_LIST);
    amxc_var_add(cstring_t, &list, "02:00:00:00:10:01");
    amxc_var_add(cstring_t, &list, "02:00:00:00:3B:01");
    amxc_var_add(cstring_t, &list, "02:00:00:00:10:01");
    EXPECT_EQ(call(mesh_pbc_path(1), "InitiatePBC", {}, nullptr, &list), amxd_status_deferred);
    amxc_var_clean(&list);
    ASSERT_EQ(trigger_count(true), 2U);
    EXPECT_EQ(triggers(g_agent_mac, true)[0].bssids, std::vector<sMacAddr>{g_bssid_1});
    EXPECT_EQ(triggers(g_other_agent, true)[0].bssids, std::vector<sMacAddr>{g_bssid_4});
}

TEST_F(MeshPbcSessionActionsTest, agent_call_arms_only_that_agent)
{
    ASSERT_EQ(call(m_agent_2->dm_path, "X_PRPLWARE-COM_InitiatePBC", {{"PBCTemplateID", "t1"}}),
              amxd_status_deferred);
    ASSERT_EQ(g_stub_sent_cmdus.size(), 1U);
    EXPECT_EQ(g_stub_sent_cmdus[0].destination, g_other_agent);
    EXPECT_EQ(bssid_set(g_stub_sent_cmdus[0].bssids), bssid_set({g_bssid_3, g_bssid_4}));
    EXPECT_EQ(bss_param(*m_bss, "State"), "Idle");
    ASSERT_TRUE(m_task->terminate(mesh_pbc_path(1)));
    deliver_events();
    ASSERT_EQ(m_replies.size(), 1U);

    g_stub_sent_cmdus.clear();
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    ASSERT_EQ(call(m_agent->dm_path, "X_PRPLWARE-COM_InitiatePBC"), amxd_status_deferred);
    ASSERT_EQ(g_stub_sent_cmdus.size(), 1U);
    EXPECT_EQ(g_stub_sent_cmdus[0].destination, g_agent_mac);
    EXPECT_EQ(g_stub_sent_cmdus[0].bssids, std::vector<sMacAddr>{g_bssid_1});
}

TEST_F(MeshPbcSessionActionsTest, agent_call_arguments_that_are_not_valid_are_rejected)
{
    out_args_t out;
    expect_rejected(
        call(m_agent_2->dm_path, "X_PRPLWARE-COM_InitiatePBC", {{"PBCTemplateID", "nope"}}, &out),
        out, "Error_Invalid_Input");
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "PBCTemplateID \"nope\" names no"), 1U);

    expect_rejected(call(m_agent_2->dm_path, "X_PRPLWARE-COM_InitiatePBC", {}, &out), out,
                    "Error_Invalid_Input");
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "PrimaryMeshPBCTemplateReference is empty"),
              1U);

    ASSERT_TRUE(add(g_device_table, 9, {{"ID", "not-a-mac"}}, true));
    expect_rejected(
        call(g_device_table + ".9", "X_PRPLWARE-COM_InitiatePBC", {{"PBCTemplateID", "t1"}}, &out),
        out, "Error_Invalid_Input");
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "the ID of the Device is not a MAC address"),
              1U);
    EXPECT_EQ(status("Result"), "");
    EXPECT_TRUE(g_stub_sent_cmdus.empty());
}

TEST_F(MeshPbcSessionActionsTest, agent_call_with_one_bssid_arms_only_that_bss)
{
    ASSERT_EQ(call(m_agent_2->dm_path, "X_PRPLWARE-COM_InitiatePBC",
                   {{"PBCTemplateID", "t1"}, {"BSSIDList", "02:00:00:00:3b:01"}}),
              amxd_status_deferred);
    ASSERT_EQ(g_stub_sent_cmdus.size(), 1U);
    EXPECT_EQ(g_stub_sent_cmdus[0].destination, g_other_agent);
    EXPECT_EQ(g_stub_sent_cmdus[0].bssids, std::vector<sMacAddr>{g_bssid_4});
    EXPECT_EQ(bss_param(*m_bss_3, "State"), "Idle");
    EXPECT_EQ(bss_param(*m_bss_4, "State"), "Requested");

    EXPECT_EQ(call(mesh_pbc_path(1), "TerminatePBC"), amxd_status_ok);
    expect_one_reply(
        {{"Status", "Error_Other"}, {"PBCSessionID", session_id()}, {"FailureReason", "0"}});
}

TEST_F(MeshPbcSessionActionsTest, template_terminate_fails_without_its_session)
{
    EXPECT_EQ(call(mesh_pbc_path(1), "TerminatePBC"), amxd_status_invalid_value);

    ASSERT_TRUE(add(g_mesh_pbc_table, 2, {{"PBCTemplateID", "t2"}}));
    ASSERT_EQ(call(mesh_pbc_path(1), "InitiatePBC"), amxd_status_deferred);
    EXPECT_EQ(call(mesh_pbc_path(2), "TerminatePBC"), amxd_status_invalid_value);

    g_stub_pbc_onboarding_task.reset();
    EXPECT_EQ(call(mesh_pbc_path(1), "TerminatePBC"), amxd_status_invalid_value);
    EXPECT_EQ(trigger_count(false), 0U);
    EXPECT_EQ(
        LogRecorder::count(el::Level::Info, "TerminatePBC() on " + mesh_pbc_path(2) + ": failed"),
        1U);
}

TEST_F(MeshPbcSessionActionsTest, agent_terminate_stops_only_that_agent)
{
    ASSERT_EQ(call(mesh_pbc_path(1), "InitiatePBC"), amxd_status_deferred);
    std::string upper = session_id();
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    EXPECT_EQ(call(m_agent_2->dm_path, "X_PRPLWARE-COM_TerminatePBC",
                   {{"PBCSessionID", "02:00:00:00:00:01-ffff"}}),
              amxd_status_invalid_value);
    EXPECT_EQ(trigger_count(false), 0U);

    EXPECT_EQ(call(m_agent_2->dm_path, "X_PRPLWARE-COM_TerminatePBC", {{"PBCSessionID", upper}}),
              amxd_status_ok);
    expect_one_stop(g_other_agent, {g_bssid_3, g_bssid_4});
    EXPECT_TRUE(triggers(g_agent_mac, false).empty());
    deliver_events();
    EXPECT_TRUE(m_replies.empty());

    report(g_agent_mac, g_bssid_1, eStatusResult::FAILURE_ABORTED, 0);
    expect_one_reply(
        {{"Status", "Error_Other"}, {"PBCSessionID", session_id()}, {"FailureReason", "0"}});
}

TEST_F(MeshPbcSessionActionsTest, cleanup_cancels_the_open_call)
{
    ASSERT_EQ(call(mesh_pbc_path(1), "InitiatePBC"), amxd_status_deferred);
    prplmesh::controller::actions::mesh_pbc_session_actions_cleanup();
    ASSERT_EQ(m_replies.size(), 1U);
    EXPECT_EQ(m_replies[0].status, uint32_t(amxd_status_unknown_error));
    EXPECT_EQ(status("State"), "InProgress");
    EXPECT_TRUE(m_completions.empty());

    advance(std::chrono::seconds(10));
    deliver_events();
    EXPECT_EQ(status("Result"), "Error_Timeout");
    EXPECT_EQ(m_replies.size(), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "reply for session"), 0U);
}

TEST_F(MeshPbcSessionActionsTest, button_session_gives_no_reply)
{
    ASSERT_TRUE(set(g_network, g_primary, mesh_pbc_path(1)));
    EXPECT_TRUE(
        receive(ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE, g_agent_mac));
    EXPECT_EQ(status("PBCSessionID"), "02:00:00:00:00:02-1234");
    EXPECT_EQ(call(mesh_pbc_path(1), "TerminatePBC"), amxd_status_ok);
    deliver_events();
    EXPECT_TRUE(m_replies.empty());
    EXPECT_EQ(status("Result"), "Error_Other");
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "reply for session"), 0U);
}

} // namespace
