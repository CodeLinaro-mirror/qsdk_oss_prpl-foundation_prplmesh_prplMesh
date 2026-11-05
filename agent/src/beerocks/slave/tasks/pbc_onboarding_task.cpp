/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "pbc_onboarding_task.h"
#include "../agent_db.h"

#include <bcl/network/file_descriptor.h>
#include <bcl/network/network_utils.h>
#include <beerocks/tlvf/beerocks_message.h>
#include <beerocks/tlvf/beerocks_message_apmanager.h>
#include <tlvf/wfa_map/tlvMultiApPushButtonStatus.h>
#include <tlvf/wfa_map/tlvMultiApPushButtonTrigger.h>
#include <tlvf/wfa_map/tlvPushButtonConfig.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace beerocks {

// pwhm defers InitiateWPSPBC() while the radio reloads: it retries after 1 s, then every 10 s.
// The Ambiorix call that starts it times out after 3 s.
static constexpr auto session_start_limit = std::chrono::seconds(45);

PBCOnboardingTask::PBCOnboardingTask(std::unique_ptr<MessageSender> sender,
                                     ieee1905_1::CmduMessageTx &cmdu_tx, now_f now)
    : Task(eTaskType::MESH_PBC_ONBOARDING), m_sender(std::move(sender)), m_cmdu_tx(cmdu_tx),
      m_now(std::move(now))
{
}

using eResult = wfa_map::tlvMultiApPushButtonStatus::eResult;

static constexpr uint16_t WSC_CONFIG_ERROR_NO_ERROR              = 0;
static constexpr uint16_t WSC_CONFIG_ERROR_MULTIPLE_PBC_SESSIONS = 12;
static constexpr uint16_t WSC_CONFIG_ERROR_DEVICE_BUSY           = 14;
static constexpr uint16_t WSC_CONFIG_ERROR_SETUP_LOCKED          = 15;

static bool same_session_id(const wfa_map::sPbcSessionId &a, const wfa_map::sPbcSessionId &b)
{
    return a.al_mac == b.al_mac && a.mid == b.mid;
}

static std::string session_id_to_string(const wfa_map::sPbcSessionId &session_id)
{
    std::stringstream text;
    text << session_id.al_mac << "/0x" << std::hex << std::setw(4) << std::setfill('0')
         << session_id.mid;
    return text.str();
}

/**
 * @brief Gets the WSC Configuration Error of a session that ended without success.
 *
 * @param reason Reason of the end of the session: the pwhm pairingDone reason, or an Agent reason.
 * @return The WSC Configuration Error, or WSC_CONFIG_ERROR_NO_ERROR if no code applies.
 */
static uint16_t end_reason_to_wsc_config_error(const std::string &reason)
{
    if (reason == "Overlap") {
        return WSC_CONFIG_ERROR_MULTIPLE_PBC_SESSIONS;
    }
    if (reason == "Locked") {
        return WSC_CONFIG_ERROR_SETUP_LOCKED;
    }
    return WSC_CONFIG_ERROR_NO_ERROR;
}

bool PBCOnboardingTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                    const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                                    std::shared_ptr<beerocks_header> beerocks_header)
{
    if (cmdu_rx.getMessageType() !=
        ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_TRIGGER_MESSAGE) {
        return false;
    }
    handle_trigger(cmdu_rx, src_mac);
    return true;
}

