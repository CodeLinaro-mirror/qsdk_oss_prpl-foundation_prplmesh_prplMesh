/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_dm.h"
#include "agent_info_publisher.h"

#include <ambiorix_dummy.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

using namespace beerocks;
using Strings = std::map<std::string, std::string>;
using Writes  = std::vector<std::pair<std::string, std::string>>;

void require(bool condition, const char *failure)
{
    if (!condition) {
        std::fprintf(stderr, "AgentInfoPublisher regression: %s\n", failure);
        _exit(EXIT_FAILURE);
    }
}

void watchdog(int)
{
    constexpr char failure[] = "AgentInfoPublisher regression: child watchdog expired\n";
    const auto written       = write(STDERR_FILENO, failure, sizeof(failure) - 1);
    (void)written;
    _exit(EXIT_FAILURE);
}

template <typename Scenario> void run_child(Scenario scenario)
{
    // Initialize AgentDM once per isolated child, as in agent_dm_test.cpp.
    std::signal(SIGALRM, watchdog);
    alarm(8);
    scenario();
    _exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
}

class RecordingDataModel : public nbapi::AmbiorixDummy {
public:
    struct Operation {
        std::string kind;
        std::string path;
        Strings values;
    };

    using nbapi::AmbiorixDummy::set;

    bool set(const std::string &path, const std::string &parameter,
             const std::string &value) override
    {
        return record(path, parameter, value);
    }

    bool set(const std::string &path, const std::string &parameter, const bool &value) override
    {
        return record(path, parameter, value ? "true" : "false");
    }

    void reject_next(const std::string &parameter, unsigned count = 1)
    {
        m_rejections[parameter] = count;
    }

    const std::vector<Operation> &operations() const { return m_operations; }
    void clear_operations() { m_operations.clear(); }

private:
    bool record(const std::string &path, const std::string &parameter, const std::string &value)
    {
        // Record rejected attempts too: retries must be observable independently
        // of the value that the data model would retain after a successful set.
        m_operations.push_back({"set", path, {{parameter, value}}});
        auto &remaining = m_rejections[parameter];
        if (remaining != 0) {
            --remaining;
            return false;
        }
        return true;
    }

    std::map<std::string, unsigned> m_rejections;
    std::vector<Operation> m_operations;
};

std::shared_ptr<RecordingDataModel> init_recording_data_model()
{
    auto dm = std::make_shared<RecordingDataModel>();
    require(AgentDM::get()->init_data_model(dm), "datamodel initialization failed");
    return dm;
}

void publish_and_expect(AgentInfoPublisher &publisher, RecordingDataModel &dm, bool connected,
                        const Writes &expected)
{
    dm.clear_operations();
    publisher.publish(connected);
    const auto &operations = dm.operations();

    // Assert both state-key attempt counts, including zero on idle ticks and
    // ControllerConnected-only retries. Expected writes are literal test data.
    for (const auto *key : {"CurrentState", "BestState", "ControllerConnected"}) {
        SCOPED_TRACE(key);
        const auto attempts = std::count_if(
            operations.begin(), operations.end(),
            [key](const RecordingDataModel::Operation &op) { return op.values.count(key) != 0; });
        const auto wanted =
            std::count_if(expected.begin(), expected.end(),
                          [key](const Writes::value_type &write) { return write.first == key; });
        EXPECT_EQ(attempts, wanted);
    }

    ASSERT_EQ(operations.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(operations[i].kind, "set");
        EXPECT_EQ(operations[i].path, AGENT_ROOT_DM ".Info");
        const Strings values{{expected[i].first, expected[i].second}};
        EXPECT_EQ(operations[i].values, values);
    }
}

class AgentInfoPublisherDeathTest : public ::testing::Test {
    void SetUp() override { ::testing::FLAGS_gtest_death_test_style = "threadsafe"; }
};

