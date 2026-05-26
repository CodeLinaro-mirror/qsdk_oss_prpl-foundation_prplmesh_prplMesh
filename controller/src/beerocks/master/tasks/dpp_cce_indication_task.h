/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_CCE_INDICATION_TASK_H_
#define _DPP_CCE_INDICATION_TASK_H_

#include "../db/db.h"
#include "task.h"
#include "task_pool.h"

#include <tlvf/wfa_map/tlvDppCceIndication.h>

namespace son {

/**
 * @brief Sends DPP CCE Indication (Multi-AP) to agents that advertise DPP onboarding support.
 *
 * Builds one DPP_CCE_INDICATION_MESSAGE with tlvDppCceIndication and transmits it to each
 * connected agent whose database entry has dpp_onboarding_support set.
 */
class dpp_cce_indication_task : public task {
public:
    dpp_cce_indication_task(db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_,
                            wfa_map::tlvDppCceIndication::eAdvertiseCee advertise_cee_,
                            const std::string &task_name = std::string("dpp_cce_indication_task"));
    ~dpp_cce_indication_task() override = default;

protected:
    void work() override;

private:
    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    wfa_map::tlvDppCceIndication::eAdvertiseCee m_advertise_cee;
    bool m_completed = false;
};

} // namespace son

#endif // _DPP_CCE_INDICATION_TASK_H_
