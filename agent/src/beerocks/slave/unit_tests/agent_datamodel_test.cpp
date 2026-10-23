/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_datamodel.h"
#include "agent_db.h"

#include <ambiorix_dummy.h>
#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

using beerocks::AgentDataModel;
using beerocks::AgentDB;
using Strings               = std::map<std::string, std::string>;
constexpr auto wait_timeout = std::chrono::seconds(2);

void require(bool condition, const char *failure)
{
    if (!condition) {
        std::fprintf(stderr, "AgentDataModel regression: %s\n", failure);
        _exit(EXIT_FAILURE);
    }
}

void await(std::future<void> &event, const char *failure)
{
    require(event.wait_for(wait_timeout) == std::future_status::ready, failure);
    event.get();
}

void watchdog(int)
{
    constexpr char failure[] = "AgentDataModel regression: child watchdog expired\n";
    const auto written       = write(STDERR_FILENO, failure, sizeof(failure) - 1);
    (void)written;
    _exit(EXIT_FAILURE);
}

template <typename Scenario> void run_child(Scenario scenario)
{
    // The singleton is initialized once per isolated child, without a test-only
    // reset API. Bound deadlocks, including joins, independently of test gates.
    std::signal(SIGALRM, watchdog);
    alarm(8);
    scenario();
    _exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
}

void verify_database_is_unlocked()
{
    std::promise<void> acquired;
    auto completed = acquired.get_future();
    std::thread reader([&]() {
        {
            auto db = AgentDB::get();
        }
        acquired.set_value();
    });
    await(completed, "publication retained AgentDB while another thread tried to acquire it");
    reader.join();
}

class RecordingDataModel : public beerocks::nbapi::AmbiorixDummy {
public:
    struct Operation {
        std::string kind;
        std::string path;
        Strings values;
        uint32_t index = 0;
    };

    using beerocks::nbapi::AmbiorixDummy::set;

    bool set(const std::string &path, const std::string &parameter,
             const std::string &value) override
    {
        record({"set", path, {{parameter, value}}, 0});
        return true;
    }

    bool set(const std::string &path, const std::string &parameter, const bool &value) override
    {
        const std::string text = value ? "true" : "false";
        record({"set", path, {{parameter, text}}, 0});
        return true;
    }

    bool set_strings(const std::string &path, const Strings &values) override
    {
        record({"set_strings", path, values, 0});
        return initialize_successfully;
    }

    uint32_t get_instance_index(const std::string &path, const std::string &key) override
    {
        record({"lookup", path, {{"Iface", key}}, existing_index});
        return existing_index;
    }

    std::string add_instance(const std::string &path) override
    {
        record({"add", path, {}, 7});
        return path + ".7";
    }

    bool remove_instance(const std::string &path, uint32_t index) override
    {
        record({"remove", path, {}, index});
        return true;
    }

    std::vector<Operation> operations()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_operations;
    }

    uint32_t existing_index          = 0;
    bool initialize_successfully     = true;
    std::function<void()> on_publish = verify_database_is_unlocked;

private:
    void record(Operation operation)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_operations.push_back(std::move(operation));
        }
        on_publish();
    }

    std::mutex m_mutex;
    std::vector<Operation> m_operations;
};

class AgentDataModelDeathTest : public ::testing::Test {
    void SetUp() override { ::testing::FLAGS_gtest_death_test_style = "threadsafe"; }
};

