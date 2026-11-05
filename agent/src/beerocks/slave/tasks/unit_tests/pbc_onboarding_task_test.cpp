/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "pbc_onboarding_task.h"

#include "../../agent_db.h"

#include <bcl/network/file_descriptor.h>
#include <bcl/network/network_utils.h>
#include <beerocks/tlvf/beerocks_message.h>
#include <beerocks/tlvf/beerocks_message_apmanager.h>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/tlvftypes.h>
#include <tlvf/wfa_map/tlvMultiApPushButtonStatus.h>
#include <tlvf/wfa_map/tlvPushButtonConfig.h>

#include <easylogging++.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace beerocks {

namespace {

using eResult = wfa_map::tlvMultiApPushButtonStatus::eResult;

constexpr int g_ap_manager_fd        = 7;
constexpr char g_radio_iface[]       = "wlan0";
constexpr char g_fronthaul_ssid[]    = "fronthaul-1";
constexpr char g_fronthaul_ssid_2[]  = "fronthaul-2";
constexpr uint16_t g_listen_duration = 120;

const sMacAddr g_controller_mac = tlvf::mac_from_string("02:00:00:00:00:01");
const sMacAddr g_other_mac      = tlvf::mac_from_string("02:00:00:00:00:99");
const sMacAddr g_radio_mac      = tlvf::mac_from_string("02:00:00:00:10:00");
const sMacAddr g_bssid_1        = tlvf::mac_from_string("02:00:00:00:10:01");
const sMacAddr g_bssid_2        = tlvf::mac_from_string("02:00:00:00:10:02");
const sMacAddr g_bssid_backhaul = tlvf::mac_from_string("02:00:00:00:10:03");
const sMacAddr g_bssid_unknown  = tlvf::mac_from_string("02:00:00:00:20:01");
const sMacAddr g_enrollee_mac   = tlvf::mac_from_string("11:22:33:44:55:66");
const sMacAddr g_zero_mac       = net::network_utils::ZERO_MAC;

struct sStatus {
    sMacAddr bssid;
    sMacAddr session_al_mac;
    uint16_t session_mid;
    eResult result;
    bool has_failure_reason;
    uint16_t failure_reason;
    sMacAddr enrollee_mac;
};

struct sControllerMessage {
    ieee1905_1::eMessageType type;
    uint16_t mid;
    std::vector<sStatus> statuses;
};

struct sApManagerRequest {
    int fd;
    uint8_t action_op;
    sMacAddr bssid;
};

class LogRecorder : public el::LogDispatchCallback {
public:
    void handle(const el::LogDispatchData *data) override
    {
        lines().emplace_back(data->logMessage()->level(), data->logMessage()->message());
    }

    static std::vector<std::pair<el::Level, std::string>> &lines()
    {
        static std::vector<std::pair<el::Level, std::string>> recorded;
        return recorded;
    }

    static size_t count(el::Level level, const std::string &text)
    {
        return std::count_if(
            lines().begin(), lines().end(), [&](const std::pair<el::Level, std::string> &line) {
                return line.first == level && line.second.find(text) != std::string::npos;
            });
    }
};

class FakeMessageSender : public PBCOnboardingTask::MessageSender {
public:
    bool send_cmdu_to_controller(ieee1905_1::CmduMessageTx &cmdu_tx) override
    {
        EXPECT_TRUE(cmdu_tx.finalize());
        std::vector<uint8_t> buffer(cmdu_tx.getMessageBuff(),
                                    cmdu_tx.getMessageBuff() + cmdu_tx.getMessageLength());
        ieee1905_1::CmduMessageRx cmdu_rx(buffer.data(), buffer.size());
        EXPECT_TRUE(cmdu_rx.parse());

        sControllerMessage message{cmdu_rx.getMessageType(), cmdu_rx.getMessageId(), {}};
        for (const auto &tlv : cmdu_rx.getClassList<wfa_map::tlvMultiApPushButtonStatus>()) {
            auto failure_reason = tlv->failure_reason();
            message.statuses.push_back(
                {tlv->bssid(), tlv->pbc_session_id().al_mac, tlv->pbc_session_id().mid,
                 tlv->result(), failure_reason != nullptr,
                 failure_reason ? *failure_reason : uint16_t(0), tlv->enrollee_mac()});
        }
        controller_messages.push_back(message);
        return true;
    }

    int get_ap_manager_fd(const std::string &radio_iface) override
    {
        auto it = ap_manager_fds.find(radio_iface);
        return it == ap_manager_fds.end() ? net::FileDescriptor::invalid_descriptor : it->second;
    }

