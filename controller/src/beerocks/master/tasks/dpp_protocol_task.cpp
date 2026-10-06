/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_protocol_task.h"
#include "dpp_onboarding_task.h"
#include "task_pool.h"

#include "../controller_dpp_configuration.h"
#include "../db/db.h"
#include "../son_actions.h"

#include <bcl/network/network_utils.h>
#include <easylogging++.h>
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>
#include <tlvf/wfa_map/tlvDppMessage.h>

namespace son {

namespace {
constexpr uint8_t k_dpp_protocol_version = 2;
constexpr uint8_t k_dpp_status_ok        = 0;
// DPP Public Action Connection Status Result. Same value as 0004 dpp_internal
// k_dpp_connection_status_result. Not present in 0001 eFrameType, so dispatch
// uses uint8_t (see switch below) to avoid -Wswitch on the TLV enum.
constexpr uint8_t k_dpp_connection_status_result = 12;
} // namespace

dpp_protocol_task::dpp_protocol_task(db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_)
    : task("dpp_protocol_task"), m_database(database_), m_cmdu_tx(cmdu_tx_)
{
}

void dpp_protocol_task::configure_onboarding_notifier(task_pool &pool, int onboarding_task_id)
{
    m_task_pool              = &pool;
    m_dpp_onboarding_task_id = onboarding_task_id;
}

void dpp_protocol_task::push_dpp_onboarding_task_event(int event_type, const std::string &reason)
{
    if (!m_task_pool || m_dpp_onboarding_task_id < 0) {
        return;
    }

    if (reason.empty()) {
        m_task_pool->push_event(m_dpp_onboarding_task_id, event_type);
        return;
    }

    m_task_pool->push_event(m_dpp_onboarding_task_id, event_type, new std::string(reason));
}

void dpp_protocol_task::reset_session()
{
    m_matched_bootstrap          = nullptr;
    m_active_request_conn_status = false;
    m_configurator.reset();
    m_session = {};
    LOG(INFO) << "DPP protocol session reset";
}

bool dpp_protocol_task::handle_ieee1905_1_msg(const sMacAddr &src_mac,
                                              ieee1905_1::CmduMessageRx &cmdu_rx)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::CHIRP_NOTIFICATION_MESSAGE:
        return handle_cmdu_1905_chirp_notification(src_mac, cmdu_rx);
    case ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE:
        return handle_cmdu_1905_proxied_encap_dpp(src_mac, cmdu_rx);
    case ieee1905_1::eMessageType::DIRECT_ENCAP_DPP_MESSAGE:
        return handle_cmdu_1905_direct_encap_dpp(src_mac, cmdu_rx);
    default:
        return false;
    }
}

