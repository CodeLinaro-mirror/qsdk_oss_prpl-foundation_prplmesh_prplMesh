/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef _SENSING_EXCHANGE_TASK_H_
#define _SENSING_EXCHANGE_TASK_H_

#include "../db/db.h"
#include "task.h"
#include "task_pool.h"

#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/common/sMacAddr.h>

namespace son {

class sensing_exchange_task : public task {
public:
    sensing_exchange_task(db &database,
                          ieee1905_1::CmduMessageTx &cmdu_tx,
                          task_pool &tasks,
                          uint32_t exchange_id,
                          bool add_exchange,
                          uint8_t exchange_type,
                          uint16_t rate_tu10,
                          uint16_t bandwidth_mhz,
                          uint16_t ntx,
                          uint16_t nrx,
                          uint32_t data_type_mask,
                          uint8_t csi_threshold,
                          bool tx_mac_valid,
                          bool rx_mac_valid,
                          const sMacAddr &tx_mac,
                          const sMacAddr &rx_mac,
                          const sMacAddr &agent_mac,
                          const std::string &task_name = std::string("sensing_exchange_task"));
    virtual ~sensing_exchange_task() {}

    /** Parse SENSING_EXCHANGE_RESPONSE_MESSAGE; reply with ACK. Called from Controller. */
    static bool handle_sensing_exchange_response(db &database,
                                                 ieee1905_1::CmduMessageTx &cmdu_tx,
                                                 const sMacAddr &src_mac,
                                                 ieee1905_1::CmduMessageRx &cmdu_rx);

protected:
    virtual void work() override;

private:
    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    task_pool &m_tasks;

    const uint32_t m_exchange_id;
    const bool m_add_exchange;
    const uint8_t m_exchange_type;
    const uint16_t m_rate_tu10;
    const uint16_t m_bandwidth_mhz;
    const uint16_t m_ntx;
    const uint16_t m_nrx;
    const uint32_t m_data_type_mask;
    const uint8_t m_csi_threshold;
    const bool m_tx_mac_valid;
    const bool m_rx_mac_valid;
    const sMacAddr m_tx_mac;
    const sMacAddr m_rx_mac;
    const sMacAddr m_agent_mac;
    bool send_sensing_exchange_request();
};

} // namespace son

#endif // _SENSING_EXCHANGE_TASK_H_
