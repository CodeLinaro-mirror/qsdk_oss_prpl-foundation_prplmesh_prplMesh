/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include "../../son_actions.h"
#include "../dpp_cce_indication_task.h"

#include <gtest/gtest.h>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/ieee_1905_1/eMessageType.h>

#include <array>
#include <vector>

namespace {
struct CapturedSend {
    sMacAddr destination;
    std::vector<uint8_t> wire;
};
std::vector<CapturedSend> sends;
bool fail_first = false;
} // namespace

// Confined to this executable: do not link the production son_actions.cpp.
namespace son {
bool son_actions::send_cmdu_to_agent(const sMacAddr &destination,
                                     ieee1905_1::CmduMessageTx &message, db &, const std::string &)
{
    // The task rebuilds the CMDU for every send, so finalize every message.
    if (!message.finalize()) {
        ADD_FAILURE() << "CCE message failed serialization";
        return false;
    }
    sends.push_back(
        {destination, std::vector<uint8_t>(message.getMessageBuff(),
                                           message.getMessageBuff() + message.getMessageLength())});
    return !(fail_first && sends.size() == 1);
}
} // namespace son

namespace {
using Advertise = wfa_map::tlvDppCceIndication::eAdvertiseCee;

class TestableDppCceIndicationTask : public son::dpp_cce_indication_task {
public:
    using son::dpp_cce_indication_task::dpp_cce_indication_task;
    using son::dpp_cce_indication_task::work;
};

class DppCceIndicationTaskTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        sends.clear();
        fail_first                        = false;
        logging_config.sLog.global_levels = "error,fatal,warning";
        logging_config.sLog.syslog_levels = "off";
        logger.reset(new beerocks::logging("dpp-cce-test", logging_config.sLog));
        database.reset(
            new son::db(config, *logger, tlvf::mac_from_string("02:00:00:00:00:01"), nullptr));
    }

    void add(const char *mac, bool supported, bool connected = true)
    {
        auto agent = database->m_agents.add(tlvf::mac_from_string(mac));
        ASSERT_NE(nullptr, agent);
        agent->dpp_onboarding_support = supported;
        agent->state = connected ? beerocks::STATE_CONNECTED : beerocks::STATE_DISCONNECTED;
    }

    void verify(Advertise expected)
    {
        for (auto &sent : sends) {
            ieee1905_1::CmduMessageRx rx(sent.wire.data(), sent.wire.size());
            ASSERT_TRUE(rx.parse());
            EXPECT_EQ(ieee1905_1::eMessageType::DPP_CCE_INDICATION_MESSAGE, rx.getMessageType());
            auto indication = rx.getClass<wfa_map::tlvDppCceIndication>();
            ASSERT_NE(nullptr, indication);
            EXPECT_EQ(expected, indication->advertise_cee());
        }
    }

    beerocks::config_file::sConfigMaster logging_config{};
    son::db::sDbMasterConfig config{};
    std::unique_ptr<beerocks::logging> logger;
    std::unique_ptr<son::db> database;
    std::array<uint8_t, 4096> buffer{};
    ieee1905_1::CmduMessageTx outgoing{buffer.data(), buffer.size()};
};

TEST_F(DppCceIndicationTaskTest, EmptyDatabaseFinishesWithoutSending)
{
    son::dpp_cce_indication_task task(*database, outgoing, Advertise::ENABLE);
    task.execute();
    EXPECT_TRUE(task.is_done());
    EXPECT_TRUE(sends.empty());
}

TEST_F(DppCceIndicationTaskTest, UnsupportedConnectedAgentIsSkipped)
{
    add("02:00:00:00:00:02", false);
    ASSERT_FALSE(HasFatalFailure());
    son::dpp_cce_indication_task task(*database, outgoing, Advertise::ENABLE);
    task.execute();
    EXPECT_TRUE(task.is_done());
    EXPECT_TRUE(sends.empty());
}