bool dpp_protocol_task::handle_cmdu_1905_proxied_encap_dpp(const sMacAddr &src_mac,
                                                           ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto agent = m_database.m_agents.get(src_mac);
    if (!agent || !agent->dpp_onboarding_support) {
        LOG(WARNING) << "PROXIED_ENCAP_DPP_MESSAGE from unknown or non-DPP Agent " << src_mac;
        return false;
    }

    if (m_session.proxy_agent == beerocks::net::network_utils::ZERO_MAC ||
        src_mac != m_session.proxy_agent) {
        LOG(WARNING) << "PROXIED_ENCAP_DPP_MESSAGE from Agent " << src_mac
                     << " does not match active Proxy Agent " << m_session.proxy_agent;
        return false;
    }

    if (m_session.use_direct_encap) {
        LOG(WARNING) << "Ignoring PROXIED_ENCAP_DPP_MESSAGE while Direct Encap session is active";
        return false;
    }
    auto encap = cmdu_rx.getClass<wfa_map::tlv1905EncapDpp>();
    if (!encap) {
        LOG(ERROR) << "PROXIED_ENCAP_DPP_MESSAGE missing 1905 Encap DPP TLV";
        return false;
    }

    const auto frame_len  = encap->encapsulated_frame_length();
    const auto frame_data = encap->encapsulated_frame();
    if (!frame_data || frame_len == 0) {
        LOG(ERROR) << "PROXIED_ENCAP_DPP_MESSAGE contains an empty DPP frame";
        return false;
    }
    std::vector<uint8_t> frame(frame_data, frame_data + frame_len);

    sMacAddr enrollee_mac = m_session.last_chirp_enrollee;
    if (encap->frame_flags().enrollee_mac_address_present) {
        auto received_enrollee = encap->dest_sta_mac();
        if (!received_enrollee) {
            LOG(ERROR) << "1905 Encap DPP TLV sets Enrollee MAC present without a MAC";
            return false;
        }
        if (m_session.last_chirp_enrollee_valid &&
            *received_enrollee != m_session.last_chirp_enrollee) {
            LOG(WARNING) << "PROXIED_ENCAP_DPP_MESSAGE Enrollee "
                         << tlvf::mac_to_string(*received_enrollee)
                         << " does not match active Enrollee "
                         << tlvf::mac_to_string(m_session.last_chirp_enrollee);
            return false;
        }
        enrollee_mac = *received_enrollee;
    } else if (!m_session.last_chirp_enrollee_valid) {
        LOG(ERROR) << "PROXIED_ENCAP_DPP_MESSAGE has no Enrollee MAC for the active session";
        return false;
    }

    // Cast to uint8_t: Connection Status Result (12) is a DPP Public Action
    // type used by 0004 unwrap APIs but is not a named 0001 eFrameType value.
    const auto frame_type = static_cast<uint8_t>(encap->frame_type());
    const bool is_gas     = encap->frame_flags().dpp_frame_indicator != 0;
    std::string error;

    switch (frame_type) {
    case static_cast<uint8_t>(wfa_map::tlv1905EncapDpp::eFrameType::DPP_AUTHENTICATION_RESPONSE): {
        if (is_gas) {
            LOG(ERROR) << "DPP Authentication Response marked as GAS";
            return false;
        }

        std::vector<uint8_t> auth_confirm;
        if (!m_configurator.handle_authentication_response(frame, auth_confirm, error)) {
            LOG(WARNING) << "Failed processing DPP Authentication Response: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_INIT_FAILED, error);
            return false;
        }

        if (!send_dpp_authentication_confirm(enrollee_mac, std::move(auth_confirm))) {
            LOG(ERROR) << "Failed sending DPP Authentication Confirm through Proxy Agent "
                       << src_mac;
            push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_INIT_FAILED,
                                           "Failed sending DPP Authentication Confirm");
            return false;
        }

        m_session.authentication_confirm_sent = true;
        LOG(INFO) << "DPP Authentication Response validated and Authentication Confirm sent";
        push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_SUCCESS);
        return true;
    }
    case static_cast<uint8_t>(wfa_map::tlv1905EncapDpp::eFrameType::DPP_GAS_FRAME): {
        if (!is_gas) {
            LOG(ERROR) << "DPP GAS frame missing GAS frame indicator";
            return false;
        }

        std::string request_json;
        std::string net_role;
        if (!m_configurator.unwrap_gas_configuration_request(frame, request_json, net_role,
                                                             error)) {
            LOG(WARNING) << "Failed processing DPP GAS Configuration Request: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
            return false;
        }

        m_session.configuration_request_json     = std::move(request_json);
        m_session.requested_net_role             = std::move(net_role);
        m_session.configuration_request_received = true;
        LOG(INFO) << "DPP Configuration Request received for netRole="
                  << m_session.requested_net_role;

        if (!m_session.pending_configuration_objects.empty()) {
            if (!send_dpp_configuration_response(m_session.pending_configuration_objects,
                                                 m_session.pending_send_conn_status)) {
                LOG(WARNING) << "Failed sending staged DPP Configuration Response";
                return false;
            }
        } else {
            // FEAT-68 C-4: build Config Objects from Controller BSS policy.
            std::vector<std::string> policy_objects;
            bool send_conn_status = false;
            if (!controller_dpp::build_dpp_configuration_objects_from_policy(
                    m_database, m_configurator, m_session.proxy_agent,
                    m_session.configuration_request_json, policy_objects, send_conn_status,
                    error)) {
                LOG(WARNING) << "Failed building DPP Configuration Objects from BSS policy: "
                             << error;
                push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
                return false;
            }
            if (!send_dpp_configuration_response(policy_objects, send_conn_status)) {
                LOG(WARNING) << "Failed sending policy DPP Configuration Response";
                return false;
            }
        }

        return true;
    }
    case static_cast<uint8_t>(wfa_map::tlv1905EncapDpp::eFrameType::DPP_CONFIGURATION_RESULT): {
        if (is_gas) {
            LOG(ERROR) << "DPP Configuration Result marked as GAS";
            return false;
        }

        uint8_t status = 0;
        if (!m_configurator.unwrap_configuration_result(frame, status, error)) {
            LOG(WARNING) << "Failed processing DPP Configuration Result: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
            return false;
        }

        m_session.configuration_result_status = status;

        m_session.configuration_result_received = true;
        LOG(INFO) << "DPP Configuration Result received with status=" << int(status);
        if (status != k_dpp_status_ok) {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED,
                                           "DPP Configuration Result status=" +
                                               std::to_string(status));
            return true;
        }
        if (m_active_request_conn_status) {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_RECEIVED,
                                           "wait_conn_status=1");
        } else {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_RECEIVED);
        }
        return true;
    }
    case k_dpp_connection_status_result: {
        if (is_gas) {
            LOG(ERROR) << "DPP Connection Status Result marked as GAS";
            return false;
        }

        uint8_t result = 0;
        if (!m_configurator.unwrap_connection_status_result(frame, result, error)) {
            LOG(WARNING) << "Failed processing DPP Connection Status Result: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONN_STATUS_FAILED, error);
            return false;
        }

        m_session.connection_status_result          = result;
        m_session.connection_status_result_received = true;
        LOG(INFO) << "DPP Connection Status Result received with result=" << int(result);
        if (result != k_dpp_status_ok) {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONN_STATUS_FAILED,
                                           "DPP Connection Status Result=" +
                                               std::to_string(result));
        } else {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONN_STATUS_OK);
        }
        return true;
    }
    default:
        LOG(WARNING) << "Unsupported proxied DPP frame type " << int(frame_type);
        return false;
    }
}

