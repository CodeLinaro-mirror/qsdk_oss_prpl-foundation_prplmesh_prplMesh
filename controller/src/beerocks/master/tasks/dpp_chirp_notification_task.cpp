/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 Tata Elxsi
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_chirp_notification_task.h"

#include <bcl/network/network_utils.h>
#include <easylogging++.h>
#include <tlvf/common/sMacAddr.h>
#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/tlvftypes.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>

namespace son {

namespace {

std::string hash_to_key(const uint8_t *hash, uint8_t hash_length)
{
    if (!hash || hash_length == 0) {
        return {};
    }
    return std::string(reinterpret_cast<const char *>(hash), hash_length);
}

bool validate_dpp_chirp_value_tlv(wfa_map::tlvDppChirpValue &chirp_tlv)
{
    if (chirp_tlv.flags().reserved != 0) {
        LOG(WARNING) << "DPP Chirp Value TLV reserved bits are not zero: "
                     << int(chirp_tlv.flags().reserved);
    }

    if (chirp_tlv.flags().enrollee_mac_address_present) {
        auto dest_mac = chirp_tlv.dest_sta_mac();
        if (!dest_mac) {
            LOG(ERROR) << "DPP Chirp Value TLV: enrollee MAC present bit set but "
                          "Destination STA MAC Address is missing";
            return false;
        }
        if (*dest_mac == beerocks::net::network_utils::ZERO_MAC) {
            LOG(ERROR) << "DPP Chirp Value TLV: invalid zero Destination STA MAC Address";
            return false;
        }
    } else if (chirp_tlv.dest_sta_mac()) {
        LOG(ERROR) << "DPP Chirp Value TLV: Destination STA MAC present but "
                      "enrollee MAC present bit is clear";
        return false;
    }

    if (chirp_tlv.hash_length() == 0) {
        LOG(ERROR) << "DPP Chirp Value TLV: hash length is zero";
        return false;
    }

    return true;
}

} // namespace

dpp_chirp_notification_task::dpp_chirp_notification_task(db &database_,
                                                         const std::string &task_name)
    : task(task_name), m_database(database_)
{
}

bool dpp_chirp_notification_task::handle_ieee1905_1_msg(const sMacAddr &src_mac,
                                                        ieee1905_1::CmduMessageRx &cmdu_rx)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::CHIRP_NOTIFICATION_MESSAGE:
        return handle_chirp_notification_message(src_mac, cmdu_rx);
    default:
        return false;
    }
}

bool dpp_chirp_notification_task::handle_chirp_notification_message(
    const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx)
{
    LOG(DEBUG) << "Received CHIRP_NOTIFICATION_MESSAGE from " << src_mac << " mid=" << std::hex
               << cmdu_rx.getMessageId();

    auto agent = m_database.m_agents.get(src_mac);
    if (!agent) {
        LOG(ERROR) << "CHIRP_NOTIFICATION_MESSAGE from unknown agent " << src_mac;
        return false;
    }

    bool handled = false;
    for (auto chirp_tlv : cmdu_rx.getClassList<wfa_map::tlvDppChirpValue>()) {
        if (!chirp_tlv) {
            LOG(ERROR) << "getClassList wfa_map::tlvDppChirpValue failed";
            return false;
        }
        if (!handle_dpp_chirp_value_tlv(agent, *chirp_tlv)) {
            LOG(ERROR) << "Failed to handle tlvDppChirpValue from agent " << src_mac;
            continue;
        }
        handled = true;
    }

    if (!handled) {
        LOG(ERROR) << "CHIRP_NOTIFICATION_MESSAGE without tlvDppChirpValue from " << src_mac;
        return false;
    }

    return true;
}

bool dpp_chirp_notification_task::handle_dpp_chirp_value_tlv(std::shared_ptr<Agent> agent,
                                                             wfa_map::tlvDppChirpValue &chirp_tlv)
{
    if (!validate_dpp_chirp_value_tlv(chirp_tlv)) {
        return false;
    }

    const auto hash_key  = hash_to_key(chirp_tlv.hash(), chirp_tlv.hash_length());
    const bool establish = chirp_tlv.flags().hash_validity;

    if (establish) {
        Agent::sDppChirpAuthenticationEntry entry;
        entry.enrollee_mac_address_present = chirp_tlv.flags().enrollee_mac_address_present;
        if (entry.enrollee_mac_address_present) {
            entry.enrollee_mac = *chirp_tlv.dest_sta_mac();
        }
        agent->dpp_chirp_authentication_state[hash_key] = entry;

        LOG(INFO) << "DPP chirp establish hash_len=" << int(chirp_tlv.hash_length())
                  << " enrollee_mac_present=" << entry.enrollee_mac_address_present
                  << " enrollee_mac="
                  << (entry.enrollee_mac_address_present ? tlvf::mac_to_string(entry.enrollee_mac)
                                                         : "N/A")
                  << " agent=" << agent->al_mac;
    } else {
        const auto erased = agent->dpp_chirp_authentication_state.erase(hash_key);
        LOG(INFO) << "DPP chirp purge hash_len=" << int(chirp_tlv.hash_length())
                  << " agent=" << agent->al_mac
                  << (erased ? " (state removed)" : " (no prior state)");
    }

    return true;
}

} // namespace son