void PBCOnboardingTask::handle_trigger(ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &src_mac)
{
    auto mid = cmdu_rx.getMessageId();
    std::stringstream trigger_name;
    trigger_name << "Multi-AP Push-Button Trigger mid=0x" << std::hex << mid << std::dec << " from "
                 << src_mac;

    {
        auto db = AgentDB::get();
        if (src_mac != db->controller_info.bridge_mac) {
            LOG(ERROR) << trigger_name.str() << " ignored: not from the Controller "
                       << db->controller_info.bridge_mac;
            return;
        }
    }

    if (!m_cmdu_tx.create(mid, ieee1905_1::eMessageType::ACK_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type ACK_MESSAGE, has failed";
    } else {
        LOG(DEBUG) << "Sending ACK message for the " << trigger_name.str();
        m_sender->send_cmdu_to_controller(m_cmdu_tx);
    }

    auto trigger_tlvs = cmdu_rx.getClassList<wfa_map::tlvMultiApPushButtonTrigger>();
    if (trigger_tlvs.size() != 1) {
        LOG(ERROR) << trigger_name.str() << " rejected: " << trigger_tlvs.size()
                   << " Multi-AP Push-Button Trigger TLVs, expected 1";
        return;
    }
    auto trigger = trigger_tlvs.front();

    const auto num_bssid = trigger->bssid_list_length();
    const size_t expected_length =
        sizeof(wfa_map::sPbcSessionId) + 3 * sizeof(uint8_t) + num_bssid * sizeof(sMacAddr);
    if (trigger->length() != expected_length) {
        LOG(ERROR) << trigger_name.str() << " rejected: TLV length " << trigger->length()
                   << " does not match Num_BSSID " << int(num_bssid);
        return;
    }
    if (num_bssid == 0) {
        LOG(ERROR) << trigger_name.str() << " rejected: Num_BSSID is 0";
        return;
    }
    const auto state = trigger->state();
    if (state != wfa_map::tlvMultiApPushButtonTrigger::eState::START &&
        state != wfa_map::tlvMultiApPushButtonTrigger::eState::STOP) {
        LOG(ERROR) << trigger_name.str() << " rejected: unknown State " << int(state);
        return;
    }

    std::vector<sMacAddr> bssids;
    for (size_t i = 0; i < num_bssid; i++) {
        auto entry = trigger->bssid_list(i);
        if (!std::get<0>(entry)) {
            LOG(ERROR) << trigger_name.str() << " rejected: BSSID " << i << " cannot be read";
            return;
        }
        const auto &bssid = std::get<1>(entry);
        if (std::find(bssids.begin(), bssids.end(), bssid) != bssids.end()) {
            LOG(WARNING) << trigger_name.str() << ": BSSID " << bssid << " listed twice";
            continue;
        }
        bssids.push_back(bssid);
    }

    const auto session_id = trigger->pbc_session_id();
    const bool wps        = trigger->push_button_method().wps;
    LOG(INFO) << trigger_name.str() << ": PBC Session ID " << session_id_to_string(session_id)
              << ", method DPP=" << int(trigger->push_button_method().dpp) << " WPS=" << int(wps)
              << ", State=" << state << ", Num_BSSID=" << int(num_bssid);

    std::vector<sStatusReport> reports;
    for (const auto &bssid : bssids) {
        if (state == wfa_map::tlvMultiApPushButtonTrigger::eState::START) {
            start_session(bssid, session_id, wps, reports);
        } else {
            if (!wps) {
                LOG(INFO) << "Mesh PBC stop on " << bssid
                          << " ignored: the Trigger has no WPS method";
                continue;
            }
            stop_session(bssid, session_id);
        }
    }

    if (!reports.empty()) {
        send_status_reports(reports);
    }
}

void PBCOnboardingTask::start_session(const sMacAddr &bssid,
                                      const wfa_map::sPbcSessionId &session_id, bool wps,
                                      std::vector<sStatusReport> &reports)
{
    auto bssid_str = tlvf::mac_to_string(bssid);

    auto last_it    = m_last_session_ids.find(bssid_str);
    auto session_it = m_sessions.find(bssid_str);
    if ((last_it != m_last_session_ids.end() && same_session_id(last_it->second, session_id)) ||
        (session_it != m_sessions.end() &&
         same_session_id(session_it->second.session_id, session_id))) {
        LOG(INFO) << "Mesh PBC start on " << bssid << ": PBC Session ID "
                  << session_id_to_string(session_id)
                  << " already has an answer for this BSSID, duplicate Trigger ignored";
        return;
    }
    m_last_session_ids[bssid_str] = session_id;

    if (!wps) {
        LOG(INFO) << "Mesh PBC start on " << bssid << " not started: the Trigger has no WPS method";
        reports.push_back({bssid, session_id, eResult::NOT_STARTED, WSC_CONFIG_ERROR_NO_ERROR,
                           net::network_utils::ZERO_MAC});
        return;
    }

    if (session_it != m_sessions.end()) {
        LOG(ERROR) << "Mesh PBC start on " << bssid << " for PBC Session ID "
                   << session_id_to_string(session_id)
                   << " not started: the session of PBC Session ID "
                   << session_id_to_string(session_it->second.session_id) << " runs";
        reports.push_back({bssid, session_id, eResult::NOT_STARTED, WSC_CONFIG_ERROR_DEVICE_BUSY,
                           net::network_utils::ZERO_MAC});
        return;
    }

    auto not_started = [&](const std::string &reason) {
        LOG(ERROR) << "Mesh PBC start on " << bssid << " not started: " << reason;
        reports.push_back({bssid, session_id, eResult::NOT_STARTED, WSC_CONFIG_ERROR_NO_ERROR,
                           net::network_utils::ZERO_MAC});
    };

    std::string radio_iface;
    std::string ssid;
    {
        auto db    = AgentDB::get();
        auto radio = db->get_radio_by_mac(bssid, AgentDB::eMacType::BSSID);
        if (!radio || bssid == net::network_utils::ZERO_MAC) {
            not_started("unknown BSSID");
            return;
        }
        auto bss_it = std::find_if(
            radio->front.bssids.begin(), radio->front.bssids.end(),
            [&](const AgentDB::sRadio::sFront::sBssid &bss) { return bss.mac == bssid; });
        if (bss_it == radio->front.bssids.end() || !bss_it->fronthaul_bss) {
            not_started("not a fronthaul BSS");
            return;
        }
        radio_iface = radio->front.iface_name;
        ssid        = bss_it->ssid;

        auto same_ssid = std::count_if(radio->front.bssids.begin(), radio->front.bssids.end(),
                                       [&](const AgentDB::sRadio::sFront::sBssid &bss) {
                                           return bss.fronthaul_bss &&
                                                  (bss.active || bss.mac == bssid) &&
                                                  bss.ssid == ssid;
                                       });
        if (same_ssid > 1) {
            not_started("SSID '" + ssid + "' is used by " + std::to_string(same_ssid) +
                        " fronthaul BSSs of radio " + radio_iface);
            return;
        }
    }

    auto listen_duration_it = m_listen_durations.find({radio_iface, ssid});
    if (listen_duration_it == m_listen_durations.end()) {
        not_started("no push-button configuration with Listen Duration for SSID '" + ssid +
                    "' in the last WSC message for radio " + radio_iface);
        return;
    }
    const auto listen_duration = std::chrono::seconds(listen_duration_it->second);

    auto ap_manager_fd = m_sender->get_ap_manager_fd(radio_iface);
    if (ap_manager_fd == net::FileDescriptor::invalid_descriptor) {
        not_started("ap_manager of " + radio_iface + " unavailable");
        return;
    }

    auto request =
        message_com::create_vs_message<beerocks_message::cACTION_APMANAGER_MESHPBC_START_REQUEST>(
            m_cmdu_tx);
    if (!request) {
        LOG(ERROR) << "Failed building cACTION_APMANAGER_MESHPBC_START_REQUEST";
        not_started("internal error");
        return;
    }
    request->bssid() = bssid;

    if (!m_sender->send_cmdu(ap_manager_fd, m_cmdu_tx)) {
        not_started("MESHPBC_START_REQUEST not sent");
        return;
    }

    auto &session           = m_sessions[bssid_str];
    session.radio_iface     = radio_iface;
    session.session_id      = session_id;
    session.started         = false;
    session.deadline        = m_now() + session_start_limit;
    session.listen_duration = listen_duration;
    LOG(INFO) << "Mesh PBC start request sent for " << bssid << " (radio " << radio_iface
              << ", PBC Session ID " << session_id_to_string(session_id) << ")";
}

void PBCOnboardingTask::stop_session(const sMacAddr &bssid,
                                     const wfa_map::sPbcSessionId &session_id)
{
    auto session_it = m_sessions.find(tlvf::mac_to_string(bssid));
    if (session_it == m_sessions.end()) {
        LOG(INFO) << "Mesh PBC stop on " << bssid << ": no session of this Agent, nothing changed";
        return;
    }

    auto &session = session_it->second;
    if (!same_session_id(session.session_id, session_id)) {
        LOG(INFO) << "Mesh PBC stop on " << bssid << " ignored: PBC Session ID "
                  << session_id_to_string(session_id) << ", running "
                  << session_id_to_string(session.session_id);
        return;
    }

    session.cancel_requested = true;
    if (!send_cancel_request(bssid, session.radio_iface)) {
        LOG(ERROR) << "Mesh PBC stop on " << bssid << ": cancel request not sent";
    }
}

bool PBCOnboardingTask::send_cancel_request(const sMacAddr &bssid, const std::string &radio_iface)
{
    auto ap_manager_fd = m_sender->get_ap_manager_fd(radio_iface);
    if (ap_manager_fd == net::FileDescriptor::invalid_descriptor) {
        LOG(ERROR) << "Mesh PBC cancel on " << bssid << ": ap_manager of " << radio_iface
                   << " unavailable";
        return false;
    }

    auto request =
        message_com::create_vs_message<beerocks_message::cACTION_APMANAGER_MESHPBC_CANCEL_REQUEST>(
            m_cmdu_tx);
    if (!request) {
        LOG(ERROR) << "Failed building cACTION_APMANAGER_MESHPBC_CANCEL_REQUEST";
        return false;
    }
    request->bssid() = bssid;

    if (!m_sender->send_cmdu(ap_manager_fd, m_cmdu_tx)) {
        LOG(ERROR) << "Failed sending MESHPBC_CANCEL_REQUEST for BSSID " << bssid;
        return false;
    }
    LOG(INFO) << "Mesh PBC cancel request sent for " << bssid;
    return true;
}

void PBCOnboardingTask::send_status_reports(const std::vector<sStatusReport> &reports)
{
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_STATUS_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type MULTI_AP_PUSH_BUTTON_STATUS_MESSAGE, has failed";
        return;
    }

    size_t added = 0;
    for (const auto &report : reports) {
        const bool with_failure_reason =
            report.result == eResult::FAILURE_ABORTED || report.result == eResult::NOT_STARTED;
        const size_t tlv_size = wfa_map::tlvMultiApPushButtonStatus::get_initial_size() +
                                (with_failure_reason ? sizeof(uint16_t) : 0);
        if (m_cmdu_tx.getMessageBuffLength() - m_cmdu_tx.getMessageLength() < tlv_size + 3) {
            LOG(ERROR) << "No space for the Status TLV of BSSID " << report.bssid << " and "
                       << (reports.size() - added - 1) << " more, not sent";
            break;
        }
        auto tlv = m_cmdu_tx.addClass<wfa_map::tlvMultiApPushButtonStatus>();
        if (!tlv) {
            LOG(ERROR) << "addClass wfa_map::tlvMultiApPushButtonStatus failed for BSSID "
                       << report.bssid << ", the remaining reports are not sent";
            break;
        }
        tlv->pbc_session_id() = report.session_id;
        tlv->bssid()          = report.bssid;
        tlv->result()         = report.result;
        if (with_failure_reason && !tlv->set_failure_reason(report.failure_reason)) {
            LOG(ERROR) << "set_failure_reason failed for BSSID " << report.bssid
                       << ", Status message not sent";
            return;
        }
        added++;
        tlv->enrollee_mac() = (report.result == eResult::SUCCESS) ? report.enrollee_mac
                                                                  : net::network_utils::ZERO_MAC;

        std::stringstream failure_reason;
        if (with_failure_reason) {
            failure_reason << ", Failure Reason " << report.failure_reason;
        }
        LOG(INFO) << "Multi-AP Push-Button Status: BSSID " << report.bssid << ", PBC Session ID "
                  << session_id_to_string(report.session_id) << ", Result 0x0" << int(report.result)
                  << " (" << wfa_map::tlvMultiApPushButtonStatus::eResult_str(report.result) << ")"
                  << failure_reason.str() << ", Enrollee MAC " << tlv->enrollee_mac();
    }

    if (added == 0) {
        return;
    }
    if (!m_sender->send_cmdu_to_controller(m_cmdu_tx)) {
        LOG(ERROR) << "Failed sending MULTI_AP_PUSH_BUTTON_STATUS_MESSAGE";
    }
}