bool dpp_protocol_task::handle_cmdu_1905_direct_encap_dpp(const sMacAddr &src_mac,
                                                          ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto agent = m_database.m_agents.get(src_mac);
    if (!agent || !agent->dpp_onboarding_support) {
        LOG(WARNING) << "DIRECT_ENCAP_DPP_MESSAGE from unknown or non-DPP Agent " << src_mac;
        return false;
    }

    if (!m_session.use_direct_encap ||
        m_session.proxy_agent == beerocks::net::network_utils::ZERO_MAC ||
        src_mac != m_session.proxy_agent) {
        LOG(WARNING) << "DIRECT_ENCAP_DPP_MESSAGE from Agent " << src_mac
                     << " does not match active Direct Encap Agent " << m_session.proxy_agent;
        return false;
    }

    auto dpp_msg_tlv = cmdu_rx.getClass<wfa_map::tlvDppMessage>();
    if (!dpp_msg_tlv) {
        LOG(ERROR) << "DIRECT_ENCAP_DPP_MESSAGE missing DPP Message TLV";
        return false;
    }

    const auto frame_len   = dpp_msg_tlv->dpp_frame_length();
    const auto *frame_data = dpp_msg_tlv->dpp_frame();
    if (!frame_data || frame_len == 0) {
        LOG(ERROR) << "DIRECT_ENCAP_DPP_MESSAGE contains an empty DPP frame";
        return false;
    }
    std::vector<uint8_t> frame(frame_data, frame_data + frame_len);

    uint8_t frame_type          = 0;
    size_t attrs_offset         = 0;
    const bool is_public_action = controller_dpp::find_dpp_public_action_attributes(
        frame.data(), frame.size(), frame_type, attrs_offset);
    (void)attrs_offset;

    std::string error;

    // Authentication Response (DPP Public Action).
    if (is_public_action &&
        frame_type == static_cast<uint8_t>(
                          wfa_map::tlv1905EncapDpp::eFrameType::DPP_AUTHENTICATION_RESPONSE)) {
        if (!m_configurator.authentication_started()) {
            LOG(WARNING) << "Direct DPP Authentication Response without started authentication";
            return false;
        }

        std::vector<uint8_t> auth_confirm;
        if (!m_configurator.handle_authentication_response(frame, auth_confirm, error)) {
            LOG(WARNING) << "Failed processing direct DPP Authentication Response: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_INIT_FAILED, error);
            return false;
        }

        sMacAddr enrollee_mac = beerocks::net::network_utils::ZERO_MAC;
        if (m_session.last_chirp_enrollee_valid) {
            enrollee_mac = m_session.last_chirp_enrollee;
        } else if (m_matched_bootstrap &&
                   m_matched_bootstrap->mac != beerocks::net::network_utils::ZERO_MAC) {
            enrollee_mac = m_matched_bootstrap->mac;
        }

        if (!send_dpp_authentication_confirm(enrollee_mac, std::move(auth_confirm))) {
            LOG(ERROR) << "Failed sending direct DPP Authentication Confirm to Agent " << src_mac;
            push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_INIT_FAILED,
                                           "Failed sending DPP Authentication Confirm");
            return false;
        }

        LOG(INFO) << "Direct DPP Authentication Confirm sent to Agent " << src_mac;
        push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_SUCCESS);
        return true;
    }

    // Configuration Result / Connection Status Result (DPP Public Action).
    if (is_public_action &&
        frame_type ==
            static_cast<uint8_t>(wfa_map::tlv1905EncapDpp::eFrameType::DPP_CONFIGURATION_RESULT)) {
        uint8_t status = 0;
        if (!m_configurator.unwrap_configuration_result(frame, status, error)) {
            LOG(WARNING) << "Failed processing direct DPP Configuration Result: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
            return false;
        }

        m_session.configuration_result_status   = status;
        m_session.configuration_result_received = true;
        LOG(INFO) << "Direct DPP Configuration Result received with status=" << int(status);

        if (status != k_dpp_status_ok) {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED,
                                           "DPP Configuration Result status=" +
                                               std::to_string(status));
            return true;
        }

        if (m_active_request_conn_status) {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_RECEIVED,
                                           "wait_conn_status=1");
        } else {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_RECEIVED);
        }
        return true;
    }

    if (is_public_action && frame_type == k_dpp_connection_status_result) {
        uint8_t result = 0;
        if (!m_configurator.unwrap_connection_status_result(frame, result, error)) {
            LOG(WARNING) << "Failed processing direct DPP Connection Status Result: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONN_STATUS_FAILED, error);
            return false;
        }

        m_session.connection_status_result          = result;
        m_session.connection_status_result_received = true;
        LOG(INFO) << "Direct DPP Connection Status Result received with result=" << int(result);

        if (result != k_dpp_status_ok) {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONN_STATUS_FAILED,
                                           "DPP Connection Status Result=" +
                                               std::to_string(result));
        } else {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONN_STATUS_OK);
        }
        return true;
    }

    // Configuration Request over Direct Encap is a non-GAS DPP Configuration Request
    // (logical Ethernet / mapAgent). Proxied Wi-Fi STA path continues to use GAS.
    if (!is_public_action && m_configurator.authentication_success()) {
        std::string request_json;
        std::string net_role;
        if (!m_configurator.unwrap_configuration_request(frame, request_json, net_role, error)) {
            LOG(WARNING) << "Failed processing direct DPP Configuration Request: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
            return false;
        }

        /*
         * DIRECT_ENCAP_DPP is Controller <-> Multi-AP Agent over 1905. A non-Multi-AP
         * Wi-Fi station (netRole=sta) uses the Proxied GAS path. Keep Direct mapAgent-only.
         */
        if (net_role != "mapAgent") {
            error = "Direct DPP Configuration Request netRole is not mapAgent";
            LOG(WARNING) << error << ": " << net_role;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
            return false;
        }

        m_session.configuration_request_json     = std::move(request_json);
        m_session.requested_net_role             = std::move(net_role);
        m_session.configuration_request_received = true;
        LOG(INFO) << "Direct DPP Configuration Request received for netRole="
                  << m_session.requested_net_role;

        if (!m_session.pending_configuration_objects.empty()) {
            if (!send_dpp_configuration_response(m_session.pending_configuration_objects,
                                                 m_session.pending_send_conn_status)) {
                LOG(WARNING) << "Failed sending staged direct DPP Configuration Response";
                return false;
            }
        } else {
            std::vector<std::string> policy_objects;
            bool send_conn_status = false;
            if (!controller_dpp::build_dpp_configuration_objects_from_policy(
                    m_database, m_configurator, m_session.proxy_agent,
                    m_session.configuration_request_json, policy_objects, send_conn_status,
                    error)) {
                LOG(WARNING) << "Failed building DPP Configuration Objects from BSS policy: "
                             << error;
                push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
                return false;
            }
            if (!send_dpp_configuration_response(policy_objects, send_conn_status)) {
                LOG(WARNING) << "Failed sending policy direct DPP Configuration Response";
                return false;
            }
        }
        return true;
    }

    LOG(WARNING) << "Unsupported direct DPP frame"
                 << (is_public_action ? (" type=" + std::to_string(frame_type))
                                      : " (non-public-action)");
    return false;
}