// The CI Cppcheck scan lacks GoogleTest headers and cannot expand this fixture
// macro. Compile and run the death tests through beerocks_agent_unit_tests.
// cppcheck-suppress syntaxError
TEST_F(AgentDataModelDeathTest, NestedMovedScopesPreserveOwnedUpdatesUntilOutermostRelease)
{
    ASSERT_EXIT(
        run_child([]() {
            auto dm       = std::make_shared<RecordingDataModel>();
            auto &adapter = AgentDataModel::get();
            ASSERT_TRUE(adapter.init_data_model(dm));
            {
                auto outer = AgentDB::get();
                {
                    auto nested         = AgentDB::get();
                    auto moved          = std::move(nested);
                    std::string current = "current-before-copy";
                    std::string best    = "best-before-copy";
                    adapter.dm_set_agent_state(current, best);
                    adapter.dm_set_fronthaul_interfaces(std::string("owned-iface"));
                    current = "modified-current";
                    best    = "modified-best";
                    require(dm->operations().empty(), "nested scope published while DB was held");
                }
                require(dm->operations().empty(),
                        "nested or moved-from destructor released the outer publication fence");
            }
            const auto operations = dm->operations();
            ASSERT_EQ(operations.size(), 2U);
            EXPECT_EQ(operations[0].kind, "set_strings");
            const Strings expected_state{{"CurrentState", "current-before-copy"},
                                         {"BestState", "best-before-copy"}};
            EXPECT_EQ(operations[0].values, expected_state);
            EXPECT_EQ(operations[1].values.at("FronthaulIfaces"), "owned-iface");
        }),
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentDataModelDeathTest, ReadyPublisherCannotOvertakeUnreleasedDatabaseScope)
{
    ASSERT_EXIT(
        run_child([]() {
            auto dm       = std::make_shared<RecordingDataModel>();
            auto &adapter = AgentDataModel::get();
            ASSERT_TRUE(adapter.init_data_model(dm));
            std::promise<void> queued;
            auto first_queued = queued.get_future();
            std::promise<void> release;
            auto may_release = release.get_future();
            std::thread first([&]() {
                auto db = AgentDB::get();
                adapter.dm_set_management_mode("first-unready");
                queued.set_value();
                await(may_release, "first publisher did not receive its release gate");
            });
            await(first_queued, "first publisher did not enqueue under AgentDB");

            std::promise<void> enqueued_ready;
            auto second_queued = enqueued_ready.get_future();
            std::thread second([&]() {
                adapter.dm_set_management_mode("second-ready");
                enqueued_ready.set_value();
            });
            await(second_queued, "ready publisher blocked instead of deferring behind the FIFO");
            require(dm->operations().empty(), "ready publisher drained an unreleased DB scope");
            release.set_value();
            second.join();
            first.join();

            const auto operations = dm->operations();
            ASSERT_EQ(operations.size(), 2U);
            EXPECT_EQ(operations[0].values.at("ManagementMode"), "first-unready");
            EXPECT_EQ(operations[1].values.at("ManagementMode"), "second-ready");
        }),
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentDataModelDeathTest, CallbackReentryAppendsAfterAlreadyQueuedUpdates)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm       = std::make_shared<RecordingDataModel>();
                    auto &adapter = AgentDataModel::get();
                    ASSERT_TRUE(adapter.init_data_model(dm));
                    bool reentered       = false;
                    bool inside_callback = false;
                    dm->on_publish       = [&]() {
                        require(!inside_callback,
                                "publication reentered an unfinished datamodel callback");
                        inside_callback = true;
                        verify_database_is_unlocked();
                        if (!reentered) {
                            reentered        = true;
                            auto callback_db = AgentDB::get();
                            adapter.dm_set_management_mode("callback");
                        }
                        inside_callback = false;
                    };
                    {
                        auto db = AgentDB::get();
                        adapter.dm_set_management_mode("first");
                        adapter.dm_set_management_mode("second");
                    }
                    const auto operations = dm->operations();
                    ASSERT_EQ(operations.size(), 3U);
                    EXPECT_EQ(operations[0].values.at("ManagementMode"), "first");
                    EXPECT_EQ(operations[1].values.at("ManagementMode"), "second");
                    EXPECT_EQ(operations[2].values.at("ManagementMode"), "callback");
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

#ifdef ENABLE_NBAPI
TEST_F(AgentDataModelDeathTest, FronthaulCreationReturnsOnlyFullyInitializedInstance)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm            = std::make_shared<RecordingDataModel>();
                    dm->existing_index = 4;
                    auto &adapter      = AgentDataModel::get();
                    ASSERT_TRUE(adapter.init_data_model(dm));
                    const std::string root = AGENT_ROOT_DM ".Info.Fronthaul";
                    EXPECT_EQ(adapter.dm_create_fronthaul_object("owned-iface"), root + ".7");

                    const auto operations = dm->operations();
                    ASSERT_EQ(operations.size(), 4U);
                    EXPECT_EQ(operations[0].kind, "lookup");
                    EXPECT_EQ(operations[1].kind, "remove");
                    EXPECT_EQ(operations[1].path, root);
                    EXPECT_EQ(operations[1].index, 4U);
                    EXPECT_EQ(operations[2].kind, "add");
                    EXPECT_EQ(operations[2].path, root);
                    EXPECT_EQ(operations[3].kind, "set_strings");
                    EXPECT_EQ(operations[3].path, root + ".7");
                    const Strings expected{{"Iface", "owned-iface"},
                                           {"CurrentState", "INIT (0)"},
                                           {"BestState", "INIT (0)"}};
                    EXPECT_EQ(operations[3].values, expected);
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentDataModelDeathTest, FailedFronthaulInitializationRollsBackAndReturnsEmptyPath)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm                     = std::make_shared<RecordingDataModel>();
                    dm->initialize_successfully = false;
                    auto &adapter               = AgentDataModel::get();
                    ASSERT_TRUE(adapter.init_data_model(dm));
                    EXPECT_TRUE(adapter.dm_create_fronthaul_object("owned-iface").empty());

                    const auto operations = dm->operations();
                    ASSERT_EQ(operations.size(), 4U);
                    EXPECT_EQ(operations[0].kind, "lookup");
                    EXPECT_EQ(operations[1].kind, "add");
                    EXPECT_EQ(operations[2].kind, "set_strings");
                    const Strings expected{{"Iface", "owned-iface"},
                                           {"CurrentState", "INIT (0)"},
                                           {"BestState", "INIT (0)"}};
                    EXPECT_EQ(operations[2].values, expected);
                    EXPECT_EQ(operations[3].kind, "remove");
                    EXPECT_EQ(operations[3].path, AGENT_ROOT_DM ".Info.Fronthaul");
                    EXPECT_EQ(operations[3].index, 7U);
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif // ENABLE_NBAPI

} // namespace
