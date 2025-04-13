/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _SERVICE_PRIORITIZATION_TASK_H_
#define _SERVICE_PRIORITIZATION_TASK_H_

#include "task.h"

#include <db/db.h>

#include <memory>

namespace wfa_map {
class tlvQoSManagementDescriptor;
};

namespace son {

class service_prioritization_task : public task {

public:
    service_prioritization_task(db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_,
                                const std::string &task_name_ = std::string());
    virtual ~service_prioritization_task() {}

    bool handle_ieee1905_1_msg(const sMacAddr &src_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx) override;

protected:
    void work() override{};

private:
    bool handle_cmdu_1905_qos_management_notification_message(const sMacAddr &src_mac,
                                                              ieee1905_1::CmduMessageRx &cmdu_rx);

private:
    db &m_db;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    std::unordered_map<uint16_t, std::shared_ptr<wfa_map::tlvQoSManagementDescriptor>>
        m_descriptors;

    /**
     * @brief next number of QMID to be used, incrementing after usage is required
     **/
    uint64_t m_qmid_next{};
};

} // namespace son

#endif // _SERVICE_PRIORITIZATION_TASK_H_
