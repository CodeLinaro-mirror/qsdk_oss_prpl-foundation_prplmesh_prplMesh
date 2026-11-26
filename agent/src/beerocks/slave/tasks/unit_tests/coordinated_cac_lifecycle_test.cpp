/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "coordinated_cac_task.h"
#include "switch_channel_fsm.h"
#include <gtest/gtest.h>

#include <array>
#include <deque>

namespace {
using namespace beerocks;
namespace cac = beerocks::coordinated_cac;
namespace sw  = beerocks::switch_channel;
using namespace std::chrono;

class EventQueue : public TaskPoolInterface {
public:
    void add_task(const std::shared_ptr<Task>) override {}
    void send_event(eTaskEvent event, std::shared_ptr<void> data) override
    {
        pending.emplace_back(event, data);
    }
    std::deque<std::pair<eTaskEvent, std::shared_ptr<void>>> pending;
};

// Exercise the two production FSMs together. Time and driver transport are replaced,
// while request allocation, event correlation, reports and database access remain real.
class CoordinatedCacLifecycle : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto db    = AgentDB::get();
        auto radio = db->radio("wlan1");
        if (!radio) {
            radio = db->add_radio("wlan1", "");
        }
        ASSERT_NE(radio, nullptr);
        radio->front.iface_mac = tlvf::mac_from_string("02:00:00:00:00:01");
        radio->wifi_channel    = WifiChannel(36, eFreqType::FREQ_5G, BANDWIDTH_40);
        radio->channels_list.clear();
        radio->channels_list[60].dfs_state = beerocks_message::eDfsState::USABLE;
        radio->channels_list[64].dfs_state = beerocks_message::eDfsState::USABLE;
        switch_fsm.reset(new sw::SwitchChannelFsm(
            "wlan1", pool, [&](const sSwitchChannelRequest &) { return transport_ok; },
            [&] { return now; }));
        cac_fsm.reset(new cac::CacFsm(pool, cmdu,
                                      [&](const std::string &, ieee1905_1::CmduMessageTx &) {
                                          ++commands;
                                          return transport_ok;
                                      },
                                      [&] { return now; }));
    }

    void drain()
    {
        while (!pool.pending.empty()) {
            auto event = pool.pending.front();
            pool.pending.pop_front();
            switch (event.first) {
            case eTaskEvent::SWITCH_CHANNEL_REQUEST_CANCELLED: {
                auto request = std::static_pointer_cast<const sSwitchChannelRequest>(event.second);
                switch_fsm->fire(sw::fsm_event::CANCELLED, &request);
                break;
            }
            case eTaskEvent::SWITCH_CHANNEL_REQUEST: {
                auto request = std::static_pointer_cast<const sSwitchChannelRequest>(event.second);
                requests.push_back(request);
                switch_fsm->fire(sw::fsm_event::SWITCH_CHANNEL_REQUEST, &request);
                break;
            }
            case eTaskEvent::SWITCH_CHANNEL_REPORT: {
                auto report = std::static_pointer_cast<const sSwitchChannelReport>(event.second);
                reports.push_back(report);
                cac_fsm->fire(cac::fsm_event::SWITCH_CHANNEL_REPORT, &report);
                break;
            }
            case eTaskEvent::SWITCH_CHANNEL_DURATION_TIME: {
                auto wait =
                    std::static_pointer_cast<const sSwitchChannelDurationTime>(event.second);
                waits.push_back(wait);
                cac_fsm->fire(cac::fsm_event::SWITCH_CHANNEL_DURATION_TIME, &wait);
                break;
            }
            default:
                FAIL() << "Unexpected event " << static_cast<int>(event.first);
            }
        }
    }

    void request(bool return_previous = true, uint8_t channel = 64)
    {
        auto msg = std::make_shared<wfa_map::tlvProfile2CacRequest>(input.data(), input.size());
        ASSERT_TRUE(msg->alloc_cac_radios(1));
        auto entry = msg->cac_radios(0);
        ASSERT_TRUE(std::get<0>(entry));
        auto &radio                           = std::get<1>(entry);
        radio.radio_uid                       = tlvf::mac_from_string("02:00:00:00:00:01");
        radio.operating_class                 = channel == 60 ? 119 : 120;
        radio.channel                         = channel;
        radio.cac_method_bit_field.cac_method = wfa_map::eCacMethod::CONTINUOUS_CAC;
        radio.cac_method_bit_field.cac_completion_action =
            return_previous ? wfa_map::tlvProfile2CacRequest::RETURN_PREVIOUS_CHANNEL
                            : wfa_map::tlvProfile2CacRequest::REMAIN_ON_CHANNEL;
        cac_fsm->fire(cac::fsm_event::CAC_REQUEST, &msg);
        drain();
    }

    void csa(uint8_t channel, eWiFiBandwidth bandwidth = BANDWIDTH_40, bool success = true)
    {
        auto msg                         = std::make_shared<sSwitchChannelNotification>();
        msg->ifname                      = "wlan1";
        msg->ap_channel_switch.channel   = channel;
        msg->ap_channel_switch.bandwidth = bandwidth;
        msg->switched                    = success;
        switch_fsm->fire(sw::fsm_event::CSA, &msg);
        drain();
    }

    void started(uint16_t duration = 60, uint8_t channel = 64, std::string ifname = "wlan1",
                 uint8_t secondary_offset = static_cast<uint8_t>(-1))
    {
        auto msg                        = std::make_shared<sCacStartedNotification>();
        msg->ifname                     = ifname;
        msg->cac_started_params.channel = channel;
        // WHM currently decodes width=0 as 20 MHz even for a 40 MHz CAC.
        msg->cac_started_params.bandwidth         = BANDWIDTH_20;
        msg->cac_started_params.secondary_channel = secondary_offset;
        msg->cac_started_params.cac_duration_sec  = duration;
        switch_fsm->fire(sw::fsm_event::CAC_STARTED, &msg);
        drain();
    }

    void completed(bool success = true, uint8_t channel = 64, std::string ifname = "wlan1",
                   eWiFiBandwidth bandwidth = BANDWIDTH_20, uint16_t center_frequency = 5310)
    {
        auto msg                            = std::make_shared<sCacCompletedNotification>();
        msg->ifname                         = ifname;
        msg->cac_completed_params.channel   = channel;
        msg->cac_completed_params.bandwidth = bandwidth;
        msg->cac_completed_params.frequency =
            son::wireless_utils::channel_to_freq(channel, eFreqType::FREQ_5G);
        msg->cac_completed_params.center_frequency1 = center_frequency;
        msg->cac_completed_params.success           = success;
        switch_fsm->fire(sw::fsm_event::CAC_COMPLETED, &msg);
        drain();
    }

    void tick(seconds elapsed)
    {
        now += elapsed;
        switch_fsm->fire(sw::fsm_event::PERIODIC);
        drain();
        cac_fsm->fire(cac::fsm_event::PERIODIC);
        drain();
    }

    void terminate(bool success, const std::string &uid = "02:00:00:00:00:01")
    {
        auto msg = std::make_shared<wfa_map::tlvProfile2CacTermination>(input.data(), input.size());
        ASSERT_TRUE(msg->alloc_cac_radios(1));
        auto entry = msg->cac_radios(0);
        ASSERT_TRUE(std::get<0>(entry));
        auto &radio           = std::get<1>(entry);
        radio.radio_uid       = tlvf::mac_from_string(uid);
        radio.operating_class = 120;
        radio.channel         = 64;
        cac_fsm->fire(cac::fsm_event::CAC_TERMINATION_REQUEST, &msg);
        if (cac_fsm->state() == cac::fsm_state::WAIT_FOR_CAC_TERMINATION) {
            uint8_t result = success;
            cac_fsm->fire(cac::fsm_event::CAC_TERMINATION_RESPONSE, &result);
        }
        drain();
    }

    void expect_restoring(eWiFiBandwidth bandwidth = BANDWIDTH_40, uint8_t channel = 36)
    {
        ASSERT_EQ(requests.size(), 2U);
        EXPECT_EQ(requests.back()->channel, channel);
        EXPECT_EQ(requests.back()->bandwidth, bandwidth);
        EXPECT_FALSE(requests.back()->wait_for_cac);
        EXPECT_EQ(cac_fsm->state(),
                  cac::fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT);
    }

    EventQueue pool;
    steady_clock::time_point now{seconds(100)};
    std::array<uint8_t, 4096> output{}, input{};
    ieee1905_1::CmduMessageTx cmdu{output.data(), output.size()};
    std::unique_ptr<sw::SwitchChannelFsm> switch_fsm;
    std::unique_ptr<cac::CacFsm> cac_fsm;
    std::vector<std::shared_ptr<const sSwitchChannelRequest>> requests;
    std::vector<std::shared_ptr<const sSwitchChannelReport>> reports;
    std::vector<std::shared_ptr<const sSwitchChannelDurationTime>> waits;
    bool transport_ok = true;
    unsigned commands = 0;
};