bool dpp_protocol_task::handle_cmdu_1905_chirp_notification(const sMacAddr &src_mac,
                                                            ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto chirp_tlvs = cmdu_rx.getClassList<wfa_map::tlvDppChirpValue>();
    if (chirp_tlvs.empty()) {
        LOG(ERROR) << "CHIRP_NOTIFICATION_MESSAGE missing DPP chirp value TLV";
        return false;
    }

    bool any_match = false;
    for (const auto &chirp_tlv : chirp_tlvs) {
        if (!chirp_tlv) {
            continue;
        }

        if (!chirp_tlv->flags().hash_validity) {
            LOG(WARNING) << "DPP chirp hash invalid from agent " << src_mac;
            continue;
        }

        const auto hash_len = chirp_tlv->hash_length();
        if (hash_len == 0 || !chirp_tlv->hash()) {
            LOG(WARNING) << "DPP chirp notification missing hash value";
            continue;
        }

        std::string received_hex;
        const auto *matched_info =
            m_database.dpp_chirp_hash_matches(chirp_tlv->hash(), hash_len, received_hex);
        if (!matched_info) {
            LOG(WARNING) << "DPP chirp hash mismatch from agent " << src_mac
                         << " hash_len=" << static_cast<unsigned>(hash_len)
                         << " hash=" << received_hex
                         << " bootstrap_entries=" << (m_database.has_dpp_bootstrap_info() ? "yes" : "no");
            m_database.print_dpp_bootstrap_info();
            continue;
        }

        any_match                  = true;
        m_matched_bootstrap        = matched_info;
        m_session.proxy_agent      = src_mac;
        m_session.use_direct_encap = false;
        LOG(INFO) << "DPP chirp hash matched bootstrapping URI (" << received_hex
                  << ") alias=" << matched_info->alias;

        if (chirp_tlv->flags().enrollee_mac_address_present && chirp_tlv->dest_sta_mac()) {
            m_session.last_chirp_enrollee       = *chirp_tlv->dest_sta_mac();
            m_session.last_chirp_enrollee_valid = true;
            LOG(DEBUG) << "DPP chirp dest STA "
                       << tlvf::mac_to_string(m_session.last_chirp_enrollee);
        } else {
            m_session.last_chirp_enrollee_valid = false;
        }
    }

    if (!any_match) {
        return true;
    }

    return send_dpp_authentication_request();
}

