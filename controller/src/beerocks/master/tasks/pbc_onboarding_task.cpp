/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifdef ENABLE_NBAPI

#include "pbc_onboarding_task.h"
#include "../son_actions.h"
#include "mesh_pbc_template_actions.h"

#include <bcl/network/network_utils.h>
#include <easylogging++.h>
#include <tlvf/tlvftypes.h>
#include <tlvf/wfa_map/tlvMultiApPushButtonStatus.h>
#include <tlvf/wfa_map/tlvMultiApPushButtonTrigger.h>

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

using namespace prplmesh::controller::actions;

namespace son {

namespace {

constexpr auto STOP_REPORT_WAIT = std::chrono::seconds(10);

/**
 * An Agent drops a Trigger with the PBC Session ID of its last answer for a BSSID, so a recent
 * button PBC Session ID is not used again.
 */
constexpr auto SESSION_ID_REUSE_TIME = std::chrono::seconds(60);

/** WSC Configuration Error "Multiple PBC sessions detected" (WPS 2.0.8, Table 34). */
constexpr uint16_t WSC_CONFIG_ERROR_MULTIPLE_PBC_SESSIONS = 12;

using eReportResult = wfa_map::tlvMultiApPushButtonStatus::eResult;

bool same_session_id(const wfa_map::sPbcSessionId &a, const wfa_map::sPbcSessionId &b)
{
    return a.al_mac == b.al_mac && a.mid == b.mid;
}

const char *origin_to_string(PBCOnboardingTask::eOrigin origin)
{
    return origin == PBCOnboardingTask::eOrigin::BUTTON ? "button" : "Controller";
}

std::string bssids_to_string(const std::vector<sMacAddr> &bssids)
{
    std::ostringstream text;
    for (size_t i = 0; i < bssids.size(); i++) {
        text << (i ? "," : "") << bssids[i];
    }
    return text.str();
}

/** Hexadecimal text "0x..." with a fixed number of digits, built outside the log stream. */
std::string hex_text(unsigned int value, int digits)
{
    std::ostringstream text;
    text << "0x" << std::hex << std::setw(digits) << std::setfill('0') << value;
    return text.str();
}

bool agent_connected(db &database, const sMacAddr &al_mac)
{
    auto agent = database.get_agent(al_mac);
    return agent && agent->state == beerocks::STATE_CONNECTED;
}

} // namespace

PBCOnboardingTask::PBCOnboardingTask(db &database, ieee1905_1::CmduMessageTx &cmdu_tx, now_f now)
    : task("PBC onboarding task"), m_database(database), m_cmdu_tx(cmdu_tx), m_now(std::move(now))
{
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    m_session_counter = static_cast<uint16_t>(now_ns ^ (now_ns >> 16) ^ (now_ns >> 32));
    LOG(INFO) << "PBC onboarding task started, Controller session counter start value "
              << hex_text(m_session_counter, 4);
}

const char *PBCOnboardingTask::result_to_string(eResult result)
{
    switch (result) {
    case eResult::SUCCESS:
        return "Success";
    case eResult::ERROR_TIMEOUT:
        return "Error_Timeout";
    case eResult::ERROR_NOT_READY:
        return "Error_Not_Ready";
    case eResult::ERROR_INVALID_INPUT:
        return "Error_Invalid_Input";
    case eResult::ERROR_OTHER:
        return "Error_Other";
    }
    return "Error_Other";
}

std::string PBCOnboardingTask::session_id_to_string(const wfa_map::sPbcSessionId &session_id)
{
    std::ostringstream text;
    text << tlvf::mac_to_string(session_id.al_mac) << "-" << std::hex << std::setw(4)
         << std::setfill('0') << session_id.mid;
    return text.str();
}

const char *PBCOnboardingTask::bss_state_to_string(eBssState state)
{
    switch (state) {
    case eBssState::IDLE:
        return "Idle";
    case eBssState::REQUESTED:
        return "Requested";
    case eBssState::IN_PROGRESS:
        return "InProgress";
    }
    return "Idle";
}

const char *PBCOnboardingTask::bss_result_to_string(eBssResult result)
{
    switch (result) {
    case eBssResult::NONE:
        return "";
    case eBssResult::SUCCESS:
        return "Success";
    case eBssResult::ERROR_NOT_READY:
        return "Error_Not_Ready";
    case eBssResult::ERROR_TIMEOUT:
        return "Error_Timeout";
    case eBssResult::ERROR_OTHER:
        return "Error_Other";
    }
    return "";
}

bool PBCOnboardingTask::handle_ieee1905_1_msg(const sMacAddr &src_mac,
                                              ieee1905_1::CmduMessageRx &cmdu_rx)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_REQUEST_MESSAGE:
        handle_request(src_mac, cmdu_rx);
        return true;
    case ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_STATUS_MESSAGE:
        handle_status(src_mac, cmdu_rx);
        return true;
    default:
        return false;
    }
}