// Cppcheck 2.4 cannot parse GoogleTest fixture macros without their definitions.
// cppcheck-suppress syntaxError
TEST_F(CoordinatedCacLifecycle, OspDelayedStartAndCsaBeforeStart)
{
    request();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_TRUE(requests.front()->wait_for_cac);
    tick(seconds(6)); // The old five-second coordinator timeout expired here.
    csa(64);
    EXPECT_TRUE(reports.empty());
    started();
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT);
    tick(seconds(61));
    completed();
    expect_restoring();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front()->original_request, requests.front());
    EXPECT_TRUE(reports.front()->cac_happened);
    csa(36);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
}

TEST_F(CoordinatedCacLifecycle, HostapdPrimarySecondarySwapRestoresLowBandChannel)
{
    request();
    tick(seconds(4));
    csa(60); // The channel change precedes CAC-START in the OSPv2 logs.
    EXPECT_TRUE(reports.empty());
    started(60, 60, "wlan1", 1);
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_CAC_COMPLETED);
    ASSERT_EQ(waits.size(), 1U);
    tick(seconds(62)); // Completion is well beyond the initial 15-second deadline.
    EXPECT_TRUE(reports.empty());
    completed(true, 60, "wlan1", BANDWIDTH_40);
    expect_restoring();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front()->original_request->channel, 64);
    EXPECT_EQ(reports.front()->cac_completed_notification->cac_completed_params.channel, 60);
    csa(40); // The original channel's secondary cannot acknowledge restoration.
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT);
    csa(36);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
}