void PBCOnboardingTask::end_session_with_failure(const std::string &bssid,
                                                 const std::string &reason, bool cancel, bool error)
{
    auto session_it = m_sessions.find(bssid);
    if (session_it == m_sessions.end()) {
        return;
    }
    const auto &session = session_it->second;
    if (error) {
        LOG(ERROR) << "Mesh PBC session on " << bssid << " ended: " << reason;
    } else {
        LOG(INFO) << "Mesh PBC session on " << bssid << " ended: " << reason;
    }
    const auto bssid_mac = tlvf::mac_from_string(bssid);
    if (cancel && !send_cancel_request(bssid_mac, session.radio_iface)) {
        LOG(ERROR) << "Mesh PBC session on " << bssid << ": cancel request not sent";
    }
    send_status_reports({{bssid_mac, session.session_id,
                          session.started ? eResult::FAILURE_ABORTED : eResult::NOT_STARTED,
                          WSC_CONFIG_ERROR_NO_ERROR, net::network_utils::ZERO_MAC}});
    m_sessions.erase(session_it);
}

static std::string uuid_to_string(const uint8_t *uuid)
{
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            text << '-';
        }
        text << std::setw(2) << int(uuid[i]);
    }
    return text.str();
}

std::vector<PBCOnboardingTask::sPushButtonConfig>
PBCOnboardingTask::read_push_button_configs(const std::string &radio_iface,
                                            ieee1905_1::CmduMessageRx &cmdu_rx)
{
    constexpr size_t max_device_password_length = 63;

    std::vector<sPushButtonConfig> configs;
    for (auto tlv : cmdu_rx.getClassList<wfa_map::tlvPushButtonConfig>()) {
        sPushButtonConfig config;
        const auto bssid          = tlv->bssid();
        config.bss_index          = tlv->bss_index();
        config.device_password_id = tlv->device_password_id();
        config.listen_duration    = tlv->listen_duration();

        std::stringstream tlv_name;
        tlv_name << "Push-Button Config TLV (radio " << radio_iface << ", BSSID " << bssid
                 << ", BSS_Index " << int(config.bss_index) << ")";

        if (bssid != net::network_utils::ZERO_MAC) {
            LOG(ERROR) << tlv_name.str() << " skipped: the BSSID is not zero";
            continue;
        }
        if (config.bss_index == 0) {
            LOG(ERROR) << tlv_name.str() << " skipped: BSS_Index is 0";
            continue;
        }

        auto uuid_e = tlv->uuid_e();
        if (!uuid_e) {
            LOG(ERROR) << tlv_name.str() << " skipped: UUID-E cannot be read";
            continue;
        }
        config.uuid_e = uuid_to_string(uuid_e);

        if (config.listen_duration == 0) {
            LOG(ERROR) << tlv_name.str() << " skipped: Listen Duration is 0";
            continue;
        }

        auto length = tlv->device_password_length();
        if (length > max_device_password_length) {
            LOG(ERROR) << tlv_name.str() << " skipped: Device Password of " << length
                       << " octets, more than " << max_device_password_length;
            continue;
        }
        if (length > 0) {
            auto device_password = tlv->device_password();
            if (!device_password) {
                LOG(ERROR) << tlv_name.str() << " skipped: Device Password cannot be read";
                continue;
            }
            if (!std::all_of(device_password, device_password + length,
                             [](uint8_t c) { return c >= 0x20 && c <= 0x7e; })) {
                LOG(ERROR) << tlv_name.str() << " skipped: Device Password is not printable text";
                continue;
            }
            config.device_password.assign(reinterpret_cast<const char *>(device_password), length);
        }

        LOG(DEBUG) << tlv_name.str() << " read: UUID-E " << config.uuid_e << ", Listen Duration "
                   << config.listen_duration << " s, Device Password ID " << std::hex
                   << config.device_password_id << std::dec << ", Device Password "
                   << (config.device_password.empty() ? "absent" : "present");
        configs.push_back(std::move(config));
    }
    return configs;
}