    bool send_cmdu(int fd, ieee1905_1::CmduMessageTx &cmdu_tx) override
    {
        if (!send_to_ap_manager_result) {
            return false;
        }
        EXPECT_TRUE(cmdu_tx.finalize());
        std::vector<uint8_t> buffer(cmdu_tx.getMessageBuff(),
                                    cmdu_tx.getMessageBuff() + cmdu_tx.getMessageLength());
        ieee1905_1::CmduMessageRx cmdu_rx(buffer.data(), buffer.size());
        EXPECT_TRUE(cmdu_rx.parse());
        auto beerocks_header = message_com::parse_intel_vs_message(cmdu_rx);
        EXPECT_TRUE(beerocks_header);
        if (!beerocks_header) {
            return false;
        }

        sApManagerRequest request{fd, beerocks_header->action_op(), {}};
        switch (request.action_op) {
        case beerocks_message::ACTION_APMANAGER_MESHPBC_START_REQUEST: {
            auto start =
                beerocks_header
                    ->addClass<beerocks_message::cACTION_APMANAGER_MESHPBC_START_REQUEST>();
            EXPECT_TRUE(start);
            if (start) {
                request.bssid = start->bssid();
            }
            break;
        }
        case beerocks_message::ACTION_APMANAGER_MESHPBC_CANCEL_REQUEST: {
            auto cancel =
                beerocks_header
                    ->addClass<beerocks_message::cACTION_APMANAGER_MESHPBC_CANCEL_REQUEST>();
            EXPECT_TRUE(cancel);
            if (cancel) {
                request.bssid = cancel->bssid();
            }
            break;
        }
        default:
            ADD_FAILURE() << "Unexpected ap_manager request " << int(request.action_op);
            break;
        }
        ap_manager_requests.push_back(request);
        return true;
    }

    std::map<std::string, int> ap_manager_fds = {{g_radio_iface, g_ap_manager_fd}};
    bool send_to_ap_manager_result            = true;
    std::vector<sControllerMessage> controller_messages;
    std::vector<sApManagerRequest> ap_manager_requests;
};

struct sTrigger {
    sMacAddr session_al_mac = g_controller_mac;
    uint16_t session_mid    = 0x0101;
    bool dpp                = false;
    bool wps                = true;
    uint8_t state           = 0x01;
    std::vector<sMacAddr> bssids;
};

/**
 * @param extra_octets Octets added after the BSSIDs and counted in the TLV length.
 */
void append_trigger_tlv(std::vector<uint8_t> &octets, const sTrigger &trigger,
                        const std::vector<uint8_t> &extra_octets = {})
{
    const uint16_t length = 11 + 6 * trigger.bssids.size() + extra_octets.size();
    octets.insert(octets.end(), {0xEF, uint8_t(length >> 8), uint8_t(length & 0xFF)});
    octets.insert(octets.end(), trigger.session_al_mac.oct, trigger.session_al_mac.oct + 6);
    octets.insert(octets.end(),
                  {uint8_t(trigger.session_mid >> 8), uint8_t(trigger.session_mid & 0xFF),
                   uint8_t((trigger.wps ? 0x02 : 0x00) | (trigger.dpp ? 0x01 : 0x00)),
                   trigger.state, uint8_t(trigger.bssids.size())});
    for (const auto &bssid : trigger.bssids) {
        octets.insert(octets.end(), bssid.oct, bssid.oct + 6);
    }
    octets.insert(octets.end(), extra_octets.begin(), extra_octets.end());
}

std::vector<uint8_t> trigger_message_header(uint16_t mid)
{
    return {0x00, 0x00, 0x80, 0x4F, uint8_t(mid >> 8), uint8_t(mid & 0xFF), 0x00, 0x80};
}

const std::vector<uint8_t> g_end_of_message = {0x00, 0x00, 0x00};

class PBCOnboardingTaskTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto db                        = AgentDB::get();
        db->controller_info.bridge_mac = g_controller_mac;
        auto radio                     = db->radio(g_radio_iface);
        if (!radio) {
            radio = db->add_radio(g_radio_iface, "");
        }
        ASSERT_NE(radio, nullptr);
        radio->front.iface_mac = g_radio_mac;
        radio->front.bssids    = {};
        set_bss(*radio, 0, g_bssid_1, g_fronthaul_ssid, true, false);
        set_bss(*radio, 1, g_bssid_2, g_fronthaul_ssid_2, true, false);
        set_bss(*radio, 2, g_bssid_backhaul, "backhaul", false, true);

        auto sender = std::make_unique<FakeMessageSender>();
        m_sender    = sender.get();
        m_now       = std::chrono::steady_clock::now();
        m_task      = std::make_unique<PBCOnboardingTask>(std::move(sender), m_cmdu_tx,
                                                     [this]() { return m_now; });
        m_task->set_listen_durations(g_radio_iface, {{g_fronthaul_ssid, g_listen_duration},
                                                     {g_fronthaul_ssid_2, g_listen_duration}});

