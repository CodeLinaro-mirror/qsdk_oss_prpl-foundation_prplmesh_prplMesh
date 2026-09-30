/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef _DATAPATH_SETUP_TASK_H_
#define _DATAPATH_SETUP_TASK_H_

#include "../db/db.h"
#include "task.h"
#include "task_pool.h"

#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>

namespace son {

class datapath_setup_task : public task {
public:
    datapath_setup_task(db &database, ieee1905_1::CmduMessageTx &cmdu_tx, task_pool &tasks,
                        const std::string &dest_ip, uint16_t dest_port, bool add_path,
                        const sMacAddr &agent_mac,
                        const std::string &task_name = std::string("datapath_setup_task"));
    virtual ~datapath_setup_task() {}

    /** Parse DATA_PATH_SETUP_RESPONSE_MESSAGE; reply with ACK. Called from Controller. */
    static bool handle_data_path_setup_response(db &database,
                                                ieee1905_1::CmduMessageTx &cmdu_tx,
                                                const sMacAddr &src_mac,
                                                ieee1905_1::CmduMessageRx &cmdu_rx);

protected:
    virtual void work() override;

private:
    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    task_pool &m_tasks;
    const std::string m_dest_ip;
    const uint16_t m_dest_port;
    const bool m_add_path;
    const sMacAddr m_agent_mac;
};

} // namespace son

#endif // _DATAPATH_SETUP_TASK_H_