void PBCOnboardingTask::handle_request(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx)
{
    const uint16_t mid = cmdu_rx.getMessageId();
    std::ostringstream name;
    name << "Multi-AP Push-Button Request from " << src_mac << " mid=0x" << std::hex << std::setw(4)
         << std::setfill('0') << mid;

    if (!agent_connected(m_database, src_mac)) {
        LOG(WARNING) << name.str() << " ignored: the sender is not a connected Agent";
        return;
    }
    LOG(INFO) << name.str() << " received";

    const std::string reference = mesh_pbc_get_primary_template_reference();
    if (reference.empty()) {
        LOG(ERROR) << name.str() << ": nothing armed, " << TEMPLATES_ROOT_DM
                   << ".Network.X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference is empty";
        return;
    }

    sStartRequest request;
    request.template_reference = reference;
    request.origin             = eOrigin::BUTTON;
    request.request_id.al_mac  = src_mac;
    request.request_id.mid     = mid;
    auto result                = start(request);
    if (!result.accepted) {
        LOG(WARNING) << name.str() << " rejected: " << result_to_string(result.error);
    }
}

PBCOnboardingTask::sStartResult PBCOnboardingTask::reject(const std::string &template_path,
                                                          eResult error)
{
    if (!template_path.empty() && !(m_session && m_session->template_path == template_path)) {
        mesh_pbc_set_template_status_rejected(template_path, result_to_string(error), 0);
    }
    sStartResult result;
    result.accepted = false;
    result.error    = error;
    return result;
}

