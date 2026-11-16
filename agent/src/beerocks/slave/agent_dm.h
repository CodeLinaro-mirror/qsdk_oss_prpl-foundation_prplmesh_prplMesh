/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _AGENT_DM_H_
#define _AGENT_DM_H_

#include "ambiorix.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace beerocks {

/**
 * @brief AgentDM owns the Agent's local Ambiorix datamodel (AGENT_ROOT_DM).
 *
 * The class is a singleton, used the same way as AgentDB, but it is protected by its own
 * recursive mutex. A SafeDM scope first acquires the process-wide AMX mutex and then the AgentDM
 * mutex, so every operation in the scope runs as one AMX operation.
 *
 * How to use:
 * @code
 * AgentDM::get()->dm_set_management_mode(mode);    // Single datamodel update.
 *
 * {
 *     auto dm = AgentDM::get();                    // Several updates as one AMX operation.
 *     dm->dm_set_agent_state(cur, max);
 *     dm->dm_set_controller_connected(connected);
 * }
 * @endcode
 *
 * Lock order: AMX -> AgentDM. Never acquire AgentDM while holding AgentDB or any other
 * application lock. Copy the required AgentDB values in a separate scope, release it, and then
 * publish them:
 * @code
 * bool connected;
 * {
 *     auto db   = AgentDB::get();
 *     connected = db->statuses.controller_connected;
 * }
 * AgentDM::get()->dm_set_controller_connected(connected);
 * @endcode
 */
class AgentDM {
public:
    class SafeDM {
    public:
        explicit SafeDM(AgentDM &dm) : m_dm(&dm) { m_dm->dm_lock(); }
        SafeDM(const SafeDM &) = delete;
        SafeDM &operator=(const SafeDM &) = delete;
        SafeDM(SafeDM &&other) : m_dm(std::exchange(other.m_dm, nullptr)) {}
        SafeDM &operator=(SafeDM &&) = delete;
        ~SafeDM()
        {
            if (m_dm) {
                m_dm->dm_unlock();
            }
        }
        AgentDM *operator->() { return m_dm; }

    private:
        AgentDM *m_dm;
    };
    static SafeDM get();
    AgentDM(const AgentDM &) = delete;
    void operator=(const AgentDM &) = delete;

    /**
     * @brief Set the datamodel used by all other methods.
     *
     * Must be called once, during initialization, before any other method.
     *
     * @param dm Ambiorix datamodel which owns AGENT_ROOT_DM.
     * @return True on success, false otherwise.
     */
    bool init_data_model(std::shared_ptr<nbapi::Ambiorix> dm);

    /**
     * @brief Set AGENT_ROOT_DM.Info.MACAddress.
     *
     * @param mac MAC address of the bridge interface.
     * @return True on success, false otherwise.
     */
    bool dm_set_agent_mac(const std::string &mac);

    bool dm_set_fronthaul_interfaces(const std::string &interfaces);
    bool dm_set_management_mode(const std::string &mode);
    bool dm_set_agent_state(const std::string &cur, const std::string &max);
    bool dm_set_controller_connected(bool connected);

    /**
     * @brief Create a fully initialized AGENT_ROOT_DM.Info.Fronthaul instance.
     *
     * An existing instance with the same Iface is replaced. If initialization fails, the new
     * instance is removed.
     *
     * @param iface Fronthaul interface name.
     * @return Path of the new instance, or an empty string on failure.
     */
    std::string dm_create_fronthaul_object(const std::string &iface);

    bool dm_set_fronthaul_state(const std::string &path, const std::string &cur,
                                const std::string &max);
    bool dm_fronthaul_disconnected(const std::string &path);

private:
    // Private constructor so that no objects can be created.
    AgentDM() = default;
    std::recursive_mutex m_dm_mutex;
    void dm_lock();
    void dm_unlock();

    std::shared_ptr<nbapi::Ambiorix> m_ambiorix_datamodel;
};

} // namespace beerocks

#endif // _AGENT_DM_H_