        el::Helpers::installLogDispatchCallback<LogRecorder>("LogRecorder");
        LogRecorder::lines().clear();
    }

    void TearDown() override
    {
        el::Helpers::uninstallLogDispatchCallback<LogRecorder>("LogRecorder");
        m_task.reset();
        auto db    = AgentDB::get();
        auto radio = db->radio(g_radio_iface);
        if (radio) {
            radio->front.bssids = {};
        }
        db->controller_info.bridge_mac = g_zero_mac;
    }

    static void set_bss(AgentDB::sRadio &radio, size_t index, const sMacAddr &bssid,
                        const std::string &ssid, bool fronthaul, bool backhaul)
    {
        auto &bss         = radio.front.bssids[index];
        bss.iface_name    = std::string(g_radio_iface) + "." + std::to_string(index);
        bss.mac           = bssid;
        bss.ssid          = ssid;
        bss.fronthaul_bss = fronthaul;
        bss.backhaul_bss  = backhaul;
        bss.active        = true;
    }

    bool receive(std::vector<uint8_t> octets, const sMacAddr &src_mac = g_controller_mac)
    {
        ieee1905_1::CmduMessageRx cmdu_rx(octets.data(), octets.size());
        EXPECT_TRUE(cmdu_rx.parse());
        return m_task->handle_cmdu(cmdu_rx, 0, g_radio_mac, src_mac,
                                   net::FileDescriptor::invalid_descriptor, nullptr);
    }

    void receive_trigger(const sTrigger &trigger, uint16_t mid = 0x2001,
                         const sMacAddr &src_mac = g_controller_mac)
    {
        auto octets = trigger_message_header(mid);
        append_trigger_tlv(octets, trigger);
        octets.insert(octets.end(), g_end_of_message.begin(), g_end_of_message.end());
        EXPECT_TRUE(receive(octets, src_mac));
    }

    std::vector<sControllerMessage> status_messages() const
    {
        std::vector<sControllerMessage> messages;
        for (const auto &message : m_sender->controller_messages) {
            if (message.type == ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_STATUS_MESSAGE) {
                messages.push_back(message);
            }
        }
        return messages;
    }

    std::vector<sStatus> statuses() const
    {
        std::vector<sStatus> all;
        for (const auto &message : status_messages()) {
            all.insert(all.end(), message.statuses.begin(), message.statuses.end());
        }
        return all;
    }

    size_t ack_count(uint16_t mid) const
    {
        size_t count = 0;
        for (const auto &message : m_sender->controller_messages) {
            if (message.type == ieee1905_1::eMessageType::ACK_MESSAGE && message.mid == mid) {
                count++;
            }
        }
        return count;
    }

    size_t request_count(uint8_t action_op, const sMacAddr &bssid) const
    {
        size_t count = 0;
        for (const auto &request : m_sender->ap_manager_requests) {
            if (request.action_op == action_op && request.bssid == bssid) {
                count++;
            }
        }
        return count;
    }

    size_t start_count(const sMacAddr &bssid) const
    {
        return request_count(beerocks_message::ACTION_APMANAGER_MESHPBC_START_REQUEST, bssid);
    }

    size_t cancel_count(const sMacAddr &bssid) const
    {
        return request_count(beerocks_message::ACTION_APMANAGER_MESHPBC_CANCEL_REQUEST, bssid);
    }

    void arm(uint16_t session_mid = 0x0101, bool started = true)
    {
        sTrigger trigger;
        trigger.session_mid = session_mid;
        trigger.bssids      = {g_bssid_1};
        receive_trigger(trigger);
        ASSERT_EQ(start_count(g_bssid_1), 1U);
        if (started) {
            m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Started,
                                               "Success", g_zero_mac);
        }
    }

    static void expect_status(const sStatus &status, const sMacAddr &bssid, uint16_t session_mid,
                              eResult result, bool has_failure_reason, uint16_t failure_reason,
                              const sMacAddr &enrollee_mac = g_zero_mac)
    {
        EXPECT_EQ(status.bssid, bssid);
        EXPECT_EQ(status.session_al_mac, g_controller_mac);
        EXPECT_EQ(status.session_mid, session_mid);
        EXPECT_EQ(status.result, result);
        EXPECT_EQ(status.has_failure_reason, has_failure_reason);
        EXPECT_EQ(status.failure_reason, failure_reason);
        EXPECT_EQ(status.enrollee_mac, enrollee_mac);
    }

    void advance_clock(std::chrono::steady_clock::duration duration) { m_now += duration; }

    std::chrono::steady_clock::time_point m_now;
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];
    ieee1905_1::CmduMessageTx m_cmdu_tx{m_tx_buffer, sizeof(m_tx_buffer)};
    FakeMessageSender *m_sender = nullptr;
    std::unique_ptr<PBCOnboardingTask> m_task;
};

// cppcheck-suppress syntaxError
TEST_F(PBCOnboardingTaskTest, handles_only_the_trigger_message)
{
    std::vector<uint8_t> octets = {0x00, 0x00, 0x80, 0x50, 0x00, 0x01, 0x00, 0x80};
    octets.insert(octets.end(), g_end_of_message.begin(), g_end_of_message.end());
    EXPECT_FALSE(receive(octets));
    EXPECT_TRUE(m_sender->controller_messages.empty());
}

TEST_F(PBCOnboardingTaskTest, success_reports_started_then_success_with_enrollee)
{
    arm(0x0101, false);
    EXPECT_EQ(ack_count(0x2001), 1U);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Started,
                                       "Success", g_zero_mac);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Success",
                                       g_enrollee_mac);

    auto messages = status_messages();
    ASSERT_EQ(messages.size(), 2U);
    ASSERT_EQ(messages[0].statuses.size(), 1U);
    expect_status(messages[0].statuses[0], g_bssid_1, 0x0101, eResult::STARTED, false, 0);
    ASSERT_EQ(messages[1].statuses.size(), 1U);
    expect_status(messages[1].statuses[0], g_bssid_1, 0x0101, eResult::SUCCESS, false, 0,
                  g_enrollee_mac);
}