void PBCOnboardingTask::set_listen_durations(
    const std::string &radio_iface,
    const std::vector<std::pair<std::string, uint16_t>> &listen_durations)
{
    forget_listen_durations(radio_iface);
    for (const auto &ssid_duration : listen_durations) {
        auto result = m_listen_durations.emplace(std::make_pair(radio_iface, ssid_duration.first),
                                                 ssid_duration.second);
        if (!result.second) {
            LOG(WARNING) << "Listen Duration of SSID '" << ssid_duration.first << "' on radio "
                         << radio_iface << " given twice";
            continue;
        }
        LOG(INFO) << "Listen Duration of SSID '" << ssid_duration.first << "' on radio "
                  << radio_iface << ": " << ssid_duration.second << " s";
    }
}

void PBCOnboardingTask::handle_status_notification(const sMacAddr &bssid,
                                                   beerocks_message::eMeshPbcStatus status,
                                                   const std::string &reason,
                                                   const sMacAddr &enrollee_mac)
{
    auto bssid_str = tlvf::mac_to_string(bssid);

    LOG(INFO) << "Mesh PBC status bssid=" << bssid_str << " status=" << int(status) << " reason='"
              << reason << "' enrollee=" << enrollee_mac;

    auto session_it = m_sessions.find(bssid_str);
    if (session_it == m_sessions.end()) {
        LOG(WARNING) << "Mesh PBC status for " << bssid_str << " without a session, ignored";
        return;
    }
    auto &session = session_it->second;

    switch (status) {
    case beerocks_message::eMeshPbcStatus_Started: {
        if (session.started) {
            LOG(DEBUG) << "Mesh PBC session on " << bssid_str << " already reported as started";
            break;
        }
        session.started  = true;
        session.deadline = m_now() + session.listen_duration;
        LOG(INFO) << "Mesh PBC session on " << bssid_str << " started: ended by the Agent after "
                  << session.listen_duration.count()
                  << " s (Listen Duration of the Push-Button Config TLV)";
        send_status_reports({{bssid, session.session_id, eResult::STARTED,
                              WSC_CONFIG_ERROR_NO_ERROR, net::network_utils::ZERO_MAC}});
        if (session.cancel_requested) {
            LOG(INFO) << "Mesh PBC session on " << bssid_str << " started after a stop request";
            send_cancel_request(bssid, session.radio_iface);
        }
        break;
    }
    case beerocks_message::eMeshPbcStatus_NotStarted:
        end_session_with_failure(bssid_str, "not started, reason '" + reason + "'", false, true);
        break;
    case beerocks_message::eMeshPbcStatus_Ended: {
        const auto session_id = session.session_id;
        const bool started    = session.started;
        m_sessions.erase(session_it);
        if (!started) {
            // pwhm reports the end of a pairing only while a pairing runs, so the session
            // started.
            send_status_reports({{bssid, session_id, eResult::STARTED, WSC_CONFIG_ERROR_NO_ERROR,
                                  net::network_utils::ZERO_MAC}});
        }
        if (reason == "Success") {
            send_status_reports(
                {{bssid, session_id, eResult::SUCCESS, WSC_CONFIG_ERROR_NO_ERROR, enrollee_mac}});
        } else {
            send_status_reports(
                {{bssid, session_id, eResult::FAILURE_ABORTED,
                  end_reason_to_wsc_config_error(reason), net::network_utils::ZERO_MAC}});
        }
        break;
    }
    default:
        LOG(ERROR) << "Unknown mesh PBC status " << int(status);
        break;
    }
}

