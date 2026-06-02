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
#include <unordered_map>
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
   * Supported events trigger either a debounced TS policy refresh or
   * immediate exact FH/WDS updates.
   */
    void handle_event(uint8_t event_enum_value, const void *event_obj) override;

    /**
   * @brief Event IDs for TrafficSeparationTask.
   */
    enum eEvent : uint8_t {
        TS_ENABLE          = 0, /**< Refresh config and reapply TS on managed ports. */
        TS_NEW_FH_IFACE    = 1, /**< Incrementally add one pure-FH iface. */
        TS_CLEAR_FH_IFACE  = 2, /**< Incrementally clear one pure-FH iface. */
        TS_NEW_WDS_IFACE   = 3, /**< Add one WDS iface (e.g. `wlan1.2.sta1`). */
        TS_CLEAR_WDS_IFACE = 4, /**< Clear one WDS iface (e.g. `wlan1.2.sta1`). */
        TS_CLEAR           = 5  /**< Clear active TS policies and task-side deferred state. */
    };

private:
    struct sPendingWdsIfaceState {
        std::chrono::steady_clock::time_point not_before;
        std::chrono::steady_clock::time_point deadline;
    };

    /**
   * @brief Schedule the next task wakeup for the earliest requested due time.
   */
    void run_at(std::chrono::steady_clock::time_point due);

    /**
   * @brief Request a debounced full TS reapply.
   */
    void request_full_apply();

    /**
   * @brief Register one WDS iface for deferred retry processing.
   *
   * Stores the per-iface retry window and schedules the task wakeup for the
   * earliest requested retry time.
   */
    void request_wds_retry(const std::string &iface_name,
                           std::chrono::steady_clock::time_point not_before,
                           std::chrono::steady_clock::time_point deadline);

    /**
   * @brief Reset pending debounced apply state.
   */
    void clear_pending_apply();

    /**
   * @brief Reset any scheduled task work.
   */
    void clear_scheduled_work();

    /**
   * @brief Check whether the debounce timeout expired and apply can run now.
   */
    bool should_run_now() const;

    /**
   * @brief Refresh config and reapply TS on currently managed ports.
   *
   * Keeps exact FH/WDS iface ownership event-driven while refreshing the
   * config and persistent DB-owned trunk ports.
   */
    bool reset();

    /**
   * @brief Re-add exact FH/WDS ports from the current DB snapshot.
   *
   * Exact FH/WDS ports are primarily managed incrementally by task events.
   * This rebuild path repopulates the manager after task recreation or
   * crash/restart when those exact events may not be replayed.
   */
    bool restore_exact_ports_from_db();

    /**
   * @brief Resolve one exact WDS trunk from current DB state.
   *
   * Uses associated-client and matching backhaul-BSS state to derive trunk
   * mode for an already known exact WDS netdev.
   */
    bool fill_wds_trunk_from_db(const std::string &iface_name, net::sTrunkPort &trunk) const;

    /**
   * @brief Incrementally add a fronthaul access port carried by a task event.
   */
    bool handle_new_fh_iface(const std::string &iface_name);

    /**
   * @brief Incrementally clear a fronthaul access port carried by a task event.
   */
    bool handle_clear_fh_iface(const std::string &iface_name);

    /**
   * @brief Add one WDS iface (e.g. `wlan1.2.sta1`).
   */
    bool handle_new_wds_iface(const std::string &iface_name);

    /**
   * @brief Clear one WDS iface (e.g. `wlan1.2.sta1`).
   */
    bool handle_clear_wds_iface(const std::string &iface_name);

    /**
   * @brief Retry pending WDS iface additions after transient readiness
   * failures.
   */
    bool retry_pending_wds_ifaces();

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
   * @brief Collect persistent trunk ports from current DB state.
   */
    bool get_ports_from_db(std::vector<net::sTrunkPort> &trunks) const;

    /**
     * @brief Add currently selected backhaul connection interface as a trunk candidate.
     */
    bool add_backhaul_connection_trunk(std::vector<net::sTrunkPort> &trunks) const;

    slave_thread &m_btl_ctx;

    std::unique_ptr<net::TrafficSeparationManager> m_mgr;

    bool m_pending       = false;
    bool m_apply_pending = false;
    std::chrono::steady_clock::time_point m_next_run{std::chrono::steady_clock::time_point::min()};
    std::unordered_map<std::string, sPendingWdsIfaceState> m_pending_wds_ifaces;

    uint16_t m_last_primary_vid = 0;

private:
    // Debounce TS apply events to coalesce short bursts into one operation
    // and avoid repetitive bridge/VLAN reconfiguration churn.
    static constexpr int DEBOUNCE_MS = 200;
    // Newly learned WDS netdevs may appear and become runnable shortly after
    // the property notification is emitted, so keep the first add attempt
    // out of the synchronous event path.
    static constexpr int WDS_SETTLE_MS = 500;
    // Keep retrying for a bounded window so the same WDS iface is not lost if
    // it becomes ready shortly after the settle delay.
    static constexpr int WDS_RETRY_TIMEOUT_MS = 2000;
};

} // namespace beerocks

#endif // _TRAFFIC_SEPARATION_TASK_H_