TEST_F(PBCOnboardingTaskTest, end_reasons_map_to_failure_and_wsc_configuration_error)
{
    const std::vector<std::pair<std::string, uint16_t>> reasons = {
        {"Overlap", 12}, {"Locked", 15},       {"Timeout", 0},      {"Canceled", 0},
        {"Failure", 0},  {"UnknownReason", 0}, {"SomethingElse", 0}};
    uint16_t session_mid = 0x0100;
    for (const auto &reason : reasons) {
        SCOPED_TRACE(reason.first);
        m_sender->controller_messages.clear();
        m_sender->ap_manager_requests.clear();
        arm(++session_mid);
        m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended,
                                           reason.first, g_enrollee_mac);
        auto all = statuses();
        ASSERT_EQ(all.size(), 2U);
        expect_status(all[0], g_bssid_1, session_mid, eResult::STARTED, false, 0);
        expect_status(all[1], g_bssid_1, session_mid, eResult::FAILURE_ABORTED, true,
                      reason.second);
    }
}

TEST_F(PBCOnboardingTaskTest, start_failures_map_to_not_started)
{
    const std::vector<std::string> reasons = {"UnknownBSSID", "CallNotSent", "NoStatus", "Busy"};
    uint16_t session_mid                   = 0x0100;
    for (const auto &reason : reasons) {
        SCOPED_TRACE(reason);
        m_sender->controller_messages.clear();
        m_sender->ap_manager_requests.clear();
        arm(++session_mid, false);
        m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_NotStarted,
                                           reason, g_zero_mac);
        auto all = statuses();
        ASSERT_EQ(all.size(), 1U);
        expect_status(all[0], g_bssid_1, session_mid, eResult::NOT_STARTED, true, 0);
    }
}

TEST_F(PBCOnboardingTaskTest, end_without_started_reports_started_in_its_own_message)
{
    arm(0x0101, false);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Timeout",
                                       g_zero_mac);

    auto messages = status_messages();
    ASSERT_EQ(messages.size(), 2U);
    ASSERT_EQ(messages[0].statuses.size(), 1U);
    expect_status(messages[0].statuses[0], g_bssid_1, 0x0101, eResult::STARTED, false, 0);
    ASSERT_EQ(messages[1].statuses.size(), 1U);
    expect_status(messages[1].statuses[0], g_bssid_1, 0x0101, eResult::FAILURE_ABORTED, true, 0);
}

TEST_F(PBCOnboardingTaskTest, at_most_two_reports_per_armed_bss)
{
    arm();
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Started,
                                       "Success", g_zero_mac);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Timeout",
                                       g_zero_mac);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Success",
                                       g_enrollee_mac);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_NotStarted,
                                       "Busy", g_zero_mac);

    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    EXPECT_EQ(all[0].result, eResult::STARTED);
    EXPECT_EQ(all[1].result, eResult::FAILURE_ABORTED);
}

TEST_F(PBCOnboardingTaskTest, status_without_session_is_ignored)
{
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Success",
                                       g_enrollee_mac);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus(0x7F), "",
                                       g_zero_mac);
    EXPECT_TRUE(m_sender->controller_messages.empty());
}

TEST_F(PBCOnboardingTaskTest, unknown_status_value_is_ignored)
{
    arm();
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus(0x7F), "",
                                       g_zero_mac);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Success",
                                       g_enrollee_mac);
    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    EXPECT_EQ(all[1].result, eResult::SUCCESS);
}

TEST_F(PBCOnboardingTaskTest, rejected_triggers_get_an_ack_and_no_status)
{
    sTrigger one_bssid;
    one_bssid.bssids = {g_bssid_1};

    sTrigger no_bssid;
    receive_trigger(no_bssid, 0x3001);

    auto octets = trigger_message_header(0x3002);
    append_trigger_tlv(octets, one_bssid, {0x00});
    octets.insert(octets.end(), g_end_of_message.begin(), g_end_of_message.end());
    EXPECT_TRUE(receive(octets));

    sTrigger unknown_state = one_bssid;
    unknown_state.state    = 0x02;
    receive_trigger(unknown_state, 0x3003);

    octets = trigger_message_header(0x3004);
    octets.insert(octets.end(), g_end_of_message.begin(), g_end_of_message.end());
    EXPECT_TRUE(receive(octets));

    octets = trigger_message_header(0x3005);
    append_trigger_tlv(octets, one_bssid);
    append_trigger_tlv(octets, one_bssid);
    octets.insert(octets.end(), g_end_of_message.begin(), g_end_of_message.end());
    EXPECT_TRUE(receive(octets));

    for (uint16_t mid = 0x3001; mid <= 0x3005; mid++) {
        EXPECT_EQ(ack_count(mid), 1U) << "MID " << std::hex << mid;
    }
    EXPECT_TRUE(status_messages().empty());
    EXPECT_TRUE(m_sender->ap_manager_requests.empty());

    receive_trigger(one_bssid, 0x3006);
    EXPECT_EQ(start_count(g_bssid_1), 1U);
}

TEST_F(PBCOnboardingTaskTest, trigger_not_from_the_controller_is_ignored)
{
    sTrigger trigger;
    trigger.bssids = {g_bssid_1};
    receive_trigger(trigger, 0x3001, g_other_mac);
    EXPECT_TRUE(m_sender->controller_messages.empty());
    EXPECT_TRUE(m_sender->ap_manager_requests.empty());
}

TEST_F(PBCOnboardingTaskTest, bssid_listed_twice_is_used_once)
{
    sTrigger trigger;
    trigger.bssids = {g_bssid_1, g_bssid_1};
    receive_trigger(trigger);
    EXPECT_EQ(start_count(g_bssid_1), 1U);
    EXPECT_TRUE(status_messages().empty());
}