void PBCOnboardingTask::forget_listen_durations(const std::string &radio_iface)
{
    for (auto it = m_listen_durations.begin(); it != m_listen_durations.end();) {
        if (it->first.first == radio_iface) {
            it = m_listen_durations.erase(it);
        } else {
            ++it;
        }
    }
}

void PBCOnboardingTask::handle_ap_manager_disconnected(const std::string &radio_iface)
{
    forget_listen_durations(radio_iface);

    std::vector<std::string> bssids;
    for (const auto &session : m_sessions) {
        if (session.second.radio_iface == radio_iface) {
            bssids.push_back(session.first);
        }
    }
    for (const auto &bssid : bssids) {
        end_session_with_failure(bssid, "ap_manager of " + radio_iface + " disconnected", false,
                                 true);
    }
}

void PBCOnboardingTask::work()
{
    auto now = m_now();
    std::vector<std::pair<std::string, bool>> expired;
    for (const auto &session : m_sessions) {
        if (now >= session.second.deadline) {
            expired.emplace_back(session.first, session.second.started);
        }
    }
    for (const auto &bssid_started : expired) {
        if (bssid_started.second) {
            end_session_with_failure(bssid_started.first, "Listen Duration expired", true, false);
        } else {
            end_session_with_failure(bssid_started.first, "no start status in time", true, true);
        }
    }
}

} // namespace beerocks