TEST_F(DppCceIndicationTaskTest, SupportedDisconnectedAgentIsSkipped)
{
    add("02:00:00:00:00:02", true, false);
    ASSERT_FALSE(HasFatalFailure());
    son::dpp_cce_indication_task task(*database, outgoing, Advertise::ENABLE);
    task.execute();
    EXPECT_TRUE(task.is_done());
    EXPECT_TRUE(sends.empty());
}

TEST_F(DppCceIndicationTaskTest, EnableIsSentOnlyToEligibleAgent)
{
    add("02:00:00:00:00:02", true);
    add("02:00:00:00:00:03", false);
    add("02:00:00:00:00:04", true, false);
    ASSERT_FALSE(HasFatalFailure());
    son::dpp_cce_indication_task task(*database, outgoing, Advertise::ENABLE);
    task.execute();
    EXPECT_TRUE(task.is_done());
    ASSERT_EQ(1U, sends.size());
    EXPECT_EQ(tlvf::mac_from_string("02:00:00:00:00:02"), sends.front().destination);
    verify(Advertise::ENABLE);
}

TEST_F(DppCceIndicationTaskTest, DisableIsBroadcastToAllEligibleAgents)
{
    add("02:00:00:00:00:02", true);
    add("02:00:00:00:00:03", true);
    ASSERT_FALSE(HasFatalFailure());
    son::dpp_cce_indication_task task(*database, outgoing, Advertise::DISABLE);
    task.execute();
    EXPECT_TRUE(task.is_done());
    ASSERT_EQ(2U, sends.size());
    EXPECT_NE(sends[0].destination, sends[1].destination);
    verify(Advertise::DISABLE);
}

TEST_F(DppCceIndicationTaskTest, SendFailureDoesNotPreventOtherAgentAttempt)
{
    add("02:00:00:00:00:02", true);
    add("02:00:00:00:00:03", true);
    ASSERT_FALSE(HasFatalFailure());
    fail_first = true;
    TestableDppCceIndicationTask task(*database, outgoing, Advertise::ENABLE);
    task.execute();
    EXPECT_FALSE(task.is_done());
    ASSERT_EQ(2U, sends.size());
    EXPECT_NE(sends[0].destination, sends[1].destination);

    // Dispatch the retry directly; scheduler timing is not under test here.
    task.work();
    EXPECT_TRUE(task.is_done());
    ASSERT_EQ(3U, sends.size());
    EXPECT_EQ(sends[0].destination, sends[2].destination);

    task.work();
    EXPECT_EQ(3U, sends.size());
    verify(Advertise::ENABLE);
}

TEST_F(DppCceIndicationTaskTest, FailedSingleSendRetriesOnceThenFinishes)
{
    add("02:00:00:00:00:02", true);
    ASSERT_FALSE(HasFatalFailure());
    fail_first = true;
    TestableDppCceIndicationTask task(*database, outgoing, Advertise::ENABLE);
    task.execute();
    EXPECT_FALSE(task.is_done());
    ASSERT_EQ(1U, sends.size());

    // Dispatch the retry directly; scheduler timing is not under test here.
    task.work();
    EXPECT_TRUE(task.is_done());
    ASSERT_EQ(2U, sends.size());
    EXPECT_EQ(sends[0].destination, sends[1].destination);

    task.work();
    EXPECT_EQ(2U, sends.size());
    verify(Advertise::ENABLE);
}

TEST_F(DppCceIndicationTaskTest, CompletedTaskDoesNotBroadcastAgain)
{
    add("02:00:00:00:00:02", true);
    ASSERT_FALSE(HasFatalFailure());
    son::dpp_cce_indication_task task(*database, outgoing, Advertise::ENABLE);
    task.execute();
    ASSERT_EQ(1U, sends.size());
    task.execute();
    EXPECT_EQ(1U, sends.size());
}
} // namespace