TEST_F(PBCOnboardingTaskTest, start_with_dpp_only_reports_not_started_per_bssid)
{
    sTrigger trigger;
    trigger.dpp    = true;
    trigger.wps    = false;
    trigger.bssids = {g_bssid_1, g_bssid_2};
    receive_trigger(trigger);

    EXPECT_TRUE(m_sender->ap_manager_requests.empty());
    auto messages = status_messages();
    ASSERT_EQ(messages.size(), 1U);
    ASSERT_EQ(messages[0].statuses.size(), 2U);
    expect_status(messages[0].statuses[0], g_bssid_1, 0x0101, eResult::NOT_STARTED, true, 0);
    expect_status(messages[0].statuses[1], g_bssid_2, 0x0101, eResult::NOT_STARTED, true, 0);
}

TEST_F(PBCOnboardingTaskTest, start_on_bssid_that_cannot_be_armed_reports_not_started)
{
    sTrigger trigger;
    trigger.bssids = {g_bssid_unknown, g_zero_mac, g_bssid_backhaul};
    receive_trigger(trigger);

    EXPECT_TRUE(m_sender->ap_manager_requests.empty());
    auto all = statuses();
    ASSERT_EQ(all.size(), 3U);
    expect_status(all[0], g_bssid_unknown, 0x0101, eResult::NOT_STARTED, true, 0);
    expect_status(all[1], g_zero_mac, 0x0101, eResult::NOT_STARTED, true, 0);
    expect_status(all[2], g_bssid_backhaul, 0x0101, eResult::NOT_STARTED, true, 0);
}

TEST_F(PBCOnboardingTaskTest, start_without_ap_manager_reports_not_started)
{
    sTrigger trigger;
    trigger.bssids = {g_bssid_1};
    m_sender->ap_manager_fds.clear();
    receive_trigger(trigger, 0x2001);

    m_sender->ap_manager_fds[g_radio_iface] = g_ap_manager_fd;
    m_sender->send_to_ap_manager_result     = false;
    trigger.session_mid                     = 0x0102;
    receive_trigger(trigger, 0x2002);

    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    expect_status(all[0], g_bssid_1, 0x0101, eResult::NOT_STARTED, true, 0);
    expect_status(all[1], g_bssid_1, 0x0102, eResult::NOT_STARTED, true, 0);
}

TEST_F(PBCOnboardingTaskTest, stop_without_wps_or_session_changes_nothing)
{
    arm();
    sTrigger stop;
    stop.state       = 0x00;
    stop.session_mid = 0x0102;
    stop.wps         = false;
    stop.dpp         = true;
    stop.bssids      = {g_bssid_1};
    receive_trigger(stop, 0x2002);
    EXPECT_EQ(cancel_count(g_bssid_1), 0U);

    stop.wps    = true;
    stop.bssids = {g_bssid_2};
    receive_trigger(stop, 0x2003);
    EXPECT_EQ(cancel_count(g_bssid_2), 0U);

    auto all = statuses();
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].result, eResult::STARTED);
    EXPECT_EQ(ack_count(0x2002), 1U);
    EXPECT_EQ(ack_count(0x2003), 1U);
}

TEST_F(PBCOnboardingTaskTest, stop_with_other_session_id_changes_nothing)
{
    arm(0x0101);
    sTrigger stop;
    stop.state       = 0x00;
    stop.session_mid = 0x0202;
    stop.bssids      = {g_bssid_1};
    receive_trigger(stop, 0x2002);
    EXPECT_EQ(ack_count(0x2002), 1U);
    EXPECT_EQ(cancel_count(g_bssid_1), 0U);
    EXPECT_EQ(statuses().size(), 1U);

    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Success",
                                       g_enrollee_mac);
    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    expect_status(all[1], g_bssid_1, 0x0101, eResult::SUCCESS, false, 0, g_enrollee_mac);
}

TEST_F(PBCOnboardingTaskTest, stop_with_arming_session_id_cancels)
{
    arm(0x0101);
    sTrigger stop;
    stop.state       = 0x00;
    stop.session_mid = 0x0101;
    stop.bssids      = {g_bssid_1};
    receive_trigger(stop, 0x2002);
    EXPECT_EQ(cancel_count(g_bssid_1), 1U);
    EXPECT_EQ(statuses().size(), 1U);

    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended,
                                       "Canceled", g_zero_mac);
    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    expect_status(all[1], g_bssid_1, 0x0101, eResult::FAILURE_ABORTED, true, 0);
}

TEST_F(PBCOnboardingTaskTest, stop_before_started_cancels_again_after_the_start)
{
    arm(0x0101, false);
    sTrigger stop;
    stop.state  = 0x00;
    stop.bssids = {g_bssid_1};
    receive_trigger(stop, 0x2002);
    EXPECT_EQ(cancel_count(g_bssid_1), 1U);

    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Started,
                                       "Success", g_zero_mac);
    EXPECT_EQ(cancel_count(g_bssid_1), 2U);
    auto all = statuses();
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].result, eResult::STARTED);
}