PBCOnboardingTask::sStartResult PBCOnboardingTask::start(const sStartRequest &request)
{
    std::ostringstream name;
    name << "push-button session request (origin " << origin_to_string(request.origin)
         << ", template \"" << request.template_reference << "\"";
    if (request.agent_al_mac != beerocks::net::network_utils::ZERO_MAC) {
        name << ", Agent " << request.agent_al_mac;
    }
    if (!request.bssids.empty()) {
        name << ", BSSIDs " << bssids_to_string(request.bssids);
    }
    name << ")";

    sMeshPbcSessionTemplate session_template;
    std::string reason;
    auto check =
        mesh_pbc_get_session_template(request.template_reference, session_template, reason);

    if (m_session) {
        LOG(WARNING) << name.str() << " rejected (Error_Not_Ready): session " << m_session->id_text
                     << " of " << m_session->template_path << " is active";
        return reject(session_template.template_path, eResult::ERROR_NOT_READY);
    }
    if (check == eMeshPbcSessionTemplateResult::NOT_READY) {
        LOG(WARNING) << name.str() << " rejected (Error_Not_Ready): " << reason;
        return reject(session_template.template_path, eResult::ERROR_NOT_READY);
    }
    if (check != eMeshPbcSessionTemplateResult::VALID) {
        LOG(ERROR) << name.str() << " rejected (Error_Invalid_Input): " << reason;
        return reject(session_template.template_path, eResult::ERROR_INVALID_INPUT);
    }
    const std::string &template_path = session_template.template_path;

    if (request.origin == eOrigin::BUTTON &&
        request.request_id.al_mac == beerocks::net::network_utils::ZERO_MAC) {
        LOG(ERROR) << name.str() << " rejected (Error_Invalid_Input): no Request AL MAC";
        return reject(template_path, eResult::ERROR_INVALID_INPUT);
    }
    if (request.agent_al_mac != beerocks::net::network_utils::ZERO_MAC &&
        !m_database.get_agent(request.agent_al_mac)) {
        LOG(ERROR) << name.str() << " rejected (Error_Invalid_Input): " << request.agent_al_mac
                   << " is not a known Agent";
        return reject(template_path, eResult::ERROR_INVALID_INPUT);
    }

    std::unordered_map<sMacAddr, sMacAddr> resolved_set;
    std::unordered_map<sMacAddr, std::string> link_ssids;
    std::unordered_map<sMacAddr, std::string> other_links;
    for (const auto &link : m_database.get_mesh_pbc_bss_links()) {
        if (link.mesh_pbc_template != template_path) {
            if (link.bssid != beerocks::net::network_utils::ZERO_MAC) {
                other_links[link.bssid] = link.mesh_pbc_template;
            }
            continue;
        }
        if (link.bssid == beerocks::net::network_utils::ZERO_MAC) {
            LOG(INFO) << "push-button target skipped: link al_mac=" << link.al_mac
                      << " ruid=" << link.ruid << " bss_index=" << int(link.bss_index)
                      << " ssid=" << link.ssid << " has no BSSID";
            continue;
        }
        resolved_set[link.bssid] = link.al_mac;
        link_ssids[link.bssid]   = link.ssid;
    }

    for (const auto &bssid : request.bssids) {
        auto it = resolved_set.find(bssid);
        if (it == resolved_set.end()) {
            LOG(ERROR) << name.str() << " rejected (Error_Invalid_Input): BSSID " << bssid
                       << " is not in the resolved set of " << template_path;
            return reject(template_path, eResult::ERROR_INVALID_INPUT);
        }
        if (request.agent_al_mac != beerocks::net::network_utils::ZERO_MAC &&
            it->second != request.agent_al_mac) {
            LOG(ERROR) << name.str() << " rejected (Error_Invalid_Input): BSSID " << bssid
                       << " is not a BSS of Agent " << request.agent_al_mac;
            return reject(template_path, eResult::ERROR_INVALID_INPUT);
        }
    }

    std::unordered_map<sMacAddr, std::vector<sMacAddr>> targets;
    size_t target_count = 0;
    for (const auto &agent_entry : m_database.m_agents) {
        const auto &agent = agent_entry.second;
        for (const auto &radio_entry : agent->radios) {
            for (const auto &bss_entry : radio_entry.second->bsses) {
                const auto &bss = bss_entry.second;
                std::string skip;
                auto it = resolved_set.find(bss->bssid);
                if (it == resolved_set.end() || it->second != agent->al_mac) {
                    auto other = other_links.find(bss->bssid);
                    if (other != other_links.end()) {
                        skip = "its push-button link names " + other->second;
                    } else if (bss->backhaul && !bss->fronthaul) {
                        skip = "backhaul BSS, no push-button link";
                    } else {
                        skip = "no push-button link";
                    }
                } else if (bss->ssid != link_ssids[bss->bssid]) {
                    skip = "its push-button link has SSID \"" + link_ssids[bss->bssid] +
                           "\", the BSS has SSID \"" + bss->ssid + "\"";
                } else if (agent->state != beerocks::STATE_CONNECTED) {
                    skip = "the Agent is not connected";
                } else if (!agent->mesh_wide_pbc_onboarding_support) {
                    skip = "the Agent does not report Multi-AP Push-Button Trigger support";
                } else if (request.agent_al_mac != beerocks::net::network_utils::ZERO_MAC &&
                           agent->al_mac != request.agent_al_mac) {
                    skip = "not a BSS of the requested Agent";
                } else if (!request.bssids.empty() &&
                           std::find(request.bssids.begin(), request.bssids.end(), bss->bssid) ==
                               request.bssids.end()) {
                    skip = "not in the requested BSSID list";
                }
                if (!skip.empty()) {
                    LOG(INFO) << "push-button target skipped: bssid=" << bss->bssid
                              << " al_mac=" << agent->al_mac << " ssid=" << bss->ssid << ": "
                              << skip;
                    continue;
                }
                targets[agent->al_mac].push_back(bss->bssid);
                target_count++;
            }
        }
    }
    if (targets.empty()) {
        LOG(WARNING) << name.str() << " rejected (Error_Not_Ready): no BSS is a target";
        return reject(template_path, eResult::ERROR_NOT_READY);
    }

    wfa_map::sPbcSessionId session_id{};
    if (request.origin == eOrigin::BUTTON) {
        session_id = request.request_id;
        if (recently_used(session_id)) {
            LOG(WARNING) << name.str() << " rejected (Error_Not_Ready): PBC Session ID "
                         << session_id_to_string(session_id) << " was used in the last "
                         << std::chrono::seconds(SESSION_ID_REUSE_TIME).count() << " s";
            return reject(template_path, eResult::ERROR_NOT_READY);
        }
    } else if (!next_controller_session_id(session_id)) {
        LOG(ERROR) << name.str() << " rejected (Error_Not_Ready): no free PBC Session ID";
        return reject(template_path, eResult::ERROR_NOT_READY);
    }
    remember_session_id(session_id);

    m_session                  = std::make_unique<sSession>();
    m_session->id              = session_id;
    m_session->id_text         = session_id_to_string(session_id);
    m_session->origin          = request.origin;
    m_session->template_path   = template_path;
    m_session->wps             = session_template.wps;
    m_session->dpp             = session_template.dpp;
    m_session->listen_duration = session_template.listen_duration;
    m_session->start_time      = m_now();
    m_session->deadline = m_session->start_time + std::chrono::seconds(m_session->listen_duration);
    m_session->armed_bss_count   = target_count;
    m_session->armed_agent_count = targets.size();
    m_session->on_completion     = request.on_completion;

    LOG(INFO) << "push-button session " << m_session->id_text << " started: origin "
              << origin_to_string(request.origin) << ", template " << template_path
              << ", ListenDuration " << m_session->listen_duration << " s, "
              << m_session->armed_agent_count << " Agent(s), " << m_session->armed_bss_count
              << " BSS(s)";

    mesh_pbc_set_template_status_started(template_path, m_session->id_text);

    for (const auto &target : targets) {
        for (const auto &bssid : target.second) {
            auto stopped = m_stopped_bsses.find(bssid);
            if (stopped != m_stopped_bsses.end()) {
                LOG(INFO) << "BSS " << bssid << " armed again: the final report of session "
                          << stopped->second.session_id_text << " is no longer expected";
                m_stopped_bsses.erase(stopped);
            }
            sArmedBss bss;
            bss.al_mac              = target.first;
            bss.state               = eBssState::REQUESTED;
            m_session->bsses[bssid] = bss;
            update_bss(bssid, target.first, m_session->id_text, eBssState::REQUESTED,
                       eBssResult::NONE, 0, beerocks::net::network_utils::ZERO_MAC);
        }
    }

    for (const auto &target : targets) {
        if (send_trigger(target.first, m_session->id, m_session->wps, m_session->dpp, true,
                         target.second)) {
            continue;
        }
        LOG(ERROR) << "push-button session " << m_session->id_text << ": the Trigger to Agent "
                   << target.first << " was not sent, its " << target.second.size()
                   << " BSS(s) end with Error_Other";
        m_session->controller_ended_bss = true;
        for (const auto &bssid : target.second) {
            auto &bss = m_session->bsses[bssid];
            bss.state = eBssState::IDLE;
            bss.ended = true;
            update_bss(bssid, target.first, m_session->id_text, eBssState::IDLE,
                       eBssResult::ERROR_OTHER, 0, beerocks::net::network_utils::ZERO_MAC);
        }
    }

    sStartResult result;
    result.accepted   = true;
    result.error      = eResult::SUCCESS;
    result.session_id = m_session->id_text;
    return result;
}