bool dpp_protocol_task::set_direct_encap_onboarding_agent(const sMacAddr &agent_mac)
{
    if (agent_mac == beerocks::net::network_utils::ZERO_MAC) {
        LOG(WARNING) << "Cannot set Direct Encap onboarding agent to ZERO_MAC";
        return false;
    }

    auto agent = m_database.m_agents.get(agent_mac);
    if (!agent || !agent->dpp_onboarding_support) {
        LOG(WARNING) << "Direct Encap Agent " << agent_mac
                     << " is unavailable or does not support DPP";
        return false;
    }

    m_session.use_direct_encap          = true;
    m_session.proxy_agent               = agent_mac;
    m_session.last_chirp_enrollee_valid = false;
    LOG(INFO) << "Direct Encap onboarding Agent set to " << agent_mac;
    return true;
}

bool dpp_protocol_task::start_direct_encap_onboarding(const sMacAddr &agent_mac)
{
    if (!set_direct_encap_onboarding_agent(agent_mac)) {
        return false;
    }

    const db::sDppBootstrappingInfo *info = m_database.get_dpp_bootstrap_info_by_mac(agent_mac);
    if (!info) {
        info = m_database.get_sole_dpp_bootstrap_info();
    }
    if (!info || info->public_key.empty()) {
        LOG(WARNING) << "Direct Encap onboarding requires a provisioned bootstrap URI "
                        "(MAC-matched or sole store entry)";
        m_session.use_direct_encap = false;
        m_session.proxy_agent      = beerocks::net::network_utils::ZERO_MAC;
        return false;
    }

    m_matched_bootstrap = info;
    if (info->mac != beerocks::net::network_utils::ZERO_MAC) {
        m_session.last_chirp_enrollee       = info->mac;
        m_session.last_chirp_enrollee_valid = true;
    }

    LOG(INFO) << "Starting Direct Encap DPP Authentication toward Agent " << agent_mac
              << " alias=" << info->alias;
    return send_dpp_authentication_request();
}