TEST_F(PBCOnboardingTaskTest, stop_without_ap_manager_keeps_the_session)
{
    arm();
    m_sender->ap_manager_fds.clear();
    sTrigger stop;
    stop.state  = 0x00;
    stop.bssids = {g_bssid_1};
    receive_trigger(stop, 0x2002);
    EXPECT_EQ(cancel_count(g_bssid_1), 0U);

    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Timeout",
                                       g_zero_mac);
    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    EXPECT_EQ(all[1].result, eResult::FAILURE_ABORTED);
}

TEST_F(PBCOnboardingTaskTest, same_session_id_on_armed_bssid_is_a_duplicate)
{
    arm(0x0101);
    sTrigger trigger;
    trigger.session_mid = 0x0101;
    trigger.bssids      = {g_bssid_1};
    receive_trigger(trigger, 0x2002);

    EXPECT_EQ(ack_count(0x2002), 1U);
    EXPECT_EQ(start_count(g_bssid_1), 1U);
    EXPECT_EQ(statuses().size(), 1U);
}

TEST_F(PBCOnboardingTaskTest, same_session_id_after_the_end_is_a_duplicate)
{
    arm(0x0101);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Timeout",
                                       g_zero_mac);
    sTrigger trigger;
    trigger.session_mid = 0x0101;
    trigger.bssids      = {g_bssid_1};
    receive_trigger(trigger, 0x2002);

    EXPECT_EQ(start_count(g_bssid_1), 1U);
    EXPECT_EQ(statuses().size(), 2U);

    trigger.session_mid = 0x0102;
    trigger.wps         = false;
    trigger.dpp         = true;
    receive_trigger(trigger, 0x2003);
    receive_trigger(trigger, 0x2004);
    EXPECT_EQ(statuses().size(), 3U);
}

TEST_F(PBCOnboardingTaskTest, other_session_id_on_armed_bssid_reports_device_busy)
{
    arm(0x0101);
    sTrigger trigger;
    trigger.session_mid = 0x0202;
    trigger.bssids      = {g_bssid_1};
    receive_trigger(trigger, 0x2002);

    EXPECT_EQ(start_count(g_bssid_1), 1U);
    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    expect_status(all[1], g_bssid_1, 0x0202, eResult::NOT_STARTED, true, 14);

    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Success",
                                       g_enrollee_mac);
    all = statuses();
    ASSERT_EQ(all.size(), 3U);
    expect_status(all[2], g_bssid_1, 0x0101, eResult::SUCCESS, false, 0, g_enrollee_mac);
}

TEST_F(PBCOnboardingTaskTest, earlier_session_id_is_handled_again_after_another_one)
{
    arm(0x0101);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Timeout",
                                       g_zero_mac);
    sTrigger trigger;
    trigger.session_mid = 0x0102;
    trigger.bssids      = {g_bssid_1};
    receive_trigger(trigger, 0x2002);
    EXPECT_EQ(start_count(g_bssid_1), 2U);
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended, "Timeout",
                                       g_zero_mac);

    trigger.session_mid = 0x0101;
    receive_trigger(trigger, 0x2003);
    EXPECT_EQ(start_count(g_bssid_1), 3U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "duplicate Trigger ignored"), 0U);
}

TEST_F(PBCOnboardingTaskTest, running_session_id_after_a_busy_answer_is_a_duplicate)
{
    arm(0x0101);
    sTrigger trigger;
    trigger.session_mid = 0x0202;
    trigger.bssids      = {g_bssid_1};
    receive_trigger(trigger, 0x2002);
    ASSERT_EQ(statuses().size(), 2U);

    trigger.session_mid = 0x0101;
    receive_trigger(trigger, 0x2003);
    EXPECT_EQ(ack_count(0x2003), 1U);
    EXPECT_EQ(start_count(g_bssid_1), 1U);
    EXPECT_EQ(statuses().size(), 2U);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "duplicate Trigger ignored"), 1U);
}

TEST_F(PBCOnboardingTaskTest, session_without_start_in_time_is_cancelled)
{
    arm(0x0101, false);
    advance_clock(std::chrono::seconds(45) - std::chrono::milliseconds(1));
    m_task->work();
    EXPECT_TRUE(status_messages().empty());

    advance_clock(std::chrono::milliseconds(1));
    m_task->work();
    EXPECT_EQ(cancel_count(g_bssid_1), 1U);
    auto all = statuses();
    ASSERT_EQ(all.size(), 1U);
    expect_status(all[0], g_bssid_1, 0x0101, eResult::NOT_STARTED, true, 0);
    EXPECT_EQ(LogRecorder::count(el::Level::Error, "ended: no start status in time"), 1U);
}

TEST_F(PBCOnboardingTaskTest, started_session_ends_at_the_listen_duration)
{
    arm(0x0101);
    advance_clock(std::chrono::seconds(g_listen_duration) - std::chrono::milliseconds(1));
    m_task->work();
    EXPECT_EQ(statuses().size(), 1U);
    EXPECT_EQ(cancel_count(g_bssid_1), 0U);

    advance_clock(std::chrono::milliseconds(1));
    m_task->work();
    EXPECT_EQ(cancel_count(g_bssid_1), 1U);
    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    expect_status(all[1], g_bssid_1, 0x0101, eResult::FAILURE_ABORTED, true, 0);
    EXPECT_EQ(LogRecorder::count(el::Level::Info, "ended: Listen Duration expired"), 1U);
    EXPECT_EQ(LogRecorder::count(el::Level::Error, "ended: Listen Duration expired"), 0U);

    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Ended,
                                       "Canceled", g_zero_mac);
    EXPECT_EQ(statuses().size(), 2U);
}

