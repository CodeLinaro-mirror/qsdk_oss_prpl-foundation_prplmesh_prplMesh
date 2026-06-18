/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 Tata Elxsi
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_CHIRP_NOTIFICATION_TASK_H_
#define _DPP_CHIRP_NOTIFICATION_TASK_H_

#include "../db/db.h"
#include "task.h"
#include <tlvf/wfa_map/tlvDppChirpValue.h>

namespace son {

/**
 * @brief Handles CHIRP_NOTIFICATION_MESSAGE from proxy agents (Multi-AP 17.2.83).
 *
 * Parses tlvDppChirpValue and establishes or purges DPP authentication state per hash.
 */
class dpp_chirp_notification_task : public task {
public:
    dpp_chirp_notification_task(
        db &database_, const std::string &task_name = std::string("dpp_chirp_notification_task"));
    ~dpp_chirp_notification_task() override = default;

    bool handle_ieee1905_1_msg(const sMacAddr &src_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx) override;

protected:
    void work() override {}

private:
    bool handle_chirp_notification_message(const sMacAddr &src_mac,
                                           ieee1905_1::CmduMessageRx &cmdu_rx);

    bool handle_dpp_chirp_value_tlv(std::shared_ptr<Agent> agent,
                                    wfa_map::tlvDppChirpValue &chirp_tlv);

    db &m_database;
};

} // namespace son

#endif // _DPP_CHIRP_NOTIFICATION_TASK_H_
