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

dpp_cce_indication_task::dpp_cce_indication_task(
    db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_,
    wfa_map::tlvDppCceIndication::eAdvertiseCee advertise_cee_, const std::string &task_name)
    : task(task_name), m_database(database_), m_cmdu_tx(cmdu_tx_), m_advertise_cee(advertise_cee_)
{
}

void dpp_cce_indication_task::work()
{
    if (m_completed) {
        return;
    }
    m_completed = true;

    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::DPP_CCE_INDICATION_MESSAGE)) {
        LOG(ERROR) << "Failed to create DPP_CCE_INDICATION_MESSAGE";
        finish();
        return;
    }

    auto tlv = m_cmdu_tx.addClass<wfa_map::tlvDppCceIndication>();
    if (!tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvDppCceIndication failed";
        finish();
        return;
    }

    tlv->advertise_cee() = m_advertise_cee;

    bool any_sent = false;
    for (const auto &agent : m_database.get_all_connected_agents()) {
        if (!agent->dpp_onboarding_support) {
            continue;
        }
        if (!son_actions::send_cmdu_to_agent(agent->al_mac, m_cmdu_tx, m_database)) {
            LOG(ERROR) << "Failed to send DPP_CCE_INDICATION_MESSAGE to agent " << agent->al_mac;
            continue;
        }
        any_sent = true;
        LOG(DEBUG) << "Sent DPP_CCE_INDICATION_MESSAGE advertise_cee=" << m_advertise_cee
                   << " to agent " << agent->al_mac;
    }

    if (!any_sent) {
        LOG(DEBUG) << "No connected agent with DPP onboarding support; nothing sent";
    }

    finish();
}

} // namespace son
