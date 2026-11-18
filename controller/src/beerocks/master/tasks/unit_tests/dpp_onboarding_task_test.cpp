/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

// VAL-04 (STEP-06): Controller desired CCE state. Signalling only; the over-the-air state is
// covered by VAL-05. No Controller context is set, so indications are not sent; the test checks
// the desired state that drives ENABLE and the AP Capability Report re-send.

#include "../dpp_onboarding_task.h"
#include "db.h"

#include <bcl/beerocks_event_loop_impl.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

class TestableDppOnboardingTask : public son::dpp_onboarding_task {
public:
    using son::dpp_onboarding_task::dpp_onboarding_task;
    using son::dpp_onboarding_task::handle_event;
    using son::dpp_onboarding_task::work;

    void expire_event_wait()
    {
        set_events_timeout(0);
        wait_for(0);
    }

    void handle_events_timeout(std::multiset<int> pending_events) override
    {
        timed_out_events = pending_events;
        ++timeout_count;
        son::dpp_onboarding_task::handle_events_timeout(std::move(pending_events));
    }

    std::multiset<int> timed_out_events;
    int timeout_count = 0;
};

class DppOnboardingTaskTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_log_conf.files_enabled  = "false";
        m_log_conf.stdout_enabled = "true";
        m_log_conf.global_levels  = "all";
        m_log_conf.syslog_levels  = "off";
        m_logger = std::make_unique<beerocks::logging>("dpp_onboarding_task_test", m_log_conf);

        m_amxrt                  = std::make_shared<beerocks::nbapi::Amxrt>();
        char app_name[]          = "dpp_onboarding_task_test";
        std::vector<char *> argv = {app_name, nullptr};
        ASSERT_EQ(0, m_amxrt->Initialize(argv.size() - 1, argv.data(), nullptr));

        m_event_loop = std::make_shared<beerocks::EventLoopImpl>();
        ASSERT_TRUE(m_event_loop->register_handlers(amxp_signal_fd(), {}));

        m_ambiorix = std::make_shared<beerocks::nbapi::AmbiorixImpl>(
            m_event_loop, std::vector<beerocks::nbapi::sActionsCallback>{},
            std::vector<beerocks::nbapi::sEvents>{}, std::vector<beerocks::nbapi::sFunctions>{});
        ASSERT_TRUE(m_ambiorix->load_datamodel(IEEE1905_ODL_PATH));

        m_database = std::make_unique<son::db>(
            m_master_conf, *m_logger, tlvf::mac_from_string("11:22:33:44:55:66"), m_ambiorix);
        m_task = std::make_unique<TestableDppOnboardingTask>(*m_database, nullptr);
    }

    void TearDown() override
    {
        m_task.reset();
        m_database.reset();
        m_ambiorix.reset();
        m_event_loop.reset();
        m_amxrt.reset();
    }

    void add_uri(const std::string &alias, const std::string &public_key)
    {
        son::db::sDppBootstrappingInfo info;
        info.alias      = alias;
        info.public_key = public_key;
        std::string error;
        ASSERT_TRUE(m_database->add_dpp_bootstrap_info(std::move(info), error)) << error;
    }

    // dpp_protocol_task marks the matched entry onboarded before it pushes CONF_RECEIVED.
    void onboard(const std::string &alias)
    {
        auto *info = m_database->get_dpp_bootstrap_info(alias);
        ASSERT_NE(nullptr, info);
        info->onboarded = true;
        m_task->handle_event(son::dpp_onboarding_task::CONF_RECEIVED, nullptr);
    }

    beerocks::config_file::SConfigLog m_log_conf{};
    std::unique_ptr<beerocks::logging> m_logger;
    std::shared_ptr<beerocks::nbapi::Amxrt> m_amxrt;
    std::shared_ptr<beerocks::EventLoopImpl> m_event_loop;
    std::shared_ptr<beerocks::nbapi::AmbiorixImpl> m_ambiorix;
    son::db::sDbMasterConfig m_master_conf{};
    std::unique_ptr<son::db> m_database;
    std::unique_ptr<TestableDppOnboardingTask> m_task;
};

