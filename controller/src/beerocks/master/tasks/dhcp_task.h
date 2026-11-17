/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DHCP_TASK_H_
#define _DHCP_TASK_H_

#include "../db/db.h"
#include "dhcp_task_state.h"
#include "task.h"
#include "task_pool.h"
#include <bcl/beerocks_timer_manager.h>
#include <bcl/network/file_descriptor.h>

namespace son {
class DhcpTask : public task {
public:
    DhcpTask(db &database_, std::shared_ptr<beerocks::TimerManager> timer_manager);
    ~DhcpTask() override;

    enum events {
        STA_CONNECTED,
    };

protected:
    void work() override{};
    void handle_event(int event_type, void *obj) override;

private:
    db &m_database; //!< Database object.
    std::shared_ptr<beerocks::TimerManager>
        m_timer_manager; //!< Timer manager to add/remove timers.

    //! File descriptor of the timer to get DHCP IPv4/IPv6 leases periodically.
    int m_periodic_timer{beerocks::net::FileDescriptor::invalid_descriptor};
    bool m_successful_init = false;
    DhcpPollSchedule m_schedule;
    DhcpLeaseTracker m_ipv4_tracker, m_ipv6_tracker;

    /**
     * @brief Start a single timer driving the shared poll schedule.
     * Fast refresh after connections and failure backoff do not create per-STA timers.
     */
    void start_periodic_lease_poll();

    /**
     * @brief Handles periodic lease poll timer timeout.
     *
     * It calls bpl::dhcp_get_leases() and related datamodel methods.
     */
    void handle_timer_timeout();
};

template <> struct easymesh_task<DhcpTask> : std::false_type {
};

} // namespace son

#endif