bool PBCOnboardingTask::recently_used(const wfa_map::sPbcSessionId &session_id)
{
    const auto now = m_now();
    while (!m_recent_session_ids.empty() &&
           now - m_recent_session_ids.front().second >= SESSION_ID_REUSE_TIME) {
        m_recent_session_ids.pop_front();
    }
    return std::any_of(
        m_recent_session_ids.begin(), m_recent_session_ids.end(),
        [&](const std::pair<wfa_map::sPbcSessionId, std::chrono::steady_clock::time_point> &entry) {
            return same_session_id(entry.first, session_id);
        });
}

void PBCOnboardingTask::remember_session_id(const wfa_map::sPbcSessionId &session_id)
{
    m_recent_session_ids.emplace_back(session_id, m_now());
}

bool PBCOnboardingTask::next_controller_session_id(wfa_map::sPbcSessionId &session_id)
{
    session_id.al_mac = m_database.get_local_bridge_mac();
    for (uint32_t i = 0; i <= UINT16_MAX; i++) {
        m_session_counter++;
        session_id.mid = m_session_counter;
        if (!recently_used(session_id)) {
            return true;
        }
    }
    return false;
}

bool PBCOnboardingTask::send_trigger(const sMacAddr &al_mac,
                                     const wfa_map::sPbcSessionId &session_id, bool wps, bool dpp,
                                     bool start, const std::vector<sMacAddr> &bssids)
{
    const std::string id_text = session_id_to_string(session_id);
    if (bssids.empty()) {
        LOG(ERROR) << "Multi-AP Push-Button Trigger to " << al_mac << " (session " << id_text
                   << ") not sent: no BSSID";
        return false;
    }
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_TRIGGER_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type MULTI_AP_PUSH_BUTTON_TRIGGER_MESSAGE has failed";
        return false;
    }
    auto tlv = m_cmdu_tx.addClass<wfa_map::tlvMultiApPushButtonTrigger>();
    if (!tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvMultiApPushButtonTrigger has failed";
        return false;
    }
    tlv->pbc_session_id()         = session_id;
    tlv->push_button_method().wps = wps ? 1 : 0;
    tlv->push_button_method().dpp = dpp ? 1 : 0;
    tlv->state()                  = start ? wfa_map::tlvMultiApPushButtonTrigger::eState::START
                         : wfa_map::tlvMultiApPushButtonTrigger::eState::STOP;
    if (!tlv->alloc_bssid_list(bssids.size())) {
        LOG(ERROR) << "Failed to allocate " << bssids.size() << " BSSIDs in the Trigger TLV";
        return false;
    }
    for (size_t i = 0; i < bssids.size(); i++) {
        auto entry = tlv->bssid_list(i);
        if (!std::get<0>(entry)) {
            LOG(ERROR) << "Failed to get BSSID entry " << i << " of the Trigger TLV";
            return false;
        }
        std::get<1>(entry) = bssids[i];
    }

    if (!son_actions::send_cmdu_to_agent(al_mac, m_cmdu_tx, m_database)) {
        LOG(ERROR) << "Failed to send the Multi-AP Push-Button Trigger to " << al_mac;
        return false;
    }
    LOG(INFO) << "Multi-AP Push-Button Trigger sent to " << al_mac << ": session " << id_text
              << ", State=" << (start ? "START" : "STOP") << ", method " << (wps ? "WPS" : "")
              << (wps && dpp ? "," : "") << (dpp ? "DPP" : "") << ", BSSIDs "
              << bssids_to_string(bssids);
    return true;
}