bool dpp_protocol_task::send_dpp_authentication_request()
{
    if (!m_matched_bootstrap || m_matched_bootstrap->public_key.empty()) {
        LOG(WARNING) << "Cannot send DPP Authentication Request without bootstrapping public key";
        return false;
    }

    std::vector<uint8_t> auth_request_frame;
    std::string error;
    if (!m_configurator.start(m_matched_bootstrap->public_key, k_dpp_protocol_version,
                              auth_request_frame, error)) {
        LOG(WARNING) << "Failed building DPP Authentication Request: " << error;
        push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_INIT_FAILED, error);
        return false;
    }

    m_session.authentication_confirm_sent    = false;
    m_session.configuration_request_received = false;
    m_session.configuration_request_json.clear();
    m_session.requested_net_role.clear();
    m_session.configuration_result_received     = false;
    m_session.configuration_result_status       = 0;
    m_session.connection_status_result_received = false;
    m_session.connection_status_result          = 0;
    m_session.pending_configuration_objects.clear();
    m_session.pending_send_conn_status    = false;
    m_session.configuration_response_sent = false;
    m_active_request_conn_status          = false;

    auto session_agent = m_database.m_agents.get(m_session.proxy_agent);
    if (!session_agent || !session_agent->dpp_onboarding_support) {
        LOG(WARNING) << "Session Agent is unavailable or does not support DPP";
        m_configurator.reset();
        push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_INIT_FAILED,
                                       "Agent unavailable or lacks DPP support");
        return false;
    }

    if (m_session.use_direct_encap) {
        // M-8: DIRECT_ENCAP_DPP_MESSAGE carries tlvDppMessage only (no Chirp Value TLV).
        db::sDirectEncapDppMessage direct_message;
        direct_message.frame = std::move(auth_request_frame);
        if (!send_direct_encap_dpp_to_agent(m_session.proxy_agent, direct_message)) {
            LOG(WARNING) << "Failed sending direct DPP Authentication Request";
            m_configurator.reset();
            push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_INIT_FAILED,
                                           "Failed sending DPP Authentication Request");
            return false;
        }
        LOG(INFO) << "Controller DPP Authentication Request sent via Direct Encap to Agent "
                  << m_session.proxy_agent;
        return true;
    }

    sMacAddr enrollee_mac = beerocks::net::network_utils::ZERO_MAC;
    if (m_session.last_chirp_enrollee_valid) {
        enrollee_mac = m_session.last_chirp_enrollee;
    } else if (m_matched_bootstrap->mac != beerocks::net::network_utils::ZERO_MAC) {
        enrollee_mac = m_matched_bootstrap->mac;
    }

    db::sProxiedEncapDppMessage message;
    message.frame = std::move(auth_request_frame);
    message.frame_type =
        static_cast<uint8_t>(wfa_map::tlv1905EncapDpp::eFrameType::DPP_AUTHENTICATION_REQUEST);
    message.dpp_frame_indicator = false;
    message.dest_sta_mac        = enrollee_mac;

    if (m_matched_bootstrap->chirp_hash_valid) {
        message.chirp_hash.assign(m_matched_bootstrap->chirp_hash.begin(),
                                  m_matched_bootstrap->chirp_hash.end());
        message.chirp_hash_valid = true;
    } else if (m_matched_bootstrap->pkhash_valid) {
        message.chirp_hash.assign(m_matched_bootstrap->pkhash.begin(),
                                  m_matched_bootstrap->pkhash.end());
        message.chirp_hash_valid = true;
    }

    if (!send_proxied_encap_dpp_to_agent(m_session.proxy_agent, message)) {
        LOG(WARNING) << "Chirp-selected Proxy Agent rejected the DPP Authentication Request";
        m_configurator.reset();
        push_dpp_onboarding_task_event(dpp_onboarding_task::AUTH_INIT_FAILED,
                                       "Failed sending DPP Authentication Request");
        return false;
    }

    LOG(INFO) << "Controller DPP Authentication Request sent through Proxy Agent "
              << m_session.proxy_agent;
    return true;
}
void dpp_protocol_task::set_pending_configuration_objects(
    std::vector<std::string> config_object_jsons, bool send_conn_status)
{
    m_session.pending_configuration_objects = std::move(config_object_jsons);
    m_session.pending_send_conn_status      = send_conn_status;
    m_session.configuration_response_sent   = false;
    LOG(INFO) << "Staged " << m_session.pending_configuration_objects.size()
              << " DPP Configuration Object(s) for Encap TX"
              << " send_conn_status=" << send_conn_status;
}