// cppcheck-suppress syntaxError
TEST_F(DppOnboardingTaskTest, no_uri_cce_desired)
{
    EXPECT_FALSE(m_task->is_cce_desired());
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, uri_provisioned_cce_desired)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, session_failure_keeps_cce_desired)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    m_task->handle_event(son::dpp_onboarding_task::FAIL, nullptr);
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, session_timeout_keeps_cce_desired)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    m_task->handle_events_timeout({});
    EXPECT_TRUE(m_task->is_cce_desired());
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, success_keeps_cce_desired)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    onboard("enrollee1");
    EXPECT_TRUE(m_task->is_cce_desired());
    // Same (onboarded) URI still stored: no DISABLE.
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, uri_removal_keeps_cce_desired)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    ASSERT_TRUE(m_database->remove_dpp_bootstrap_info("enrollee1"));
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
}

// Whole-store clear never withdraws the CCE, also after success.
TEST_F(DppOnboardingTaskTest, uri_clear_after_success_keeps_cce_desired)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    onboard("enrollee1");
    ASSERT_TRUE(m_task->is_cce_desired());
    ASSERT_TRUE(m_database->remove_dpp_bootstrap_info("enrollee1"));
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, new_uri_after_success_keeps_cce_desired)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    onboard("enrollee1");
    ASSERT_TRUE(m_task->is_cce_desired());
    add_uri("enrollee2", "KEY2");
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
    EXPECT_EQ(2U, m_database->get_dpp_bootstrap_info_count());
}

// Review point 5: completion is tracked per URI, not for the whole store.
TEST_F(DppOnboardingTaskTest, second_uri_onboarding_keeps_cce_desired)
{
    add_uri("enrollee1", "KEY1");
    add_uri("enrollee2", "KEY2");
    m_task->work();
    ASSERT_TRUE(m_task->is_cce_desired());
    onboard("enrollee1");
    EXPECT_TRUE(m_task->is_cce_desired());
    EXPECT_TRUE(m_database->has_pending_dpp_bootstrap_info());
    // URI store unchanged: the task must still wait for enrollee2.
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
    onboard("enrollee2");
    EXPECT_FALSE(m_database->has_pending_dpp_bootstrap_info());
    EXPECT_TRUE(m_task->is_cce_desired());
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, removing_pending_uri_keeps_cce_when_rest_onboarded)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    onboard("enrollee1");
    add_uri("enrollee2", "KEY2");
    m_task->work();
    ASSERT_TRUE(m_task->is_cce_desired());
    ASSERT_TRUE(m_database->remove_dpp_bootstrap_info("enrollee2"));
    m_task->work();
    EXPECT_TRUE(m_task->is_cce_desired());
}

// A URI change is synced when it is notified, not deferred to a later work() that would reset
// a session started by a chirp in between (M-13 / M-15). Observable: clearing the store
// while waiting for Authentication drops the pending Auth events on the trigger alone, so
// the expired wait never times out. Deferred to work(), the wait would time out once.
TEST_F(DppOnboardingTaskTest, bootstrap_trigger_syncs_store_immediately)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    m_task->handle_event(son::dpp_onboarding_task::AUTH_STARTED, nullptr);
    ASSERT_TRUE(m_database->remove_dpp_bootstrap_info("enrollee1"));
    m_task->handle_event(son::dpp_onboarding_task::BOOTSTRAP_TRIGGERED, nullptr);
    // No Auth events are left pending, so this pass ends the event wait.
    m_task->execute();
    m_task->expire_event_wait();
    m_task->execute();
    EXPECT_EQ(0, m_task->timeout_count);
    EXPECT_TRUE(m_task->is_cce_desired());
}

