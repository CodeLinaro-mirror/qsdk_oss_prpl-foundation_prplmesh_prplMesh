/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _IEEE1905_TASK_H_
#define _IEEE1905_TASK_H_

#include "../db/db.h"
#include "task.h"

#include <chrono>
#include <functional>
#include <memory>
#include <unordered_map>

namespace son {

class IEEE1905QuerySender {
public:
    virtual ~IEEE1905QuerySender() = default;

    virtual bool send_topology_query(const sMacAddr &dest_mac,
                                     ieee1905_1::CmduMessageTx &cmdu_tx)     = 0;
    virtual bool send_higher_layer_query(const sMacAddr &dest_mac,
                                         ieee1905_1::CmduMessageTx &cmdu_tx) = 0;
    virtual bool send_link_metric_query(const sMacAddr &dest_mac,
                                        ieee1905_1::CmduMessageTx &cmdu_tx)  = 0;
};

class ieee1905_task : public task {
public:
    using steady_clock = std::chrono::steady_clock;
    using time_point   = steady_clock::time_point;
    using now_f        = std::function<time_point()>;

    enum eEventType {
        IEEE1905_NETWORK_ENABLE_CHANGED = 1,
    };

    static constexpr std::chrono::seconds topology_response_timeout{2};
    static constexpr std::chrono::seconds higher_layer_response_timeout{1};
    static constexpr std::chrono::seconds periodic_topology_requery_interval{30};
    /** to not to interfere with link metric task: */
    static constexpr std::chrono::seconds link_metric_response_requery_delay_guard{1};

    ieee1905_task(db &database, ieee1905_1::CmduMessageTx &cmdu_tx,
                  std::unique_ptr<IEEE1905QuerySender> query_sender, now_f now = steady_clock::now);

    bool handle_ieee1905_1_msg(const sMacAddr &src_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx) override;

protected:
    /**
     * @brief Sidecar to db::ieee1905_network_db::sAL, contains lifetime management data
     *
     * Whenever the sidecar exist, the sAL exists too (possibly default-constructed).
     * The opposite may not be true (failed first topology response).
     */
    struct AL {
        time_point first_topology_query_deadline     = time_point::max();
        time_point first_higher_layer_query_deadline = time_point::max();

        single_shot_counter info_pending;
        single_shot_counter topology_response_pending;
        single_shot_counter higher_layer_response_pending;

        // in case Topology Notification/Response was lost
        time_point next_periodic_topology_query_deadline     = time_point::max();
        time_point next_periodic_higher_layer_query_deadline = time_point::min();
        time_point next_periodic_link_metric_query_deadline  = time_point::min();
    };

    void work() override;
    void handle_event(int event_type, void *obj) override;

    void set_ieee1905_network_enabled(bool enabled);
    bool set_network_status(const std::string &status);
    bool start_local_al_discovery();
    bool start_remote_al_discovery(const sMacAddr &al_mac);
    bool materialize_local_al();
    void complete_remote_al(const sMacAddr &al_mac);
    void handle_topology_timeout(const sMacAddr &al_mac);
    void handle_higher_layer_timeout(const sMacAddr &al_mac);
    bool ensure_al_in_dm(const sMacAddr &al_mac);
    bool update_al_in_dm(const sMacAddr &al_mac);

    /**
     * @brief Remove ALs which are no longer reachable from the local AL.
     *
     * Called after topology processing to garbage-collect disappeared devices.
     */
    void cleanup_orphan_als();

    bool handle_higher_layer_response(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);
    bool handle_link_metric_response(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);
    bool handle_topology_response(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);
    bool handle_topology_notification(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    db &database;
    ieee1905_1::CmduMessageTx &cmdu_tx;
    std::unique_ptr<IEEE1905QuerySender> query_sender;
    now_f now;

    /** updates Network.Status to Available once initial AL info is gathered */
    single_shot_counter status_pending;
    std::unordered_map<sMacAddr, AL> m_als; ///< sidecar data to \ref database.ieee1905_network.al
};

} // namespace son

#endif
