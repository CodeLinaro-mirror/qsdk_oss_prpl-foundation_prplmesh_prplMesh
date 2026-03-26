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

/**
 * @brief Abstract interface for sending IEEE1905 query messages to remote AL entities.
 *
 * Provides a seam for dependency injection, allowing the ieee1905_task to be tested
 * without a live CMDU transport layer.
 */
class IEEE1905QuerySender {
public:
    virtual ~IEEE1905QuerySender() = default;

    /**
     * @brief Send an IEEE1905 Topology Query to the given destination.
     *
     * @param dest_mac destination AL MAC address
     * @param cmdu_tx  transmit buffer used to build and send the message
     *
     * @return true on success, false otherwise
     */
    virtual bool send_topology_query(const sMacAddr &dest_mac,
                                     ieee1905_1::CmduMessageTx &cmdu_tx) = 0;

    /**
     * @brief Send an IEEE1905 Higher Layer Query to the given destination.
     *
     * @param dest_mac destination AL MAC address
     * @param cmdu_tx  transmit buffer used to build and send the message
     *
     * @return true on success, false otherwise
     */
    virtual bool send_higher_layer_query(const sMacAddr &dest_mac,
                                         ieee1905_1::CmduMessageTx &cmdu_tx) = 0;

    /**
     * @brief Send an IEEE1905 Link Metric Query to the given destination.
     *
     * @param dest_mac destination AL MAC address
     * @param cmdu_tx  transmit buffer used to build and send the message
     *
     * @return true on success, false otherwise
     */
    virtual bool send_link_metric_query(const sMacAddr &dest_mac,
                                        ieee1905_1::CmduMessageTx &cmdu_tx) = 0;
};

/**
 * @brief Task that discovers and maintains the IEEE1905 network topology in the data model.
 *
 * On startup it discovers the local AL and all reachable remote AL entities by sending
 * Topology, Higher Layer, and Link Metric queries. It also processes unsolicited
 * Topology Notifications and periodically re-queries each AL to keep the data model
 * up to date.
 *
 * The task owns \ref m_als, a sidecar map that tracks per-AL lifetime state
 * (pending counters, deadlines) separately from the canonical \ref db::ieee1905_network_db.
 */
class ieee1905_task : public task {
public:
    /** @brief Alias for the steady clock used throughout the task. */
    using steady_clock = std::chrono::steady_clock;
    /** @brief Alias for a steady-clock time point. */
    using time_point = steady_clock::time_point;
    /** @brief Callable that returns the current time; injectable for testing. */
    using now_f = std::function<time_point()>;

    /** @brief Event types dispatched to this task via task::handle_event(). */
    enum eEventType {
        /** Sent when IEEE1905.Network.Enable changes; payload is a bool*. */
        IEEE1905_NETWORK_ENABLE_CHANGED = 1,
    };

    /** @brief Maximum time to wait for the first Topology Response after sending a query. */
    static constexpr std::chrono::seconds topology_response_timeout{2};
    /** @brief Maximum time to wait for the first Higher Layer Response after sending a query. */
    static constexpr std::chrono::seconds higher_layer_response_timeout{1};
    /** @brief How often to re-send periodic Topology Queries to each known AL. */
    static constexpr std::chrono::seconds periodic_topology_requery_interval{30};
    /** @brief to not to interfere with link metric task: */
    static constexpr std::chrono::seconds link_metric_response_requery_delay_guard{1};
    /**
     * @brief Construct and initialize the ieee1905_task.
     *
     * Reads IEEE1905.Network.Enable from the data model, registers the task ID in the
     * database, and starts local AL discovery if the network is enabled.
     *
     * @param database     controller database
     * @param cmdu_tx      shared CMDU transmit buffer
     * @param query_sender abstraction used to send IEEE1905 query messages
     * @param now          clock source; defaults to std::chrono::steady_clock::now
     */
    ieee1905_task(db &database, ieee1905_1::CmduMessageTx &cmdu_tx,
                  std::unique_ptr<IEEE1905QuerySender> query_sender, now_f now = steady_clock::now);

    /**
     * @brief Dispatches an incoming IEEE1905.1 message to the appropriate handler.
     *
     * @param src_mac  source MAC address
     * @param cmdu_rx  received CMDU
     *
     * @return true if the message was handled, false if it should be forwarded
     */
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
        /** Deadline by which the first Topology Response must arrive. */
        time_point first_topology_query_deadline = time_point::max();
        /** Deadline by which the first Higher Layer Response must arrive. */
        time_point first_higher_layer_query_deadline = time_point::max();

        single_shot_counter info_pending; ///< To track initial AL information collection.
        single_shot_counter topology_response_pending; ///< To track the first Topology Response.
        /** To track the first Higher Layer Response (or timeout). */
        single_shot_counter higher_layer_response_pending;