bool dpp_protocol_task::send_dpp_authentication_confirm(const sMacAddr &enrollee_mac,
                                                        std::vector<uint8_t> auth_confirm_frame)
{
    if (m_session.proxy_agent == beerocks::net::network_utils::ZERO_MAC) {
        LOG(WARNING) << "Cannot send DPP Authentication Confirm without an active Agent";
        return false;
    }
    if (auth_confirm_frame.empty()) {
        LOG(WARNING) << "Refusing to send empty DPP Authentication Confirm";
        return false;
    }
    if (m_session.use_direct_encap) {
        db::sDirectEncapDppMessage response;
        response.frame = std::move(auth_confirm_frame);
        if (!send_direct_encap_dpp_to_agent(m_session.proxy_agent, response)) {
            return false;
        }
        m_session.authentication_confirm_sent = true;
        return true;
    }

    db::sProxiedEncapDppMessage response;
    response.frame = std::move(auth_confirm_frame);
    response.frame_type =
        static_cast<uint8_t>(wfa_map::tlv1905EncapDpp::eFrameType::DPP_AUTHENTICATION_CONFIRM);
    response.dpp_frame_indicator = false;
    response.dest_sta_mac        = enrollee_mac;

    if (!send_proxied_encap_dpp_to_agent(m_session.proxy_agent, response)) {
        return false;
    }

    m_session.authentication_confirm_sent = true;
    return true;
}

bool dpp_protocol_task::send_dpp_configuration_response(
    const std::vector<std::string> &config_object_jsons, bool send_conn_status)
{
    if (m_session.proxy_agent == beerocks::net::network_utils::ZERO_MAC) {
        LOG(WARNING) << "Cannot send DPP Configuration Response without an active Agent";
        return false;
    }
    if (config_object_jsons.empty()) {
        LOG(WARNING) << "Cannot send DPP Configuration Response without Config Objects";
        return false;
    }
    if (m_session.use_direct_encap) {
        // Direct / logical Ethernet: non-GAS Configuration Response in tlvDppMessage.
        std::vector<uint8_t> response_frame;
        std::string error;
        if (!m_configurator.build_configuration_response(config_object_jsons, response_frame, error,
                                                         send_conn_status)) {
            LOG(WARNING) << "Failed building direct DPP Configuration Response: " << error;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
            return false;
        }

        db::sDirectEncapDppMessage message;
        message.frame = std::move(response_frame);
        if (!send_direct_encap_dpp_to_agent(m_session.proxy_agent, message)) {
            LOG(ERROR) << "Failed sending direct DPP Configuration Response to Agent "
                       << m_session.proxy_agent;
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED,
                                           "Failed sending DPP Configuration Response");
            return false;
        }

        m_session.configuration_response_sent = true;
        m_session.pending_configuration_objects.clear();
        m_session.pending_send_conn_status = false;
        m_active_request_conn_status       = send_conn_status;
        LOG(INFO) << "DPP Configuration Response sent via Direct Encap to Agent "
                  << m_session.proxy_agent << " netRole=" << m_session.requested_net_role;
        if (send_conn_status) {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_SENT, "wait_conn_status=1");
        } else {
            push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_SENT);
        }
        return true;
    }

    sMacAddr enrollee_mac = beerocks::net::network_utils::ZERO_MAC;
    if (m_session.last_chirp_enrollee_valid) {
        enrollee_mac = m_session.last_chirp_enrollee;
    } else if (m_matched_bootstrap &&
               m_matched_bootstrap->mac != beerocks::net::network_utils::ZERO_MAC) {
        enrollee_mac = m_matched_bootstrap->mac;
    }

    std::vector<uint8_t> gas_frame;
    std::string error;
    if (!m_configurator.build_gas_configuration_response(config_object_jsons, gas_frame, error,
                                                         send_conn_status)) {
        LOG(WARNING) << "Failed building DPP GAS Configuration Response: " << error;
        push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED, error);
        return false;
    }

    db::sProxiedEncapDppMessage message;
    message.frame      = std::move(gas_frame);
    message.frame_type = static_cast<uint8_t>(wfa_map::tlv1905EncapDpp::eFrameType::DPP_GAS_FRAME);
    message.dpp_frame_indicator = true;
    message.dest_sta_mac        = enrollee_mac;

    if (!send_proxied_encap_dpp_to_agent(m_session.proxy_agent, message)) {
        LOG(ERROR) << "Failed sending DPP Configuration Response through Proxy Agent "
                   << m_session.proxy_agent;
        push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_FAILED,
                                       "Failed sending DPP Configuration Response");
        return false;
    }

    m_session.configuration_response_sent = true;
    m_session.pending_configuration_objects.clear();
    m_session.pending_send_conn_status = false;
    m_active_request_conn_status       = send_conn_status;
    LOG(INFO) << "DPP Configuration Response sent through Proxy Agent " << m_session.proxy_agent
              << " netRole=" << m_session.requested_net_role;
    if (send_conn_status) {
        push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_SENT, "wait_conn_status=1");
    } else {
        push_dpp_onboarding_task_event(dpp_onboarding_task::CONF_SENT);
    }
    return true;
}

