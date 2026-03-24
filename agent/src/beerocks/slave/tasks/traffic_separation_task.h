/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _TRAFFIC_SEPARATION_TASK_H_
#define _TRAFFIC_SEPARATION_TASK_H_

#include "task.h"
#include "traffic_separation/traffic_separation_manager.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace beerocks {

class slave_thread;

/**
 * @brief Task responsible for applying EasyMesh Traffic Separation rules on the agent platform.
 *
 */
class TrafficSeparationTask final : public Task {
public:
    /**
     * @brief Construct the TrafficSeparationTask.
     *
     * Reads the "traffic separation enabled" flag from configuration and creates the TS manager.
     */
    explicit TrafficSeparationTask(slave_thread &btl_ctx);

    TrafficSeparationTask(const TrafficSeparationTask &) = delete;
    TrafficSeparationTask &operator=(const TrafficSeparationTask &) = delete;

    /**
     * @brief Periodic task hook (executed by the task scheduler).
     *
     * Runs pending recomputation actions after a short debounce period.
     */
    void work() override;

    /**
     * @brief Handle task events (like other prplMesh tasks).
     *
     * Supported events trigger either a debounced TS policy refresh or an immediate exact WDS
     * update.
     */
    void handle_event(uint8_t event_enum_value, const void *event_obj) override;

    /**
     * @brief Event IDs for TrafficSeparationTask.
     */
    enum eEvent : uint8_t {
        TS_ENABLE        = 0, /**< Refresh config and reapply TS on managed ports. */
        TS_NEW_WDS_IFACE = 1, /**< Incrementally add one exact WDS iface (e.g. `wlan1.2.sta1`). */
        TS_CLEAR_WDS_IFACE =
            2,       /**< Incrementally clear one exact WDS iface. (e.g. `wlan2.1.sta2`)*/
        TS_CLEAR = 3 /**< Clear config + reset transport primary VLAN. */
    };

private:
    /**
     * @brief Queue a debounced TS apply action.
     */
    void schedule_apply();

    /**
     * @brief Reset pending debounced apply state.
     */
    void clear_pending_apply();

    /**
     * @brief Check whether the debounce timeout expired and apply can run now.
     */
    bool should_run_now() const;

    /**
     * @brief Refresh TS configuration and reset policies on managed ports.
     *
     * Keeps event-managed exact WDS ifaces intact and only re-applies policies.
     */
    bool reset();

    /**
     * @brief Incremental TS update for a newly created exact WDS interface.
     */
    bool handle_new_wds_iface(const std::string &iface_name);

    /**
     * @brief Incremental TS update for a removed exact WDS interface.
     */
    bool handle_clear_wds_iface(const std::string &iface_name);

    /**
     * @brief Clear TS state and restore transport defaults used outside TS mode.
     */
    bool cleanup_ts_runtime_state();

    /**
     * @brief Configure primary VLAN in transport/broker when it changed.
     */
    bool ensure_transport_primary_vlan(uint16_t primary_vid);

    /**
     * @brief Reset broker interface monitoring to bridge defaults after TS cleanup.
     */
    bool reset_transport_monitoring_on_bridge();

    /**
     * @brief Build manager configuration (bridges + private/guest VLAN IDs) from DB/BPL.
     */
    bool build_ts_config(net::sTrafficSeparationConfig &cfg) const;

    /**
     * @brief Collect persistent trunk and access ports from current DB state.
     */
    bool get_ports_from_db(std::vector<net::sTrunkPort> &trunks,
                           std::vector<net::sAccessPort> &access_ports) const;

    /**
     * @brief Add currently selected backhaul connection interface as a trunk candidate.
     */
    bool add_backhaul_connection_trunk(std::vector<net::sTrunkPort> &trunks) const;

    slave_thread &m_btl_ctx;

    std::unique_ptr<net::TrafficSeparationManager> m_mgr;

    bool m_pending = false;
    std::chrono::steady_clock::time_point m_next_run{std::chrono::steady_clock::time_point::min()};

    uint16_t m_last_primary_vid = 0;

private:
    // Debounce TS apply events to coalesce short bursts into one operation
    // and avoid repetitive bridge/VLAN reconfiguration churn.
    static constexpr int DEBOUNCE_MS = 200;
};

} // namespace beerocks

#endif // _TRAFFIC_SEPARATION_TASK_H_
