/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 Tata Elxsi
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_cce_indication_task.h"

#include "../son_actions.h"

#include <tlvf/ieee_1905_1/eMessageType.h>

#include <easylogging++.h>

namespace son {

uint64_t dpp_cce_indication_task::s_latest_seq = 0;
wfa_map::tlvDppCceIndication::eAdvertiseCee dpp_cce_indication_task::s_latest_advertise =
    wfa_map::tlvDppCceIndication::eAdvertiseCee::DISABLE;

dpp_cce_indication_task::dpp_cce_indication_task(
    db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_,
    wfa_map::tlvDppCceIndication::eAdvertiseCee advertise_cee_, const std::string &task_name,
    const sMacAddr &target_al_mac)
    : task(task_name), m_database(database_), m_cmdu_tx(cmdu_tx_), m_advertise_cee(advertise_cee_),
      m_target_al_mac(target_al_mac), m_seq(++s_latest_seq)
{
    s_latest_advertise = advertise_cee_;
}

bool dpp_cce_indication_task::send_to_agent(const sMacAddr &al_mac)
{
    // The shared cmdu_tx may have been reused by other tasks, so build the CMDU per send.
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::DPP_CCE_INDICATION_MESSAGE)) {
        LOG(ERROR) << "Failed to create DPP_CCE_INDICATION_MESSAGE";
        return false;
    }

    auto tlv = m_cmdu_tx.addClass<wfa_map::tlvDppCceIndication>();
    if (!tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvDppCceIndication failed";
        return false;
    }
    tlv->advertise_cee() = m_advertise_cee;

    if (!son_actions::send_cmdu_to_agent(al_mac, m_cmdu_tx, m_database)) {
        LOG(ERROR) << "Failed to send DPP_CCE_INDICATION_MESSAGE advertise_cee=" << m_advertise_cee
                   << " to agent " << al_mac;
        return false;
    }

    LOG(DEBUG) << "Sent DPP_CCE_INDICATION_MESSAGE advertise_cee=" << m_advertise_cee
               << " to agent " << al_mac;
    return true;
}

void dpp_cce_indication_task::work()
{
    if (m_completed) {
        return;
    }

    if (m_retry_pending) {
        m_completed = true;
        if (s_latest_seq != m_seq && s_latest_advertise != m_advertise_cee) {
            LOG(INFO) << "DPP CCE Indication retry advertise_cee=" << m_advertise_cee
                      << " dropped: superseded by a newer indication";
            finish();
            return;
        }
        for (const auto &al_mac : m_retry_agents) {
            auto agent = m_database.get_agent(al_mac);
            if (!agent || agent->state != beerocks::STATE_CONNECTED ||
                !agent->dpp_onboarding_support) {
                LOG(DEBUG) << "DPP CCE Indication retry skipped for agent " << al_mac
                           << ": no longer connected or no DPP onboarding support";
                continue;
            }
            if (!send_to_agent(al_mac)) {
                LOG(ERROR) << "DPP CCE Indication retry failed for agent " << al_mac
                           << "; next AP Capability Report or URI change will resend";
            }
        }
        finish();
        return;
    }

    std::vector<sMacAddr> targets;
    if (m_target_al_mac != beerocks::net::network_utils::ZERO_MAC) {
        targets.push_back(m_target_al_mac);
    } else {
        for (const auto &agent : m_database.get_all_connected_agents()) {
            if (agent->dpp_onboarding_support) {
                targets.push_back(agent->al_mac);
            }
        }
    }

    if (targets.empty()) {
        LOG(DEBUG) << "No connected agent with DPP onboarding support; nothing sent";
        m_completed = true;
        finish();
        return;
    }

    for (const auto &al_mac : targets) {
        if (!send_to_agent(al_mac)) {
            m_retry_agents.push_back(al_mac);
        }
    }

    if (m_retry_agents.empty()) {
        m_completed = true;
        finish();
        return;
    }

    LOG(WARNING) << "DPP CCE Indication advertise_cee=" << m_advertise_cee << " failed for "
                 << m_retry_agents.size() << " agent(s); retrying once in " << k_retry_delay_ms
                 << " ms";
    m_retry_pending = true;
    wait_for(k_retry_delay_ms);
}

} // namespace son