void PBCOnboardingTask::update_bss(const sMacAddr &bssid, const sMacAddr &al_mac,
                                   const std::string &session_id, eBssState state,
                                   eBssResult result, uint16_t failure_reason,
                                   const sMacAddr &enrollee_mac)
{
    sMeshPbcBssStatus status;
    status.state          = bss_state_to_string(state);
    status.session_id     = session_id;
    status.result         = bss_result_to_string(result);
    status.failure_reason = failure_reason;
    if (result == eBssResult::SUCCESS) {
        status.enrollee_mac = tlvf::mac_to_string(enrollee_mac);
    }
    LOG(INFO) << "BSS " << bssid << " (al_mac=" << al_mac
              << ") push-button state: State=" << status.state << " Result=\"" << status.result
              << "\" FailureReason=" << status.failure_reason << " Enrollee=\""
              << status.enrollee_mac << "\" session " << session_id;
    auto bss = m_database.get_bss(bssid, al_mac);
    if (!bss || bss->dm_path.empty()) {
        LOG(WARNING) << "push-button state of BSS " << bssid << " (al_mac=" << al_mac
                     << ") not written: the BSS has no data model object";
    } else {
        mesh_pbc_set_bss_status(bss->dm_path, status);
    }
}

void PBCOnboardingTask::handle_templates_network_disabled()
{
    if (!m_session) {
        return;
    }
    LOG(WARNING) << "push-button session " << m_session->id_text
                 << " terminated: Templates.Network.Enable changed to false";
    end_session(eResult::ERROR_OTHER, 0, beerocks::net::network_utils::ZERO_MAC,
                beerocks::net::network_utils::ZERO_MAC);
}