        /** Next time a periodic Topology Query should be sent. */
        time_point next_periodic_topology_query_deadline = time_point::max();
        /** Next time a periodic Higher Layer Query should be sent. */
        time_point next_periodic_higher_layer_query_deadline = time_point::min();
        /** Next time a periodic Link Metric Query should be sent. */
        time_point next_periodic_link_metric_query_deadline = time_point::min();
    };

    /** @brief Task main loop: handles timeouts and sends periodic queries. */
    void work() override;

    /**
     * @brief Handles a task event dispatched via the task scheduler.
     *
     * @param event_type one of \ref eEventType values
     * @param obj        event-specific payload pointer (type depends on event_type)
     */
    void handle_event(int event_type, void *obj) override;

    /**
     * @brief Enable or disable the IEEE1905 network tracking.
     *
     * When disabled, clears the network DB and all AL sidecar state.
     * When enabled (and not already enabled), initialises the network DB
     * and starts local AL discovery.
     *
     * @param enabled new value of IEEE1905.Network.Enable
     */
    void set_ieee1905_network_enabled(bool enabled);

    /**
     * @brief Write a new value to IEEE1905.Network.Status in the data model.
     *
     * @param status new status string (e.g. "Incomplete", "Available")
     *
     * @return true on success, false if Ambiorix is unavailable or the set fails
     */
    bool set_network_status(const std::string &status);

    /**
     * @brief Begin discovery of the local AL (controller itself).
     *
     * Registers the local AL in \ref m_als with pending counters and sends the
     * initial Topology and Higher Layer queries.
     *
     * @return true on success, false otherwise
     */
    bool start_local_al_discovery();

    /**
     * @brief Begin discovery of a newly seen remote AL.
     *
     * Inserts the AL into \ref m_als and sends Topology and Higher Layer queries.
     *
     * @param al_mac target AL MAC address
     *
     * @return true on success, false otherwise
     */
    bool start_remote_al_discovery(const sMacAddr &al_mac);

    /**
     * @brief Populate the data model entry for the local AL from system information.
     *
     * @return true on success, false otherwise
     */
    bool materialize_local_al();

    /**
     * @brief Finalise discovery of a remote AL after all queries have been answered.
     *
     * Decrements \ref status_pending so that Network.Status can transition to Available
     * once every AL has been discovered.
     *
     * @param al_mac target AL MAC address
     */
    void complete_remote_al(const sMacAddr &al_mac);

    /**
     * @brief Handle expiry of the first-Topology-Response deadline for an AL.
     *
     * Removes the AL from \ref m_als if no response was received.
     *
     * @param al_mac timed-out device AL MAC address
     */
    void handle_topology_timeout(const sMacAddr &al_mac);

    /**
     * @brief Handle expiry of the first-Higher-Layer-Response deadline for an AL.
     *
     * Completes remote AL discovery even without higher layer data.
     *
     * @param al_mac timed-out device AL MAC address
     */
    void handle_higher_layer_timeout(const sMacAddr &al_mac);

    /**
     * @brief Ensure an AL data model entry exists for the given AL MAC, creating it if absent.
     *
     * @param al_mac target AL MAC address
     *
     * @return true if the entry exists (or was created), false on error
     */
    bool ensure_al_in_dm(const sMacAddr &al_mac);

    /**
     * @brief Refresh all fields of an existing AL data model entry.
     *
     * @param al_mac target AL MAC address
     *
     * @return true on success, false otherwise
     */
    bool update_al_in_dm(const sMacAddr &al_mac);

    /**
     * @brief Remove ALs which are no longer reachable from the local AL.
     *
     * Called after topology processing to garbage-collect disappeared devices.
     */
    void cleanup_orphan_als();

    /**
     * @brief Process an IEEE1905 Higher Layer Response message.
     *
     * @param src_mac  source MAC address
     * @param cmdu_rx  received CMDU
     *
     * @return true if handled successfully, false otherwise
     */
    bool handle_higher_layer_response(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Process an IEEE1905 Link Metric Response message.
     *
     * Updates per-link TX/RX metrics in the data model.
     *
     * @param src_mac  source MAC address
     * @param cmdu_rx  received CMDU
     *
     * @return true if handled successfully, false otherwise
     */
    bool handle_link_metric_response(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Process an IEEE1905 Topology Response message.
     *
     * Updates interfaces, neighbors, bridging tuples, and IP addresses for the
     * responding AL, and starts discovery for any newly discovered neighbours.
     *
     * @param src_mac  source MAC address
     * @param cmdu_rx  received CMDU
     *
     * @return true if handled successfully, false otherwise
     */
    bool handle_topology_response(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Process an IEEE1905 Topology Notification message.
     *
     * Triggers an immediate Topology Query to the notifying AL.
     *
     * @param src_mac  source MAC address
     * @param cmdu_rx  received CMDU
     *
     * @return true if handled successfully, false otherwise
     */
    bool handle_topology_notification(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    db &database;                       ///< Controller database reference.
    ieee1905_1::CmduMessageTx &cmdu_tx; ///< Shared CMDU transmit buffer.

    std::unique_ptr<IEEE1905QuerySender> query_sender; ///< IEEE1905 query sender (injected).
    now_f now;                                         ///< Steady-clock time source (injected).

    /** updates Network.Status to Available once initial AL info is gathered */
    single_shot_counter status_pending;
    std::unordered_map<sMacAddr, AL> m_als; ///< sidecar data to \ref database.ieee1905_network.al
};

template <> struct easymesh_task<ieee1905_task> : std::false_type {
};

} // namespace son

#endif
