/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef DHCP_TASK_STATE_H_
#define DHCP_TASK_STATE_H_

#include <bpl/bpl_dhcp.h>

#include <algorithm>
#include <functional>
#include <unordered_set>
#include <vector>

namespace son {

// A single timer drives all stations. Connection bursts advance a poll, never postpone it.
class DhcpPollSchedule {
public:
    using Clock    = std::chrono::steady_clock;
    using Time     = Clock::time_point;
    using Duration = std::chrono::milliseconds;

    DhcpPollSchedule(Duration interval, Time now)
        : m_interval(std::max(interval, Duration(1000))),
          m_next(now + std::min(m_interval, Duration(5000))), m_fast_until(now + Duration(60000))
    {
    }

    bool ready(Time now) const { return now >= m_next; }

    void station_connected(Time now)
    {
        m_next       = std::min(m_next, now + Duration(5000));
        m_fast_until = now + Duration(60000);
    }

    void completed(Time now, bool healthy)
    {
        if (!healthy) {
            m_next  = now + std::min(m_interval, m_retry);
            m_retry = std::min(m_retry * 2, Duration(60000));
            return;
        }
        m_retry = Duration(5000);
        m_next  = now + (now < m_fast_until ? std::min(m_interval, Duration(5000)) : m_interval);
    }

private:
    Duration m_interval;
    Time m_next, m_fast_until;
    Duration m_retry{5000};
};

// Track only addresses written by this task, so removing a lease cannot erase an
// address subsequently supplied by another controller discovery path.
class DhcpLeaseTracker {
public:
    using MacSet = std::unordered_set<sMacAddr>;
    void remember(const sMacAddr &mac, const std::string &address) { m_addresses[mac] = address; }

    std::vector<sMacAddr> removed(const MacSet &present, bool complete,
                                  const std::function<std::string(const sMacAddr &)> &current)
    {
        std::vector<sMacAddr> result;
        for (auto it = m_addresses.begin(); it != m_addresses.end();) {
            if (current(it->first) != it->second) {
                // Station removed, or its address replaced by another producer.
                it = m_addresses.erase(it);
                continue;
            }
            if (complete && !present.count(it->first)) {
                result.push_back(it->first);
            }
            ++it;
        }
        return result;
    }

    void cleared(const sMacAddr &mac, bool success)
    {
        if (success) {
            m_addresses.erase(mac);
        } else {
            // DB setters update cached state before writing the DM. Retry failed clears
            // even though the cached address has already become the unknown sentinel.
            m_addresses[mac] = "0";
        }
    }

private:
    std::unordered_map<sMacAddr, std::string> m_addresses;
};

} // namespace son
#endif // DHCP_TASK_STATE_H_
