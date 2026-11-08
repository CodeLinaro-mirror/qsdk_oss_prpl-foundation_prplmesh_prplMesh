/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 Tata Elxsi
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_CCE_INDICATION_TASK_H_
#define _DPP_CCE_INDICATION_TASK_H_

#include "../db/db.h"
#include "task.h"
#include "task_pool.h"

#include <bcl/network/network_utils.h>
#include <tlvf/wfa_map/tlvDppCceIndication.h>

#include <cstdint>
#include <vector>

namespace son {

/**
 * @brief Sends DPP CCE Indication (Multi-AP) to agents that advertise DPP onboarding support.
 *
 * Builds one DPP_CCE_INDICATION_MESSAGE with tlvDppCceIndication and transmits it to each
 * connected agent whose database entry has dpp_onboarding_support set, or only to
 * target_al_mac when it is not ZERO_MAC.
 *
 * Agents whose send fails are retried once after k_retry_delay_ms. The retry is skipped when a
 * newer CCE Indication task with the opposite value was created in the meantime, so a stale
 * ENABLE cannot override a later DISABLE (or the reverse).
 */
class dpp_cce_indication_task : public task {
public:
    dpp_cce_indication_task(db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_,
                            wfa_map::tlvDppCceIndication::eAdvertiseCee advertise_cee_,
                            const std::string &task_name = std::string("dpp_cce_indication_task"),
                            const sMacAddr &target_al_mac = beerocks::net::network_utils::ZERO_MAC);
    ~dpp_cce_indication_task() override = default;

protected:
    void work() override;

private:
    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    wfa_map::tlvDppCceIndication::eAdvertiseCee m_advertise_cee;
    bool m_completed = false;
    sMacAddr m_target_al_mac;
    bool m_retry_pending = false;
    std::vector<sMacAddr> m_retry_agents;
    uint64_t m_seq = 0;

    static constexpr int k_retry_delay_ms = 5000;
    // Sequence and value of the most recently created task, used to drop superseded retries.
    static uint64_t s_latest_seq;
    static wfa_map::tlvDppCceIndication::eAdvertiseCee s_latest_advertise;

    bool send_to_agent(const sMacAddr &al_mac);
};

} // namespace son

#endif // _DPP_CCE_INDICATION_TASK_H_