TEST_F(CoordinatedCacLifecycle, HostapdPrimarySecondarySwapRestoresHighBandChannel)
{
    AgentDB::get()->radio("wlan1")->wifi_channel =
        WifiChannel(149, eFreqType::FREQ_5G, BANDWIDTH_20);
    request();
    csa(60);
    started(60, 60, "wlan1", 1);
    tick(seconds(62));
    completed(true, 60, "wlan1", BANDWIDTH_40);
    expect_restoring(BANDWIDTH_20, 149);
    csa(149, BANDWIDTH_40);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT);
    csa(149, BANDWIDTH_20);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
}

TEST_F(CoordinatedCacLifecycle, HostapdPrimarySecondarySwapInReverseDirection)
{
    request(true, 60);
    started(); // Primary 64, secondary offset -1 (encoded as uint8_t).
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_CAC_COMPLETED);
    completed(true, 64, "wlan1", BANDWIDTH_40);
    expect_restoring();
    csa(36);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
}

TEST_F(CoordinatedCacLifecycle, SwappedCompletionCanRecoverMissingStart)
{
    request();
    completed(true, 60, "wlan1", BANDWIDTH_40);
    expect_restoring();
}

TEST_F(CoordinatedCacLifecycle, SwappedCacRequiresTheSameFrequencyBlock)
{
    request();
    started(60, 60, "wlan1", 0); // Only 20 MHz.
    started(60, 60);             // Wrong secondary direction: 60 + 56.
    started(60, 56, "wlan1", 1); // Adjacent but different block: 56 + 60.
    started(60, 60, "wlan2", 1);
    EXPECT_TRUE(waits.empty());
    completed(true, 60); // Only 20 MHz cannot clear the requested 40 MHz block.
    completed(true, 60, "wlan1", BANDWIDTH_80);
    completed(true, 60, "wlan1", BANDWIDTH_40, 5290);
    completed(true, 56, "wlan1", BANDWIDTH_40);
    completed(true, 60, "wlan2", BANDWIDTH_40);
    EXPECT_TRUE(reports.empty());
    started(60, 60, "wlan1", 1);
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_CAC_COMPLETED);
    completed(true, 60, "wlan1", BANDWIDTH_40);
    expect_restoring();
}