// A (restarted) Authentication Request re-arms the wait; the URI stays pending.
TEST_F(DppOnboardingTaskTest, auth_started_keeps_onboarding_pending)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    m_task->handle_event(son::dpp_onboarding_task::AUTH_STARTED, nullptr);
    m_task->handle_event(son::dpp_onboarding_task::AUTH_STARTED, nullptr);
    EXPECT_TRUE(m_task->is_cce_desired());
    onboard("enrollee1");
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, send_cce_to_agent_without_controller_is_safe)
{
    m_task->send_cce_to_agent(tlvf::mac_from_string("02:00:00:00:00:01"));
    add_uri("enrollee1", "KEY1");
    m_task->work();
    m_task->send_cce_to_agent(tlvf::mac_from_string("02:00:00:00:00:01"));
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, uri_clear_never_disables_cce)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    ASSERT_TRUE(m_database->remove_dpp_bootstrap_info("enrollee1"));
    m_task->event_received(son::dpp_onboarding_task::BOOTSTRAP_TRIGGERED);
    EXPECT_TRUE(m_task->is_cce_desired());
    m_task->event_received(son::dpp_onboarding_task::WIFI_BACKHAUL_OBSERVED);
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, wifi_observation_preserves_auth_events_and_deadline)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    // The first work() can defer to next_retry_time; enter WAIT_AUTH deterministically.
    m_task->event_received(son::dpp_onboarding_task::AUTH_STARTED);
    m_task->expire_event_wait();
    m_task->event_received(son::dpp_onboarding_task::WIFI_BACKHAUL_OBSERVED);
    m_task->event_received(son::dpp_onboarding_task::WIFI_BACKHAUL_OBSERVED);
    m_task->execute();
    // An expired deadline must not be rearmed by a repeated topology observation.
    EXPECT_EQ(1, m_task->timeout_count);
    using Task = son::dpp_onboarding_task;
    const std::multiset<int> expected{Task::AUTH_SUCCESS,       Task::AUTH_INIT_FAILED,
                                      Task::CONF_SENT,          Task::CONF_RECEIVED,
                                      Task::CONF_FAILED,        Task::FAIL,
                                      Task::BOOTSTRAP_TRIGGERED};
    EXPECT_EQ(expected, m_task->timed_out_events);
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, wifi_observation_and_resend_preserve_configuration_wait)
{
    add_uri("enrollee1", "KEY1");
    m_task->work();
    m_task->event_received(son::dpp_onboarding_task::AUTH_STARTED);
    m_task->event_received(son::dpp_onboarding_task::AUTH_SUCCESS);
    m_task->expire_event_wait();
    m_task->event_received(son::dpp_onboarding_task::WIFI_BACKHAUL_OBSERVED);
    m_task->send_cce_to_agent(tlvf::mac_from_string("02:00:00:00:00:01"));
    m_task->execute();
    EXPECT_EQ(1, m_task->timeout_count);
    using Task = son::dpp_onboarding_task;
    const std::multiset<int> expected{Task::CONF_SENT, Task::CONF_RECEIVED, Task::CONF_FAILED,
                                      Task::FAIL, Task::BOOTSTRAP_TRIGGERED};
    EXPECT_EQ(expected, m_task->timed_out_events);
    EXPECT_TRUE(m_task->is_cce_desired());
}

TEST_F(DppOnboardingTaskTest, repeated_wifi_observation_keeps_empty_store_idle)
{
    for (int i = 0; i < 3; ++i) {
        m_task->event_received(son::dpp_onboarding_task::WIFI_BACKHAUL_OBSERVED);
        m_task->work();
    }
    EXPECT_TRUE(m_task->is_cce_desired());
    m_task->expire_event_wait();
    m_task->execute();
    EXPECT_EQ(0, m_task->timeout_count);
    EXPECT_EQ(0U, m_database->get_dpp_bootstrap_info_count());
}

TEST_F(DppOnboardingTaskTest, targeted_resend_reconciles_pending_uri_before_work)
{
    add_uri("enrollee1", "KEY1");
    m_task->send_cce_to_agent(tlvf::mac_from_string("02:00:00:00:00:01"));
    EXPECT_TRUE(m_task->is_cce_desired());
}

} // namespace
