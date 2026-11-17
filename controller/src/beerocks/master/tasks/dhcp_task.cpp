/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dhcp_task.h"

#include "bpl/bpl_dhcp.h"
#include <easylogging++.h>

using namespace beerocks;
using namespace son;

DhcpTask::DhcpTask(db &database_, std::shared_ptr<beerocks::TimerManager> timer_manager)
    : task("dhcp task"), m_database(database_), m_timer_manager(timer_manager),
      m_schedule(database_.config.dhcp_monitor_interval_seconds, DhcpPollSchedule::Clock::now())
{
    LOG_IF(!m_timer_manager, FATAL) << "Timer manager is a null pointer!";

    m_database.assign_dhcp_task_id(id);

    m_successful_init = bpl::dhcp_manual_procedure_init();
    start_periodic_lease_poll();
}

DhcpTask::~DhcpTask()
{
    if (m_periodic_timer != beerocks::net::FileDescriptor::invalid_descriptor) {
        m_timer_manager->remove_timer(m_periodic_timer);
    }

    bpl::dhcp_manual_procedure_destroy();
}

void DhcpTask::handle_timer_timeout()
{
    if (!m_schedule.ready(DhcpPollSchedule::Clock::now())) {
        return;
    }
    if (!m_successful_init) {
        m_successful_init = bpl::dhcp_manual_procedure_init();
        if (!m_successful_init) {
            LOG(WARNING) << "DHCP procedure init failed; will retry";
            m_schedule.completed(DhcpPollSchedule::Clock::now(), false);
            return;
        }
    }
    bpl::leases_pair_t leases;
    bpl::sLeaseReadStatus status;
    if (!bpl::dhcp_get_leases(leases, &status)) {
        LOG(ERROR) << "DHCP get leases failed.";
        m_schedule.completed(DhcpPollSchedule::Clock::now(), false);
        return;
    }

    DhcpLeaseTracker::MacSet ipv4_present, ipv6_present;
    for (const auto &ipv6 : leases.second) {
        ipv6_present.insert(ipv6.second.mac);
        if (!m_database.has_station(ipv6.second.mac)) {
            continue;
        }
        const auto station = m_database.get_station(ipv6.second.mac);
        if (!station) {
            continue;
        }
        const auto &name = ipv6.second.host_name.empty() ? station->name : ipv6.second.host_name;
        m_database.set_sta_dhcp_v6_lease(ipv6.second.mac, name, ipv6.second.ip_address);
        m_ipv6_tracker.remember(ipv6.second.mac, ipv6.second.ip_address);
    }

    // To give precedence ipv4 hostname in database in case of host name conflict,
    // ipv4 leases are proccessed later.
    for (const auto &ipv4 : leases.first) {
        ipv4_present.insert(ipv4.first);
        // The platform lease table also includes wired clients and other non-STA hosts.
        if (!m_database.has_station(ipv4.first)) {
            continue;
        }
        const auto station = m_database.get_station(ipv4.first);
        if (!station) {
            continue;
        }
        const auto &name = ipv4.second.host_name.empty() ? station->name : ipv4.second.host_name;
        m_database.set_sta_dhcp_v4_lease(ipv4.first, name, ipv4.second.ip_address);
        m_ipv4_tracker.remember(ipv4.first, ipv4.second.ip_address);
    }

    const auto current_v4 = [this](const sMacAddr &mac) {
        const auto sta = m_database.m_stations.get(mac);
        return sta ? sta->ipv4 : std::string{};
    };
    const auto current_v6 = [this](const sMacAddr &mac) {
        const auto sta = m_database.m_stations.get(mac);
        return sta ? sta->ipv6 : std::string{};
    };
    for (const auto &mac : m_ipv4_tracker.removed(ipv4_present, status.ipv4_complete, current_v4)) {
        const auto sta = m_database.m_stations.get(mac);
        m_ipv4_tracker.cleared(mac, !sta || m_database.set_sta_dhcp_v4_lease(mac, sta->name, "0"));
    }
    for (const auto &mac : m_ipv6_tracker.removed(ipv6_present, status.ipv6_complete, current_v6)) {
        const auto sta = m_database.m_stations.get(mac);
        m_ipv6_tracker.cleared(mac, !sta || m_database.set_sta_dhcp_v6_lease(mac, sta->name, "0"));
    }
    m_schedule.completed(DhcpPollSchedule::Clock::now(), !status.retry);
}

void DhcpTask::start_periodic_lease_poll()
{

    // Remove old periodic timer.
    if (m_periodic_timer != beerocks::net::FileDescriptor::invalid_descriptor) {
        m_timer_manager->remove_timer(m_periodic_timer);
    }

    m_periodic_timer = m_timer_manager->add_timer("DHCP Poll Schedule", std::chrono::seconds(1),
                                                  std::chrono::seconds(1),
                                                  [this](int fd, beerocks::EventLoop &loop) {
                                                      this->handle_timer_timeout();
                                                      return true;
                                                  });
}

void DhcpTask::handle_event(int event_type, void *obj)
{
    if (event_type == STA_CONNECTED) {
        m_schedule.station_connected(DhcpPollSchedule::Clock::now());
    }
}
