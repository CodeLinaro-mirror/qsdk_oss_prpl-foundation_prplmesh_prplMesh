/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef FRONTHAUL_BSS_TEARDOWN_H
#define FRONTHAUL_BSS_TEARDOWN_H

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

namespace beerocks {

// Tracks the AP managers that must finish teardown before the Agent can stop them.
// Time is supplied by the caller so expiration does not require a blocking wait.
class FronthaulBssTeardown {
public:
    using Clock = std::chrono::steady_clock;

    void start(Clock::time_point deadline)
    {
        if (m_active) {
            return;
        }
        if (++m_request_id == 0) {
            ++m_request_id;
        }
        m_pending.clear();
        m_deadline = deadline;
        m_active   = true;
    }

    void add(int fd, const std::string &iface) { m_pending.emplace(fd, iface); }

    bool complete(int fd, uint16_t request_id)
    {
        return m_active && request_id == m_request_id && m_pending.erase(fd) != 0;
    }

    bool disconnected(int fd) { return m_active && m_pending.erase(fd) != 0; }

    bool ready(Clock::time_point now) const
    {
        return m_active && (m_pending.empty() || now >= m_deadline);
    }

    void reset()
    {
        m_active = false;
        m_pending.clear();
    }

    bool active() const { return m_active; }
    uint16_t request_id() const { return m_request_id; }
    const std::map<int, std::string> &pending() const { return m_pending; }

private:
    bool m_active         = false;
    uint16_t m_request_id = 0;
    Clock::time_point m_deadline{};
    std::map<int, std::string> m_pending;
};

} // namespace beerocks

#endif