TEST_F(CoordinatedCacLifecycle, SwappedCompletionRejectsInconsistentPrimaryFrequency)
{
    request();
    auto msg                                    = std::make_shared<sCacCompletedNotification>();
    msg->ifname                                 = "wlan1";
    msg->cac_completed_params.channel           = 60;
    msg->cac_completed_params.frequency         = 5320; // Channel 64, not 60.
    msg->cac_completed_params.center_frequency1 = 5310;
    msg->cac_completed_params.bandwidth         = BANDWIDTH_40;
    msg->cac_completed_params.success           = 1;
    switch_fsm->fire(sw::fsm_event::CAC_COMPLETED, &msg);
    drain();
    EXPECT_TRUE(reports.empty());
    completed(true, 60, "wlan1", BANDWIDTH_40);
    expect_restoring();
}

TEST_F(CoordinatedCacLifecycle, SwappedCacFailureDoesNotClaimSuccess)
{
    request();
    started(60, 60, "wlan1", 1);
    completed(false, 60, "wlan1", BANDWIDTH_40);
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front()->compute_switch_channel_report_status(),
              eSwitchChannelReportStatus::CAC_FAILED);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
    EXPECT_EQ(requests.size(), 1U);
}

TEST_F(CoordinatedCacLifecycle, RemainOnSwappedChannelFinishesAfterCacCompletion)
{
    request(false);
    started(60, 60, "wlan1", 1);
    completed(true, 60, "wlan1", BANDWIDTH_40);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
    EXPECT_EQ(requests.size(), 1U);
}

TEST_F(CoordinatedCacLifecycle, CsaDuringCacAndDuplicatesDoNotFinishCac)
{
    request();
    started();
    csa(64);
    csa(64);
    EXPECT_TRUE(reports.empty());
    EXPECT_EQ(requests.size(), 1U);
    completed();
    expect_restoring();
    completed(); // A delayed duplicate cannot satisfy restoration.
    csa(64);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT);
    csa(36, BANDWIDTH_20);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT);
    csa(36);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
}

TEST_F(CoordinatedCacLifecycle, CompletionDeadlineStartsAtCacStart)
{
    request();
    tick(seconds(10));
    started();
    ASSERT_EQ(waits.size(), 1U);
    EXPECT_EQ(waits.front()->original_request, requests.front());
    EXPECT_EQ(waits.front()->started_at, now);
    tick(seconds(68)); // 78 seconds from request; 68 from actual CAC start.
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT);
    EXPECT_TRUE(reports.empty());
    completed();
    expect_restoring();
}

TEST_F(CoordinatedCacLifecycle, DuplicateStartDoesNotExtendDeadline)
{
    request();
    started();
    tick(seconds(50));
    started();
    EXPECT_EQ(waits.size(), 1U);
    now += seconds(23);
    switch_fsm->fire(sw::fsm_event::PERIODIC);
    drain();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front()->compute_switch_channel_report_status(),
              eSwitchChannelReportStatus::CAC_FAILED);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::IDLE);
}

