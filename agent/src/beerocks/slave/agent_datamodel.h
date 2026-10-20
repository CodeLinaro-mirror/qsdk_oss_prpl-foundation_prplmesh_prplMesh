/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _AGENT_DATAMODEL_H_
#define _AGENT_DATAMODEL_H_

#include "ambiorix.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace beerocks {

/**
 * Publishes owned Agent data independently of AgentDB's application lock.
 *
 * Void updates made inside a SafeDB scope are published synchronously when its
 * outermost lock is released. The FIFO preserves their order, including nested
 * task/FSM calls. An update cannot run until its originating DB scope releases
 * the lock; its strings and datamodel reference remain owned until publication.
 * Operations returning a result must be called outside every SafeDB scope.
 */
class AgentDataModel {
public:
    static AgentDataModel &get();

    bool init_data_model(std::shared_ptr<nbapi::Ambiorix> dm);
    bool dm_set_agent_mac(const std::string &mac);
    std::string dm_create_fronthaul_object(const std::string &iface);

    void dm_set_fronthaul_interfaces(const std::string &interfaces);
    void dm_set_management_mode(const std::string &mode);
    void dm_set_agent_state(const std::string &cur, const std::string &max);
    void dm_set_controller_connected(bool connected);
    void dm_set_fronthaul_state(const std::string &path, const std::string &cur,
                                const std::string &max);
    void dm_fronthaul_disconnected(const std::string &path);

    AgentDataModel(const AgentDataModel &) = delete;
    AgentDataModel &operator=(const AgentDataModel &) = delete;

private:
    friend class AgentDB;
    AgentDataModel() = default;

    using Update = std::function<void(const std::shared_ptr<nbapi::Ambiorix> &)>;
    struct PendingUpdate {
        std::shared_ptr<std::atomic<bool>> ready;
        std::shared_ptr<nbapi::Ambiorix> datamodel;
        Update apply;
    };

    void enqueue(Update update);
    void drain();
    std::shared_ptr<nbapi::Ambiorix> datamodel();

    // Never acquire AMX while holding this mutex, or run a queued update under it.
    std::mutex m_queue_mutex;
    std::shared_ptr<nbapi::Ambiorix> m_datamodel;
    std::deque<PendingUpdate> m_updates;

    // Protected by AmxGuard, including callback re-entry on the same thread.
    bool m_draining = false;
};

} // namespace beerocks

#endif // _AGENT_DATAMODEL_H_