TEST_F(PBCOnboardingTaskTest, listen_duration_of_the_ssid_at_the_start_is_used)
{
    m_task->set_listen_durations(g_radio_iface, {{g_fronthaul_ssid, 10}});
    arm(0x0101, false);
    m_task->set_listen_durations(g_radio_iface, {{g_fronthaul_ssid, 300}});
    advance_clock(std::chrono::seconds(5));
    m_task->handle_status_notification(g_bssid_1, beerocks_message::eMeshPbcStatus_Started,
                                       "Success", g_zero_mac);

    advance_clock(std::chrono::seconds(10) - std::chrono::milliseconds(1));
    m_task->work();
    EXPECT_EQ(statuses().size(), 1U);
    advance_clock(std::chrono::milliseconds(1));
    m_task->work();
    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    expect_status(all[1], g_bssid_1, 0x0101, eResult::FAILURE_ABORTED, true, 0);
}

TEST_F(PBCOnboardingTaskTest, start_without_listen_duration_reports_not_started)
{
    m_task->set_listen_durations(g_radio_iface, {{g_fronthaul_ssid_2, g_listen_duration}});
    sTrigger trigger;
    trigger.bssids = {g_bssid_1};
    receive_trigger(trigger);

    EXPECT_TRUE(m_sender->ap_manager_requests.empty());
    auto all = statuses();
    ASSERT_EQ(all.size(), 1U);
    expect_status(all[0], g_bssid_1, 0x0101, eResult::NOT_STARTED, true, 0);
}

TEST_F(PBCOnboardingTaskTest, start_on_ssid_of_two_fronthaul_bsss_reports_not_started)
{
    auto radio = AgentDB::get()->radio(g_radio_iface);
    ASSERT_NE(radio, nullptr);
    set_bss(*radio, 1, g_bssid_2, g_fronthaul_ssid, true, false);
    sTrigger trigger;
    trigger.bssids = {g_bssid_1, g_bssid_2};
    receive_trigger(trigger);

    EXPECT_TRUE(m_sender->ap_manager_requests.empty());
    auto all = statuses();
    ASSERT_EQ(all.size(), 2U);
    expect_status(all[0], g_bssid_1, 0x0101, eResult::NOT_STARTED, true, 0);
    expect_status(all[1], g_bssid_2, 0x0101, eResult::NOT_STARTED, true, 0);

    radio->front.bssids[1].active = false;
    trigger.session_mid           = 0x0102;
    trigger.bssids                = {g_bssid_1};
    receive_trigger(trigger, 0x2002);
    EXPECT_EQ(start_count(g_bssid_1), 1U);
}

TEST_F(PBCOnboardingTaskTest, set_listen_durations_replaces_only_the_values_of_the_radio)
{
    m_task->set_listen_durations("wlan9", {{g_fronthaul_ssid, 30}});
    m_task->set_listen_durations("wlan9", {});
    m_task->set_listen_durations(g_radio_iface, {{g_fronthaul_ssid_2, g_listen_duration}});
    m_task->set_listen_durations("wlan9", {{g_fronthaul_ssid, 30}});

    sTrigger trigger;
    trigger.bssids = {g_bssid_1, g_bssid_2};
    receive_trigger(trigger);
    EXPECT_EQ(start_count(g_bssid_1), 0U);
    EXPECT_EQ(start_count(g_bssid_2), 1U);
    auto all = statuses();
    ASSERT_EQ(all.size(), 1U);
    expect_status(all[0], g_bssid_1, 0x0101, eResult::NOT_STARTED, true, 0);
}

TEST_F(PBCOnboardingTaskTest, ssid_given_twice_keeps_the_first_listen_duration)
{
    m_task->set_listen_durations(g_radio_iface, {{g_fronthaul_ssid, 10}, {g_fronthaul_ssid, 300}});
    EXPECT_EQ(LogRecorder::count(el::Level::Warning, "given twice"), 1U);
    arm(0x0101);
    advance_clock(std::chrono::seconds(10));
    m_task->work();
    EXPECT_EQ(statuses().size(), 2U);
}

TEST_F(PBCOnboardingTaskTest, ap_manager_disconnect_ends_the_sessions_of_the_radio)
{
    arm(0x0101);
    sTrigger trigger;
    trigger.session_mid = 0x0102;
    trigger.bssids      = {g_bssid_2};
    receive_trigger(trigger, 0x2002);

    m_task->handle_ap_manager_disconnected("wlan9");
    EXPECT_EQ(statuses().size(), 1U);

    m_task->handle_ap_manager_disconnected(g_radio_iface);
    auto all = statuses();
    ASSERT_EQ(all.size(), 3U);
    std::map<std::string, eResult> results;
    for (size_t i = 1; i < all.size(); i++) {
        results[tlvf::mac_to_string(all[i].bssid)] = all[i].result;
    }
    EXPECT_EQ(results[tlvf::mac_to_string(g_bssid_1)], eResult::FAILURE_ABORTED);
    EXPECT_EQ(results[tlvf::mac_to_string(g_bssid_2)], eResult::NOT_STARTED);
    EXPECT_EQ(cancel_count(g_bssid_1), 0U);

    trigger.session_mid = 0x0103;
    trigger.bssids      = {g_bssid_1};
    receive_trigger(trigger, 0x2003);
    EXPECT_EQ(start_count(g_bssid_1), 1U);
    all = statuses();
    ASSERT_EQ(all.size(), 4U);
    expect_status(all[3], g_bssid_1, 0x0103, eResult::NOT_STARTED, true, 0);
}

