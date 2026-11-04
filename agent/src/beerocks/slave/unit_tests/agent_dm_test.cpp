/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_db.h"
#include "agent_dm.h"

#include <ambiorix_dummy.h>
#include <gtest/gtest.h>
#include <mapf/common/amx_mutex.h>

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

using beerocks::AgentDB;
using beerocks::AgentDM;
using Strings                  = std::map<std::string, std::string>;
constexpr auto wait_timeout    = std::chrono::seconds(2);
constexpr auto blocked_timeout = std::chrono::milliseconds(100);

void require(bool condition, const char *failure)
{
    if (!condition) {
        std::fprintf(stderr, "AgentDM regression: %s\n", failure);
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
    constexpr char failure[] = "AgentDM regression: child watchdog expired\n";
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

bool amx_available_to_other_thread()
{
    bool locked = false;
    std::thread other([&]() {
        locked = beerocks::amx_mutex().try_lock();
        if (locked) {
            beerocks::amx_mutex().unlock();
        }
    });
    other.join();
    return locked;
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
    std::function<void()> on_publish = []() {};

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

std::shared_ptr<RecordingDataModel> init_recording_data_model()
{
    auto dm = std::make_shared<RecordingDataModel>();
    require(AgentDM::get()->init_data_model(dm), "datamodel initialization failed");
    return dm;
}

class AgentDMDeathTest : public ::testing::Test {
    void SetUp() override { ::testing::FLAGS_gtest_death_test_style = "threadsafe"; }
};

// The CI Cppcheck scan lacks GoogleTest headers and cannot expand this fixture
// macro. Compile and run the death tests through beerocks_agent_unit_tests.
// cppcheck-suppress syntaxError
TEST_F(AgentDMDeathTest, SettersPublishImmediatelyWhileHoldingAmx)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm        = init_recording_data_model();
                    dm->on_publish = []() {
                        require(!amx_available_to_other_thread(),
                                "datamodel was written without holding the AMX mutex");
                    };

                    require(AgentDM::get()->dm_set_agent_state("INIT (1)", "OPERATIONAL (2)"),
                            "agent state was not published");
                    require(dm->operations().size() == 2U,
                            "agent state was not published before the scope ended");
                    {
                        auto agent_dm = AgentDM::get();
                        require(agent_dm->dm_set_controller_connected(true),
                                "ControllerConnected was not published");
                        require(agent_dm->dm_set_management_mode("Multi-AP-Agent"),
                                "ManagementMode was not published");
                        require(agent_dm->dm_set_fronthaul_interfaces("wlan0,wlan2"),
                                "FronthaulIfaces was not published");
                    }
                    require(amx_available_to_other_thread(),
                            "AgentDM kept the AMX mutex after its scope ended");

                    const auto operations = dm->operations();
                    ASSERT_EQ(operations.size(), 5U);
                    EXPECT_EQ(operations[0].path, AGENT_ROOT_DM ".Info");
                    EXPECT_EQ(operations[0].values.at("CurrentState"), "INIT (1)");
                    EXPECT_EQ(operations[1].values.at("BestState"), "OPERATIONAL (2)");
                    EXPECT_EQ(operations[2].values.at("ControllerConnected"), "true");
                    EXPECT_EQ(operations[3].values.at("ManagementMode"), "Multi-AP-Agent");
                    EXPECT_EQ(operations[4].values.at("FronthaulIfaces"), "wlan0,wlan2");
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentDMDeathTest, NestedAndMovedScopesSerializeThreadsUntilOutermostRelease)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    std::promise<void> attempt;
                    auto attempting = attempt.get_future();
                    std::promise<void> publish;
                    auto published = publish.get_future();
                    std::thread other;
                    {
                        auto outer = AgentDM::get();
                        {
                            auto nested = AgentDM::get();
                            auto moved  = std::move(nested);
                            moved->dm_set_management_mode("owner");
                        }
                        other = std::thread([&]() {
                            attempt.set_value();
                            AgentDM::get()->dm_set_management_mode("other");
                            publish.set_value();
                        });
                        await(attempting, "second thread did not start");
                        require(published.wait_for(blocked_timeout) == std::future_status::timeout,
                                "second thread entered AgentDM while the outer scope was held");
                        require(!amx_available_to_other_thread(),
                                "outer AgentDM scope does not hold the AMX mutex");
                    }
                    await(published, "second thread did not enter AgentDM after the outer release");
                    other.join();

                    const auto operations = dm->operations();
                    ASSERT_EQ(operations.size(), 2U);
                    EXPECT_EQ(operations[0].values.at("ManagementMode"), "owner");
                    EXPECT_EQ(operations[1].values.at("ManagementMode"), "other");
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentDMDeathTest, DatamodelCallbackMayReenterOnTheSameThread)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm        = init_recording_data_model();
                    bool reentered = false;
                    dm->on_publish = [&]() {
                        if (!reentered) {
                            reentered = true;
                            AgentDM::get()->dm_set_management_mode("callback");
                        }
                    };
                    AgentDM::get()->dm_set_management_mode("first");

                    const auto operations = dm->operations();
                    ASSERT_EQ(operations.size(), 2U);
                    EXPECT_EQ(operations[0].values.at("ManagementMode"), "first");
                    EXPECT_EQ(operations[1].values.at("ManagementMode"), "callback");
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentDMDeathTest, AgentDBMayBeTakenInsideAgentDMScope)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    std::string mode;
                    {
                        auto agent_dm = AgentDM::get();
                        {
                            auto db = AgentDB::get();
                            mode    = "from-db";
                        }
                        agent_dm->dm_set_management_mode(mode);
                    }
                    const auto operations = dm->operations();
                    ASSERT_EQ(operations.size(), 1U);
                    EXPECT_EQ(operations[0].values.at("ManagementMode"), "from-db");
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

#ifdef ENABLE_NBAPI
TEST_F(AgentDMDeathTest, FronthaulCreationReturnsOnlyFullyInitializedInstance)
{
    ASSERT_EXIT(
        run_child([]() {
            auto dm                = init_recording_data_model();
            dm->existing_index     = 4;
            const std::string root = AGENT_ROOT_DM ".Info.Fronthaul";
            EXPECT_EQ(AgentDM::get()->dm_create_fronthaul_object("owned-iface"), root + ".7");

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
            const Strings expected{
                {"Iface", "owned-iface"}, {"CurrentState", "INIT (0)"}, {"BestState", "INIT (0)"}};
            EXPECT_EQ(operations[3].values, expected);
        }),
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentDMDeathTest, FailedFronthaulInitializationRollsBackAndReturnsEmptyPath)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm                     = init_recording_data_model();
                    dm->initialize_successfully = false;
                    EXPECT_TRUE(AgentDM::get()->dm_create_fronthaul_object("owned-iface").empty());

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
