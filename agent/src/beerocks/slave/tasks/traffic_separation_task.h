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

#include <tlvf/CmduMessageTx.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    TrafficSeparationTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);

    /**
     * @brief Default destructor.
     */
    ~TrafficSeparationTask() override { handle_event(eEvent::TS_CLEAR, nullptr); };

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
     * Supported events trigger either a full rebuild or an incremental STA scan.
     */
    void handle_event(uint8_t event_enum_value, const void *event_obj) override;

    /**
     * @brief Clear all Traffic Separation rules and reset internal state.
     *
     * Intended for onboarding restart or when TS must be removed explicitly.
     */
    bool clear_configuration();

    /**
     * @brief Event IDs for TrafficSeparationTask.
     */
    enum eEvent : uint8_t {
        TS_ENABLE           = 0, /**< Rebuild config and apply TS on all ports. */
        TS_NEW_BH_STA_IFACE = 1, /**< Scan and add new wlanX.Y.staN trunk ports. */
        TS_CLEAR            = 2  /**< Clear config + reset transport primary VLAN. */
    };

private:
    enum class eApplyMode : uint8_t { None = 0, AddNewSta = 1, Full = 2 };

private:
    static eApplyMode stronger_mode(eApplyMode a, eApplyMode b);
    void schedule_apply(eApplyMode mode);
    bool should_run_now() const;

    bool reset();
    bool handle_new_sta_iface();

    bool ensure_transport_primary_vlan(uint16_t primary_vid);
    bool reset_transport_monitoring_on_bridge();

    bool build_ts_config(net::sTrafficSeparationConfig &cfg) const;

    bool collect_ports_from_db(std::vector<net::sTrunkPort> &trunks,
                               std::vector<net::sAccessPort> &access_ports) const;
    bool add_backhaul_connection_trunk(std::vector<net::sTrunkPort> &trunks) const;

    static std::vector<std::string> get_all_sta_ifaces_for_bss(const std::string &bss_iface);
    static int sta_index_for_prefix(const std::string &ifname, const std::string &prefix);

    bool
    collect_backhaul_bss_ifaces(std::vector<std::string> &out_bss_ifaces,
                                std::unordered_map<std::string, bool> &out_untagged_mode) const;

private:
    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;

    std::unique_ptr<net::TrafficSeparationManager> m_mgr;

    bool m_pending = false;
    std::chrono::steady_clock::time_point m_next_run{};
    eApplyMode m_mode = eApplyMode::None;

    uint16_t m_last_primary_vid = 0;

private:
    static constexpr int DEBOUNCE_MS           = 200;
    static constexpr int DEFAULT_GUEST_VLAN_ID = 20;
};

} // namespace beerocks

#endif // _TRAFFIC_SEPARATION_TASK_H_