// cppcheck-suppress syntaxError
TEST_F(AgentInfoPublisherDeathTest, RejectedCurrentStateRetriesBothKeysThenQuiets)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publisher = STATE_INIT;
                    dm->reject_next("CurrentState");

                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "INIT (1)"},
                                        {"BestState", "INIT (1)"},
                                        {"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "INIT (1)"}, {"BestState", "INIT (1)"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, RejectedBestStateRetriesAndRewritesCurrentState)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publisher = STATE_INIT;
                    dm->reject_next("BestState");

                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "INIT (1)"},
                                        {"BestState", "INIT (1)"},
                                        {"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "INIT (1)"}, {"BestState", "INIT (1)"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, RejectedColdStartFalseRetriesThenQuiets)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    dm->reject_next("ControllerConnected");

                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, RejectedTrueToFalseRetriesThenQuiets)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publish_and_expect(publisher, *dm, true, {{"ControllerConnected", "true"}});
                    dm->reject_next("ControllerConnected");

                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, TwoControllerRejectionsKeepRetryPending)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publish_and_expect(publisher, *dm, true, {{"ControllerConnected", "true"}});
                    dm->reject_next("ControllerConnected", 2);

                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, RejectedFalseThenDesiredTrueNeedsNoWrite)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publish_and_expect(publisher, *dm, true, {{"ControllerConnected", "true"}});
                    dm->reject_next("ControllerConnected");
                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});

                    publish_and_expect(publisher, *dm, true, {});
                    publish_and_expect(publisher, *dm, true, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, SuccessfulPublishIsNotRepeatedOnIdleTicks)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publisher = STATE_INIT;

                    publish_and_expect(publisher, *dm, true,
                                       {{"CurrentState", "INIT (1)"},
                                        {"BestState", "INIT (1)"},
                                        {"ControllerConnected", "true"}});
                    publish_and_expect(publisher, *dm, true, {});
                    publish_and_expect(publisher, *dm, true, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, FirstPublishWritesDefaultFalse)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;

                    publish_and_expect(publisher, *dm, false, {{"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, StateAssignmentsCoalesceAndPreserveBestState)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publisher = STATE_INIT;
                    publisher = STATE_LOAD_PLATFORM_CONFIGURATION;
                    publisher = STATE_INIT;
                    EXPECT_TRUE(dm->operations().empty());

                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "INIT (1)"},
                                        {"BestState", "LOAD_PLATFORM_CONFIGURATION (2)"},
                                        {"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, BestStateClampsAtOperational)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publisher = STATE_STOPPED;

                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "STOPPED (16)"},
                                        {"BestState", "OPERATIONAL (15)"},
                                        {"ControllerConnected", "false"}});
                    publisher = STATE_INIT;
                    publish_and_expect(
                        publisher, *dm, false,
                        {{"CurrentState", "INIT (1)"}, {"BestState", "OPERATIONAL (15)"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, NewDesiredStateReplacesPendingRetry)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publisher = STATE_LOAD_PLATFORM_CONFIGURATION;
                    dm->reject_next("BestState");
                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "LOAD_PLATFORM_CONFIGURATION (2)"},
                                        {"BestState", "LOAD_PLATFORM_CONFIGURATION (2)"},
                                        {"ControllerConnected", "false"}});

                    publisher = STATE_INIT;
                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "INIT (1)"},
                                        {"BestState", "LOAD_PLATFORM_CONFIGURATION (2)"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(AgentInfoPublisherDeathTest, StateFailureDoesNotBlockControllerInSameTick)
{
    ASSERT_EXIT(run_child([]() {
                    auto dm = init_recording_data_model();
                    AgentInfoPublisher publisher;
                    publish_and_expect(publisher, *dm, true, {{"ControllerConnected", "true"}});
                    publisher = STATE_INIT;
                    dm->reject_next("CurrentState");
                    dm->reject_next("BestState");

                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "INIT (1)"},
                                        {"BestState", "INIT (1)"},
                                        {"ControllerConnected", "false"}});
                    publish_and_expect(publisher, *dm, false,
                                       {{"CurrentState", "INIT (1)"}, {"BestState", "INIT (1)"}});
                    publish_and_expect(publisher, *dm, false, {});
                }),
                ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

} // namespace