bool dpp_protocol_task::send_proxied_encap_dpp_to_agent(const sMacAddr &agent_mac,
                                                        const db::sProxiedEncapDppMessage &message)
{
    if (agent_mac == beerocks::net::network_utils::ZERO_MAC) {
        LOG(WARNING) << "Missing target agent for PROXIED_ENCAP_DPP_MESSAGE";
        return false;
    }
    if (message.frame.empty()) {
        LOG(WARNING) << "Refusing to send empty PROXIED_ENCAP_DPP_MESSAGE";
        return false;
    }

    const bool dest_present = message.dest_sta_mac != beerocks::net::network_utils::ZERO_MAC;

    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type PROXIED_ENCAP_DPP_MESSAGE failed!";
        return false;
    }

    auto encap_1905_dpp_tlv = m_cmdu_tx.addClass<wfa_map::tlv1905EncapDpp>();
    if (!encap_1905_dpp_tlv) {
        LOG(ERROR) << "addClass wfa_map::tlv1905EncapDpp failed!";
        return false;
    }

    encap_1905_dpp_tlv->frame_type() =
        static_cast<wfa_map::tlv1905EncapDpp::eFrameType>(message.frame_type);
    encap_1905_dpp_tlv->frame_flags().dpp_frame_indicator          = message.dpp_frame_indicator;
    encap_1905_dpp_tlv->frame_flags().enrollee_mac_address_present = dest_present;
    if (dest_present) {
        encap_1905_dpp_tlv->set_dest_sta_mac(message.dest_sta_mac);
    }

    if (!encap_1905_dpp_tlv->set_encapsulated_frame(message.frame.data(), message.frame.size())) {
        LOG(ERROR) << "Failed setting proxied DPP payload";
        return false;
    }

    if (message.chirp_hash_valid && !message.chirp_hash.empty()) {
        auto chirp_tlv = m_cmdu_tx.addClass<wfa_map::tlvDppChirpValue>();
        if (!chirp_tlv) {
            LOG(ERROR) << "addClass wfa_map::tlvDppChirpValue failed";
            return false;
        }
        chirp_tlv->flags().hash_validity                = true;
        chirp_tlv->flags().enrollee_mac_address_present = dest_present;
        if (dest_present) {
            chirp_tlv->set_dest_sta_mac(message.dest_sta_mac);
        }
        if (!chirp_tlv->set_hash(message.chirp_hash.data(), message.chirp_hash.size())) {
            LOG(ERROR) << "Failed setting chirp hash payload";
            return false;
        }
        chirp_tlv->hash_length() = message.chirp_hash.size();
    }
    return son_actions::send_cmdu_to_agent(agent_mac, m_cmdu_tx, m_database);
}

bool dpp_protocol_task::send_direct_encap_dpp_to_agent(const sMacAddr &agent_mac,
                                                       const db::sDirectEncapDppMessage &message)
{
    if (agent_mac == beerocks::net::network_utils::ZERO_MAC) {
        LOG(WARNING) << "Missing target agent for DIRECT_ENCAP_DPP_MESSAGE";
        return false;
    }
    if (message.frame.empty()) {
        LOG(WARNING) << "Refusing to send empty DIRECT_ENCAP_DPP_MESSAGE";
        return false;
    }

    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::DIRECT_ENCAP_DPP_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type DIRECT_ENCAP_DPP_MESSAGE failed!";
        return false;
    }

    auto dpp_msg_tlv = m_cmdu_tx.addClass<wfa_map::tlvDppMessage>();
    if (!dpp_msg_tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvDppMessage failed!";
        return false;
    }

    if (!dpp_msg_tlv->set_dpp_frame(message.frame.data(), message.frame.size())) {
        LOG(ERROR) << "Failed setting DPP frame payload";
        return false;
    }

    // EasyMesh Direct Encap: tlvDppMessage only — do not append Chirp Value TLV.
    return son_actions::send_cmdu_to_agent(agent_mac, m_cmdu_tx, m_database);
}

} // namespace son