void PBCOnboardingTask::handle_status(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto tlvs = cmdu_rx.getClassList<wfa_map::tlvMultiApPushButtonStatus>();
    if (tlvs.empty()) {
        LOG(ERROR) << "Multi-AP Push-Button Status from " << src_mac
                   << " mid=" << hex_text(cmdu_rx.getMessageId(), 4)
                   << " has no Status TLV, ignored";
        return;
    }

    for (const auto &tlv : tlvs) {
        if (!tlv) {
            continue;
        }
        const sMacAddr bssid  = tlv->bssid();
        const auto session_id = tlv->pbc_session_id();
        const auto result     = tlv->result();
        const bool has_failure =
            (result == eReportResult::FAILURE_ABORTED || result == eReportResult::NOT_STARTED);
        const uint16_t failure =
            (has_failure && tlv->failure_reason()) ? *tlv->failure_reason() : 0;
        const sMacAddr enrollee   = tlv->enrollee_mac();
        const std::string id_text = session_id_to_string(session_id);
        const bool active         = m_session && same_session_id(m_session->id, session_id);

        LOG(INFO) << "Multi-AP Push-Button Status from " << src_mac << ": bssid=" << bssid
                  << " session " << id_text << " Result=" << hex_text(result, 2) << " ("
                  << wfa_map::tlvMultiApPushButtonStatus::eResult_str(result)
                  << ") FailureReason=" << failure << " Enrollee=" << enrollee << " ("
                  << (active ? "active" : "stale") << ")";

        if (!m_database.get_bss(bssid, src_mac)) {
            LOG(WARNING) << "Multi-AP Push-Button Status for " << bssid
                         << " ignored: it is not a BSS of " << src_mac;
            continue;
        }
        if (result != eReportResult::SUCCESS && result != eReportResult::FAILURE_ABORTED &&
            result != eReportResult::NOT_STARTED && result != eReportResult::STARTED) {
            LOG(WARNING) << "Multi-AP Push-Button Status for " << bssid
                         << " ignored: reserved Result value " << hex_text(result, 2);
            continue;
        }

        sArmedBss *bss = nullptr;
        std::string bss_session_id;
        bool from_stopped = false;
        if (active) {
            auto it = m_session->bsses.find(bssid);
            if (it != m_session->bsses.end() && it->second.al_mac == src_mac) {
                bss            = &it->second;
                bss_session_id = m_session->id_text;
            }
        }
        if (!bss) {
            auto it = m_stopped_bsses.find(bssid);
            if (it != m_stopped_bsses.end() && it->second.bss.al_mac == src_mac &&
                same_session_id(it->second.session_id, session_id)) {
                bss            = &it->second.bss;
                bss_session_id = it->second.session_id_text;
                from_stopped   = true;
            }
        }
        if (!bss) {
            LOG(INFO) << "Multi-AP Push-Button Status for " << bssid << " session " << id_text
                      << " does not change the BSS: the BSS waits for no report of that session";
            continue;
        }

        if (!apply_report(bssid, *bss, bss_session_id, result, failure, enrollee)) {
            continue;
        }
        if (from_stopped && bss->ended) {
            m_stopped_bsses.erase(bssid);
        }

        if (active) {
            if (failure != 0 && result != eReportResult::STARTED) {
                m_session->last_failure_reason = failure;
            }
            evaluate_session(bssid, result, failure, enrollee);
        }
    }
}

bool PBCOnboardingTask::apply_report(const sMacAddr &bssid, sArmedBss &bss,
                                     const std::string &session_id, uint8_t result,
                                     uint16_t failure_reason, const sMacAddr &enrollee_mac)
{
    if (bss.ended) {
        LOG(INFO) << "Multi-AP Push-Button Status for " << bssid << " session " << session_id
                  << " ignored: the BSS already has its final report";
        return false;
    }

    eBssResult bss_result = eBssResult::NONE;
    switch (static_cast<eReportResult>(result)) {
    case eReportResult::STARTED:
        if (bss.state != eBssState::REQUESTED) {
            LOG(INFO) << "Multi-AP Push-Button Status Started for " << bssid << " session "
                      << session_id << " ignored: the BSS is " << bss_state_to_string(bss.state);
            return false;
        }
        bss.state = eBssState::IN_PROGRESS;
        update_bss(bssid, bss.al_mac, session_id, bss.state, eBssResult::NONE, 0,
                   beerocks::net::network_utils::ZERO_MAC);
        return true;
    case eReportResult::SUCCESS:
        bss_result = eBssResult::SUCCESS;
        break;
    case eReportResult::NOT_STARTED:
        bss_result = eBssResult::ERROR_NOT_READY;
        break;
    case eReportResult::FAILURE_ABORTED:
        if (failure_reason != 0 || bss.stop_cause == eStopCause::OTHER) {
            bss_result = eBssResult::ERROR_OTHER;
        } else {
            bss_result = eBssResult::ERROR_TIMEOUT;
        }
        break;
    }

    bss.state           = eBssState::IDLE;
    bss.ended           = true;
    bss.reported        = true;
    bss.reported_result = result;
    update_bss(bssid, bss.al_mac, session_id, bss.state, bss_result, failure_reason, enrollee_mac);
    return true;
}

