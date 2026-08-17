/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_defines.h>
#include <bcl/beerocks_event_loop_impl.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

unsigned g_traffic_separation_event_count = 0;

void ignore_event(const char *const, const amxc_var_t *const, void *const) {}

amxd_status_t ignore_action(amxd_object_t *, amxd_param_t *, amxd_action_t, const amxc_var_t *const,
                            amxc_var_t *const, void *)
{
    return amxd_status_ok;
}

void count_traffic_separation_event(const char *const, const amxc_var_t *const, void *const)
{
    ++g_traffic_separation_event_count;
}

class TrafficSeparationEventTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_amxrt                  = std::make_shared<beerocks::nbapi::Amxrt>();
        char app_name[]          = "traffic_separation_event_test";
        std::vector<char *> argv = {app_name, nullptr};

        ASSERT_EQ(0, m_amxrt->Initialize(argv.size() - 1, argv.data(), nullptr));

        m_event_loop = std::make_shared<beerocks::EventLoopImpl>();
        ASSERT_TRUE(m_event_loop->register_handlers(amxp_signal_fd(), {}));

        const std::vector<beerocks::nbapi::sActionsCallback> actions = {
            {"action_read_assoc_time", ignore_action},
            {"action_read_last_change", ignore_action},
            {"action_last_steer_time", ignore_action},
        };
        const std::vector<beerocks::nbapi::sEvents> events = {
            {"event_configuration_changed", ignore_event},
            {"event_traffic_separation_override_changed", count_traffic_separation_event},
            {"event_network_enable_changed", ignore_event},
            {"event_network_group_changed", ignore_event},
        };
        m_ambiorix = std::make_shared<beerocks::nbapi::AmbiorixImpl>(
            m_event_loop, actions, events, std::vector<beerocks::nbapi::sFunctions>{});

        ASSERT_TRUE(m_ambiorix->load_datamodel(CONTROLLER_ODL_PATH));
        ASSERT_TRUE(
            m_ambiorix->set(CONTROLLER_ROOT_DM ".Configuration.TrafficSeparation", "Enable", true));
        read_all_pending_signals();
        g_traffic_separation_event_count = 0;
    }

    void TearDown() override
    {
        m_ambiorix.reset();
        m_event_loop.reset();
        m_amxrt.reset();
    }

    void read_all_pending_signals()
    {
        while (amxp_signal_read() == 0)
            ;
    }

    std::shared_ptr<beerocks::nbapi::Amxrt> m_amxrt;
    std::shared_ptr<beerocks::EventLoopImpl> m_event_loop;
    std::shared_ptr<beerocks::nbapi::AmbiorixImpl> m_ambiorix;
};

// cppcheck-suppress syntaxError
TEST_F(TrafficSeparationEventTest, enable_change_triggers_override_event)
{
    ASSERT_TRUE(
        m_ambiorix->set(CONTROLLER_ROOT_DM ".Configuration.TrafficSeparation", "Enable", false));
    read_all_pending_signals();

    EXPECT_EQ(1U, g_traffic_separation_event_count);
}

TEST_F(TrafficSeparationEventTest, private_vid_change_does_not_trigger_override_event)
{
    ASSERT_TRUE(m_ambiorix->set(CONTROLLER_ROOT_DM ".Configuration.TrafficSeparation", "PrivateVID",
                                std::int32_t{11}));
    read_all_pending_signals();

    EXPECT_EQ(0U, g_traffic_separation_event_count);
}

TEST_F(TrafficSeparationEventTest, guest_vid_change_does_not_trigger_override_event)
{
    ASSERT_TRUE(m_ambiorix->set(CONTROLLER_ROOT_DM ".Configuration.TrafficSeparation", "GuestVID",
                                std::int32_t{21}));
    read_all_pending_signals();

    EXPECT_EQ(0U, g_traffic_separation_event_count);
}

} // namespace
