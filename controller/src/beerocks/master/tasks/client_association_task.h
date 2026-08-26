/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _CLIENT_ASSOCIATION_TASK_H_
#define _CLIENT_ASSOCIATION_TASK_H_

#include "../db/db.h"
#include "task.h"
#include "task_pool.h"

#include <chrono>
#include <unordered_map>

namespace son {

class client_association_task : public task {
public:
    /**
     * @param association_event_timeout Time to wait for the Client Capability Report of a join.
     * Without a report in this time, the Associated! event of the join is emitted with the basic
     * arguments only.
     */
    client_association_task(
        db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_, task_pool &tasks_,
        std::chrono::milliseconds association_event_timeout = std::chrono::seconds(3),
        const std::string &task_name_ = std::string("client_association_task"));
    bool handle_ieee1905_1_msg(const sMacAddr &src_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx) override;

protected:
    void work() override;

private:
    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    task_pool &m_tasks;
    const std::chrono::milliseconds m_association_event_timeout;

    /**
     * @brief A join whose Associated! event waits for the Client Capability Report.
     */
    struct sPendingAssociationEvent {
        sMacAddr bssid;
        std::chrono::steady_clock::time_point deadline;
    };

    /**
     * Joins whose Associated! event is not emitted yet, by station MAC address.
     */
    std::unordered_map<sMacAddr, sPendingAssociationEvent> m_pending_association_events;

    /**
     * @brief If STA associate first time send Client Capability Query message.
     * Save mac address and timestamp of associated STA.
     * 
     * @param src_mac MAC address of Agent which reported new client association.
     * @param cmdu_rx TOPOLOGY_NOTIFICATION_MESSAGE.
     * @return True if client's associated first time, false otherwise.
     */
    bool verify_sta_association(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Fill up Client Info TLV with all needed information, add this TLV
     * to the Client Capability Query Message, send this message to the Agent.
     * 
     * @param src_mac MAC address of Agent which reported new client association.
     * @param cmdu_rx TOPOLOGY_NOTIFICATION_MESSAGE.
     * @return True if message was successfully sent, false otherwise.
     */
    bool send_sta_capability_query(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    bool handle_cmdu_1905_client_capability_report_message(const sMacAddr &src_mac,
                                                           ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Emits the Associated! event of the latest join of a station, once per join.
     *
     * Emits nothing if the event of the join is already emitted, or if the latest join of the
     * station is not to @a bssid.
     *
     * @param sta_mac MAC address of the station.
     * @param bssid BSSID of the BSS the station joined.
     * @param report Data of the Client Capability Report, nullptr if the report has none.
     */
    void emit_association_event(const sMacAddr &sta_mac, const sMacAddr &bssid,
                                const db::sAssociationReport *report);
};

} // namespace son

#endif