void PBCOnboardingTask::evaluate_session(const sMacAddr &bssid, uint8_t result,
                                         uint16_t failure_reason, const sMacAddr &enrollee_mac)
{
    if (!m_session) {
        return;
    }
    switch (static_cast<eReportResult>(result)) {
    case eReportResult::STARTED:
        return;
    case eReportResult::SUCCESS:
        end_session(eResult::SUCCESS, 0, enrollee_mac, bssid);
        return;
    case eReportResult::FAILURE_ABORTED:
        if (failure_reason == WSC_CONFIG_ERROR_MULTIPLE_PBC_SESSIONS) {
            end_session(eResult::ERROR_OTHER, failure_reason,
                        beerocks::net::network_utils::ZERO_MAC,
                        beerocks::net::network_utils::ZERO_MAC);
            return;
        }
        break;
    default:
        break;
    }

    bool all_ended       = true;
    bool all_not_started = true;
    for (const auto &entry : m_session->bsses) {
        if (!entry.second.ended) {
            all_ended = false;
            break;
        }
        if (!entry.second.reported || entry.second.reported_result != eReportResult::NOT_STARTED) {
            all_not_started = false;
        }
    }
    if (!all_ended || m_session->bsses.empty()) {
        return;
    }
    if (m_session->controller_ended_bss) {
        end_session(eResult::ERROR_OTHER, m_session->last_failure_reason,
                    beerocks::net::network_utils::ZERO_MAC, beerocks::net::network_utils::ZERO_MAC);
    } else if (all_not_started) {
        end_session(eResult::ERROR_NOT_READY, m_session->last_failure_reason,
                    beerocks::net::network_utils::ZERO_MAC, beerocks::net::network_utils::ZERO_MAC);
    } else if (m_session->last_failure_reason != 0) {
        end_session(eResult::ERROR_OTHER, m_session->last_failure_reason,
                    beerocks::net::network_utils::ZERO_MAC, beerocks::net::network_utils::ZERO_MAC);
    } else {
        end_session(eResult::ERROR_TIMEOUT, 0, beerocks::net::network_utils::ZERO_MAC,
                    beerocks::net::network_utils::ZERO_MAC);
    }
}

void PBCOnboardingTask::stop_bsses(const std::vector<sMacAddr> &bssids, eStopCause cause)
{
    if (!m_session) {
        return;
    }
    const auto now = m_now();
    std::unordered_map<sMacAddr, std::vector<sMacAddr>> stops;
    for (const auto &bssid : bssids) {
        auto it = m_session->bsses.find(bssid);
        if (it == m_session->bsses.end()) {
            continue;
        }
        sArmedBss bss = it->second;
        m_session->bsses.erase(it);
        if (bss.ended) {
            continue;
        }
        bss.stop_cause = cause;
        if (cause == eStopCause::OTHER) {
            m_session->controller_ended_bss = true;
        }
        if (!agent_connected(m_database, bss.al_mac)) {
            update_bss(bssid, bss.al_mac, m_session->id_text, eBssState::IDLE,
                       cause == eStopCause::SESSION_TIMER ? eBssResult::ERROR_TIMEOUT
                                                          : eBssResult::ERROR_OTHER,
                       0, beerocks::net::network_utils::ZERO_MAC);
            continue;
        }
        sStoppedBss stopped;
        stopped.session_id      = m_session->id;
        stopped.session_id_text = m_session->id_text;
        stopped.bss             = bss;
        stopped.deadline        = now + STOP_REPORT_WAIT;
        m_stopped_bsses[bssid]  = std::move(stopped);
        stops[bss.al_mac].push_back(bssid);
    }
    for (const auto &stop : stops) {
        send_trigger(stop.first, m_session->id, m_session->wps, m_session->dpp, false, stop.second);
    }
}

bool PBCOnboardingTask::end_if_nothing_armed()
{
    if (!m_session) {
        return false;
    }
    for (const auto &entry : m_session->bsses) {
        if (!entry.second.ended) {
            return false;
        }
    }
    LOG(INFO) << "push-button session " << m_session->id_text
              << ": no BSS is Requested or InProgress any more";
    end_session(eResult::ERROR_OTHER, m_session->last_failure_reason,
                beerocks::net::network_utils::ZERO_MAC, beerocks::net::network_utils::ZERO_MAC);
    return true;
}