TEST_F(CoordinatedCacLifecycle, MissingStartIsFailureAndBothFsmsRecover)
{
    request();
    csa(64);
    now += seconds(16);
    switch_fsm->fire(sw::fsm_event::PERIODIC);
    drain();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front()->compute_switch_channel_report_status(),
              eSwitchChannelReportStatus::CAC_FAILED);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
    tick(seconds(1));
    EXPECT_EQ(reports.size(), 1U);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
    request();
    EXPECT_EQ(requests.size(), 2U);
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION);
}

TEST_F(CoordinatedCacLifecycle, RejectsOtherRadioAndChannelEvents)
{
    request();
    started(60, 100);
    started(60, 64, "wlan2");
    completed(true, 100);
    completed(true, 64, "wlan2");
    csa(36);
    EXPECT_TRUE(reports.empty());
    EXPECT_TRUE(waits.empty());
    started();
    completed(true, 100);
    EXPECT_TRUE(reports.empty());
    completed();
    expect_restoring();
}

TEST_F(CoordinatedCacLifecycle, BusyRequestReportCannotFinishActiveRequest)
{
    request();
    started();
    auto other = std::make_shared<sSwitchChannelRequest>(*requests.front());
    pool.send_event(eTaskEvent::SWITCH_CHANNEL_REQUEST, other);
    drain();
    EXPECT_EQ(reports.back()->compute_switch_channel_report_status(),
              eSwitchChannelReportStatus::ANOTHER_SWITCH_IN_PROGRESS);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT);
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_CAC_COMPLETED);
    completed();
    ASSERT_EQ(requests.size(), 3U);
    EXPECT_EQ(reports.back()->original_request, requests.front());
    EXPECT_EQ(requests.back()->channel, 36);
}

TEST_F(CoordinatedCacLifecycle, OldReportAndDurationCannotAcknowledgeOrDelayRestoration)
{
    request();
    started();
    completed();
    expect_restoring();
    pool.send_event(eTaskEvent::SWITCH_CHANNEL_REPORT,
                    std::make_shared<sSwitchChannelReport>(*reports.front()));
    auto old_wait          = std::make_shared<sSwitchChannelDurationTime>(*waits.front());
    old_wait->duration_sec = seconds(1000);
    pool.send_event(eTaskEvent::SWITCH_CHANNEL_DURATION_TIME, old_wait);
    drain();
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT);
    now += seconds(21);
    cac_fsm->fire(cac::fsm_event::PERIODIC);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
}

TEST_F(CoordinatedCacLifecycle, CacFailureDoesNotClaimSuccess)
{
    request();
    started();
    completed(false);
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front()->compute_switch_channel_report_status(),
              eSwitchChannelReportStatus::CAC_FAILED);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
    EXPECT_EQ(requests.size(), 1U);
}

TEST_F(CoordinatedCacLifecycle, RestorationFailureDoesNotClaimSuccess)
{
    request();
    started();
    completed();
    csa(36, BANDWIDTH_40, false);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
}

TEST_F(CoordinatedCacLifecycle, AlreadyClearedTargetCanCompleteWithoutCac)
{
    {
        auto db                                         = AgentDB::get();
        db->radio("wlan1")->channels_list[60].dfs_state = beerocks_message::eDfsState::AVAILABLE;
        db->radio("wlan1")->channels_list[64].dfs_state = beerocks_message::eDfsState::AVAILABLE;
    }
    request();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_FALSE(requests.front()->wait_for_cac);
    csa(64);
    expect_restoring();
    csa(36);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
}

TEST_F(CoordinatedCacLifecycle, UnclearedSecondaryStillRequiresCac)
{
    AgentDB::get()->radio("wlan1")->channels_list[64].dfs_state =
        beerocks_message::eDfsState::AVAILABLE;
    request();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_TRUE(requests.front()->wait_for_cac);
    csa(64);
    EXPECT_TRUE(reports.empty());
}

TEST_F(CoordinatedCacLifecycle, RemainOnChannelFinishesOnlyAfterCompletion)
{
    request(false);
    csa(64);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT);
    started();
    completed();
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
    EXPECT_EQ(requests.size(), 1U);
}