struct sConfigTlv {
    sMacAddr bssid              = g_zero_mac;
    uint8_t bss_index           = 1;
    uint16_t listen_duration    = g_listen_duration;
    std::vector<uint8_t> uuid_e = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    uint16_t device_password_id = 0x0004;
    std::string device_password;
};

std::vector<PBCOnboardingTask::sPushButtonConfig> read_configs(const std::vector<sConfigTlv> &tlvs)
{
    uint8_t buffer[2048] = {};
    ieee1905_1::CmduMessageTx cmdu_tx(buffer, sizeof(buffer));
    EXPECT_TRUE(cmdu_tx.create(0x4001, ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE));
    for (const auto &config : tlvs) {
        auto tlv = cmdu_tx.addClass<wfa_map::tlvPushButtonConfig>();
        EXPECT_TRUE(tlv);
        if (!tlv) {
            return {};
        }
        tlv->bssid()              = config.bssid;
        tlv->bss_index()          = config.bss_index;
        tlv->listen_duration()    = config.listen_duration;
        tlv->device_password_id() = config.device_password_id;
        EXPECT_TRUE(tlv->set_uuid_e(config.uuid_e.data(), config.uuid_e.size()));
        if (!config.device_password.empty()) {
            EXPECT_TRUE(tlv->set_device_password(config.device_password.data(),
                                                 config.device_password.size()));
        }
    }
    EXPECT_TRUE(cmdu_tx.finalize());

    ieee1905_1::CmduMessageRx cmdu_rx(buffer, cmdu_tx.getMessageLength());
    EXPECT_TRUE(cmdu_rx.parse());
    return PBCOnboardingTask::read_push_button_configs(g_radio_iface, cmdu_rx);
}

TEST(PBCOnboardingTaskReadConfigsTest, valid_tlvs_are_returned_in_message_order)
{
    sConfigTlv first;
    first.bss_index       = 2;
    first.listen_duration = 10;
    sConfigTlv second;
    second.bss_index          = 1;
    second.listen_duration    = 65535;
    second.device_password_id = 0x0000;
    second.device_password    = "12345670";
    second.uuid_e             = std::vector<uint8_t>(16, 0xA5);

    auto configs = read_configs({first, second});
    ASSERT_EQ(configs.size(), 2U);
    EXPECT_EQ(configs[0].bss_index, 2);
    EXPECT_EQ(configs[0].uuid_e, "12345678-9abc-def0-0123-456789abcdef");
    EXPECT_EQ(configs[0].device_password_id, 0x0004);
    EXPECT_EQ(configs[0].device_password, "");
    EXPECT_EQ(configs[0].listen_duration, 10);
    EXPECT_EQ(configs[1].bss_index, 1);
    EXPECT_EQ(configs[1].uuid_e, "a5a5a5a5-a5a5-a5a5-a5a5-a5a5a5a5a5a5");
    EXPECT_EQ(configs[1].device_password_id, 0x0000);
    EXPECT_EQ(configs[1].device_password, "12345670");
    EXPECT_EQ(configs[1].listen_duration, 65535);
}

TEST(PBCOnboardingTaskReadConfigsTest, device_password_of_63_octets_is_accepted)
{
    sConfigTlv config;
    config.device_password_id = 0x0000;
    config.device_password    = std::string(62, 'a') + "~";
    auto configs              = read_configs({config});
    ASSERT_EQ(configs.size(), 1U);
    EXPECT_EQ(configs[0].device_password, config.device_password);
}

TEST(PBCOnboardingTaskReadConfigsTest, invalid_tlvs_are_skipped)
{
    sConfigTlv bssid_not_zero;
    bssid_not_zero.bssid = g_bssid_1;
    sConfigTlv bss_index_zero;
    bss_index_zero.bss_index = 0;
    sConfigTlv listen_duration_zero;
    listen_duration_zero.listen_duration = 0;
    sConfigTlv password_too_long;
    password_too_long.device_password_id = 0x0000;
    password_too_long.device_password    = std::string(64, 'a');
    sConfigTlv password_control;
    password_control.device_password_id = 0x0000;
    password_control.device_password    = std::string("1234\n5670");
    sConfigTlv password_non_ascii;
    password_non_ascii.device_password_id = 0x0000;
    password_non_ascii.device_password    = std::string("1234\x7f");
    sConfigTlv valid;
    valid.bss_index = 7;

    auto configs = read_configs({bssid_not_zero, bss_index_zero, listen_duration_zero,
                                 password_too_long, password_control, password_non_ascii, valid});
    ASSERT_EQ(configs.size(), 1U);
    EXPECT_EQ(configs[0].bss_index, 7);
}

TEST(PBCOnboardingTaskReadConfigsTest, message_without_tlv_gives_no_config)
{
    EXPECT_TRUE(read_configs({}).empty());
}

} // namespace

} // namespace beerocks