void PBCOnboardingTask::end_session(eResult status, uint16_t failure_reason,
                                    const sMacAddr &enrollee_mac, const sMacAddr &bssid)
{
    if (!m_session) {
        return;
    }

    std::vector<sMacAddr> remaining;
    for (const auto &entry : m_session->bsses) {
        if (!entry.second.ended) {
            remaining.push_back(entry.first);
        }
    }
    stop_bsses(remaining,
               status == eResult::ERROR_TIMEOUT ? eStopCause::SESSION_TIMER : eStopCause::OTHER);

    std::unique_ptr<sSession> session = std::move(m_session);
    const bool success                = (status == eResult::SUCCESS);
    mesh_pbc_set_template_status_ended(session->template_path, result_to_string(status),
                                       failure_reason,
                                       success ? tlvf::mac_to_string(enrollee_mac) : "",
                                       success ? tlvf::mac_to_string(bssid) : "");

    const auto duration =
        std::chrono::duration_cast<std::chrono::seconds>(m_now() - session->start_time);
    LOG(INFO) << "push-button session " << session->id_text << " ended: Result "
              << result_to_string(status) << ", FailureReason " << failure_reason << ", Enrollee "
              << (success ? tlvf::mac_to_string(enrollee_mac) : "-") << ", BSSID "
              << (success ? tlvf::mac_to_string(bssid) : "-") << ", duration " << duration.count()
              << " s, " << session->armed_bss_count << " BSS(s)";

    if (session->on_completion) {
        sCompletion completion;
        completion.session_id     = session->id_text;
        completion.status         = status;
        completion.failure_reason = success ? 0 : failure_reason;
        completion.enrollee_mac   = success ? enrollee_mac : beerocks::net::network_utils::ZERO_MAC;
        completion.bssid          = success ? bssid : beerocks::net::network_utils::ZERO_MAC;
        session->on_completion(completion);
    }
}

bool PBCOnboardingTask::terminate(const std::string &template_reference)
{
    sMeshPbcSessionTemplate session_template;
    std::string reason;
    mesh_pbc_get_session_template(template_reference, session_template, reason);
    if (!m_session || session_template.template_path.empty() ||
        session_template.template_path != m_session->template_path) {
        LOG(WARNING) << "push-button terminate of \"" << template_reference
                     << "\" failed: no session of that template is active";
        return false;
    }
    LOG(INFO) << "push-button session " << m_session->id_text << " terminated (template level)";
    end_session(eResult::ERROR_OTHER, 0, beerocks::net::network_utils::ZERO_MAC,
                beerocks::net::network_utils::ZERO_MAC);
    return true;
}

bool PBCOnboardingTask::terminate(const std::string &session_id, const sMacAddr &agent_al_mac)
{
    std::ostringstream name;
    name << "push-button terminate of session " << session_id << " on Agent " << agent_al_mac;
    if (!m_session || m_session->id_text != session_id) {
        LOG(WARNING) << name.str() << " failed: it is not the active session";
        return false;
    }

    std::vector<sMacAddr> stop;
    for (const auto &entry : m_session->bsses) {
        if (entry.second.al_mac == agent_al_mac && !entry.second.ended) {
            stop.push_back(entry.first);
        }
    }
    if (stop.empty()) {
        LOG(WARNING) << name.str() << " failed: no BSS of the Agent is armed in the session";
        return false;
    }

    LOG(INFO) << name.str() << ": stopping " << stop.size() << " BSS(s)";
    stop_bsses(stop, eStopCause::OTHER);
    end_if_nothing_armed();
    return true;
}

void PBCOnboardingTask::work()
{
    const auto now = m_now();
    if (end_if_nothing_armed()) {
        return;
    }
    if (m_session && now >= m_session->deadline) {
        LOG(INFO) << "push-button session " << m_session->id_text << ": the session timer ("
                  << m_session->listen_duration << " s) expired";
        end_session(eResult::ERROR_TIMEOUT, 0, beerocks::net::network_utils::ZERO_MAC,
                    beerocks::net::network_utils::ZERO_MAC);
    }

    for (auto it = m_stopped_bsses.begin(); it != m_stopped_bsses.end();) {
        if (now < it->second.deadline) {
            ++it;
            continue;
        }
        const sMacAddr bssid      = it->first;
        const sStoppedBss stopped = it->second;
        it                        = m_stopped_bsses.erase(it);
        LOG(WARNING) << "BSS " << bssid << " (al_mac=" << stopped.bss.al_mac
                     << ") sent no final report of session " << stopped.session_id_text
                     << " within " << std::chrono::seconds(STOP_REPORT_WAIT).count()
                     << " s after State=STOP: set to Idle, Error_Timeout";
        update_bss(bssid, stopped.bss.al_mac, stopped.session_id_text, eBssState::IDLE,
                   eBssResult::ERROR_TIMEOUT, 0, beerocks::net::network_utils::ZERO_MAC);
    }
}

} // namespace son

#endif // ENABLE_NBAPI