TEST_F(CoordinatedCacLifecycle, FailedTransportReportsOnceAndResets)
{
    transport_ok = false;
    request();
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::IDLE);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front()->compute_switch_channel_report_status(),
              eSwitchChannelReportStatus::GENERAL_FAIL);
    tick(seconds(1));
    EXPECT_EQ(reports.size(), 1U);
}

TEST_F(CoordinatedCacLifecycle, IdleIgnoresLateReportWithoutCsaPayload)
{
    pool.send_event(eTaskEvent::SWITCH_CHANNEL_REPORT, std::make_shared<sSwitchChannelReport>());
    drain();
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
    EXPECT_EQ(commands, 0U);
}

TEST_F(CoordinatedCacLifecycle, FreedomRestoresOriginal20MhzAfter40MhzCac)
{
    AgentDB::get()->radio("wlan1")->wifi_channel =
        WifiChannel(36, eFreqType::FREQ_5G, BANDWIDTH_20);
    request();
    csa(64);
    started();
    completed();
    expect_restoring(BANDWIDTH_20);
    csa(36, BANDWIDTH_20);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
}

TEST_F(CoordinatedCacLifecycle, ConfirmedCancellationReleasesSwitchTask)
{
    request();
    started();
    terminate(true);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::IDLE);
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::IDLE);
    EXPECT_TRUE(reports.empty());
    request();
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION);
}

TEST_F(CoordinatedCacLifecycle, FailedCancellationIsAnError)
{
    request();
    started();
    terminate(false);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_CAC_COMPLETED);
}

TEST_F(CoordinatedCacLifecycle, CancellationForOtherRadioCannotStopActiveCac)
{
    request();
    started();
    terminate(true, "02:00:00:00:00:02");
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT);
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_CAC_COMPLETED);
}

TEST_F(CoordinatedCacLifecycle, OldCancellationCannotClearNewRequest)
{
    request();
    started();
    terminate(true);
    request();
    pool.send_event(eTaskEvent::SWITCH_CHANNEL_REQUEST_CANCELLED,
                    std::make_shared<sSwitchChannelRequest>(*requests.front()));
    drain();
    EXPECT_EQ(switch_fsm->state(), sw::fsm_state::WAIT_FOR_SWITCH_CHANNEL_NOTIFICATION);
    started();
    completed();
    EXPECT_EQ(requests.back()->channel, 36);
}

TEST_F(CoordinatedCacLifecycle, LongCacDurationDoesNotOverflow)
{
    request();
    started(65535);
    ASSERT_EQ(waits.size(), 1U);
    EXPECT_GT(waits.front()->duration_sec.count(), 65535);
    tick(seconds(65536));
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT);
    completed();
    expect_restoring();
}

TEST_F(CoordinatedCacLifecycle, CoordinatorChecksRestorationPayloadAsWellAsRequest)
{
    request();
    started();
    completed();
    expect_restoring();
    auto wrong_channel                         = std::make_shared<sSwitchChannelNotification>();
    wrong_channel->ifname                      = "wlan1";
    wrong_channel->switched                    = true;
    wrong_channel->ap_channel_switch.channel   = 64;
    wrong_channel->ap_channel_switch.bandwidth = BANDWIDTH_40;
    pool.send_event(eTaskEvent::SWITCH_CHANNEL_REPORT,
                    std::make_shared<sSwitchChannelReport>(false, requests.back(), wrong_channel,
                                                           nullptr, nullptr));
    drain();
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
}

TEST_F(CoordinatedCacLifecycle, DurationFromOtherRadioCannotExtendDeadline)
{
    request();
    started();
    auto wrong_radio          = std::make_shared<sSwitchChannelDurationTime>(*waits.front());
    wrong_radio->ifname       = "wlan2";
    wrong_radio->duration_sec = seconds(1000);
    pool.send_event(eTaskEvent::SWITCH_CHANNEL_DURATION_TIME, wrong_radio);
    drain();
    now += seconds(78);
    cac_fsm->fire(cac::fsm_event::PERIODIC);
    EXPECT_EQ(cac_fsm->state(), cac::fsm_state::ERROR);
}

} // namespace
