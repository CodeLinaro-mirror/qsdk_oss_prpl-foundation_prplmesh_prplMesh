/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_protocol_task.h"

#include "../db/db.h"
#include "../son_actions.h"

#include <bcl/network/network_utils.h>
#include <easylogging++.h>
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>

namespace son {

namespace {
constexpr uint8_t k_dpp_protocol_version = 2;
} // namespace

dpp_protocol_task::dpp_protocol_task(db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_)
    : task("dpp_protocol_task"), m_database(database_), m_cmdu_tx(cmdu_tx_)
{
}

bool dpp_protocol_task::handle_ieee1905_1_msg(const sMacAddr &src_mac,
                                              ieee1905_1::CmduMessageRx &cmdu_rx)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::CHIRP_NOTIFICATION_MESSAGE:
        return handle_cmdu_1905_chirp_notification(src_mac, cmdu_rx);
    default:
        return false;
    }
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
        const auto *matched_info = m_database.dpp_chirp_hash_matches(chirp_tlv->hash(), hash_len,
                                                                     received_hex);
        if (!matched_info) {
            LOG(WARNING) << "DPP chirp hash mismatch from agent " << src_mac
                         << " hash=" << received_hex;
            continue;
        }

        any_match              = true;
        m_matched_bootstrap    = matched_info;
        m_session.proxy_agent  = src_mac;
        LOG(INFO) << "DPP chirp hash matched bootstrapping URI (" << received_hex << ") alias="
                  << matched_info->alias;

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
        return false;
    }

    sMacAddr enrollee_mac = beerocks::net::network_utils::ZERO_MAC;
    if (m_session.last_chirp_enrollee_valid) {
        enrollee_mac = m_session.last_chirp_enrollee;
    } else if (m_matched_bootstrap->mac != beerocks::net::network_utils::ZERO_MAC) {
        enrollee_mac = m_matched_bootstrap->mac;
    }

    db::sProxiedEncapDppMessage message;
    message.frame      = std::move(auth_request_frame);
    message.frame_type = static_cast<uint8_t>(
        wfa_map::tlv1905EncapDpp::eFrameType::DPP_AUTHENTICATION_REQUEST);
    message.dpp_frame_indicator = false;
    message.dest_sta_mac          = enrollee_mac;

    if (m_matched_bootstrap->pkhash_valid) {
        message.chirp_hash.assign(m_matched_bootstrap->pkhash.begin(),
                                  m_matched_bootstrap->pkhash.end());
        message.chirp_hash_valid = true;
    }

    bool any_sent = false;
    for (const auto &entry : m_database.m_agents) {
        if (!entry.second || !entry.second->dpp_onboarding_support) {
            continue;
        }
        if (send_proxied_encap_dpp_to_agent(entry.first, message)) {
            any_sent = true;
        }
    }

    if (!any_sent) {
        LOG(WARNING) << "No DPP-capable agents accepted the DPP Authentication Request";
        m_configurator.reset();
        return false;
    }

    LOG(INFO) << "Controller DPP Authentication Request sent to DPP-capable agents";
    return true;
}

bool dpp_protocol_task::send_proxied_encap_dpp_to_agent(
    const sMacAddr &agent_mac, const db::sProxiedEncapDppMessage &message)
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

} // namespace son
