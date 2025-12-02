/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef TRAFFIC_SEPARATION_MANAGER
#define TRAFFIC_SEPARATION_MANAGER

#include "port_plumbing.h"
#include "traffic_separation_utils.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace beerocks::net {

/**
 * @brief TrafficSeparationManager for per-port traffic separation plumbing.
 *
 * Owns sets of PortPlumbing instances for trunk ports and access ports and
 * coordinates applying and clearing the traffic separation policies.
 */
class TrafficSeparationManager {
public:
    TrafficSeparationManager() = default;
    virtual ~TrafficSeparationManager() { clear_policies(); }
    TrafficSeparationManager(const TrafficSeparationManager &) = delete;
    TrafficSeparationManager &operator=(const TrafficSeparationManager &) = delete;

    /**
     * @brief Configure the TrafficSeparationManager with a new TS configuration
     *
     * @param cfg Traffic separation configuration to use.
     * @return true on successful configuration, false on error
     */
    bool configure(const sTrafficSeparationConfig &cfg);

    /**
     * @brief Add a new trunk port to be managed by Traffic Separation manager.
     *
     * @param trunk_port Description of the trunk port interface to manage.
     *
     * @return true on success, false if applying TS on the new trunk port failed.
     */
    bool add_trunk_port(const sTrunkPort &trunk_port);

    /**
     * @brief Remove an existing trunk port from management.
     *
     * @param iface_name Interface name of the trunk port to remove.
     *
     * @return true on success, false if clear() fails for this trunk port.
     */
    bool remove_trunk_port(const std::string &iface_name);

    /**
     * @brief Add an access port (fronthaul BSS) to be managed.
     *
     * @param access_port Access port descriptor to manage.
     *
     * @return true on success, false if applying the rule on the new access
     *         port failed when policies are currently applied.
     */
    bool add_access_port(const sAccessPort &access_port);

    /**
     * @brief Remove an existing access port from management.
     *
     * @param iface_name Interface name of the access port to remove.
     *
     * @return true on success, false if clearing policy for this access port failed.
     */
    bool remove_access_port(const std::string &iface_name);

    /**
     * @brief Apply the current configuration to all configured trunk and access ports.
     *
     * @return true if apply() succeeded for all ports and a configuration
     *         was present, false otherwise.
     */
    bool apply_policies();

    /**
     * @brief Rollback of the current configuration on all trunk and access ports.
     *
     * @return true if clear() succeeded (or there was nothing to clear) for
     *         all ports, false otherwise.
     */
    bool clear_policies();

    /**
     * @brief Reset the manager to its initial, unconfigured state.
     *
     * @return true if all operations succeeded, false if any of them failed.
     */
    bool reset();

private:
    bool has_config() const { return m_state != eTsManagerState::NO_CONFIG; }
    bool is_applied() const { return m_state == eTsManagerState::APPLIED; }

    bool has_any_ports() const
    {
        return !m_trunk_port_plumbing_map.empty() || !m_access_port_plumbing_map.empty();
    }

    /**
     * @brief Remove all existing trunk ports from management.
     *
     * @return true on success, false if clear() fails for any trunk port.
     */
    bool remove_all_trunk_ports();

    /**
     * @brief Remove all existing access ports from management.
     *
     * @return true on success, false if clearing policy fails for any access port.
     */
    bool remove_all_access_ports();

private:
    /**
     * @brief Enum to track the TrafficSeparationManager state
     */
    enum class eTsManagerState {
        NO_CONFIG,
        CONFIGURED,
        APPLIED
    } m_state = eTsManagerState::NO_CONFIG;

    /**
     * @brief Map from trunk port interface name to its plumbing implementation.
     */
    std::unordered_map<std::string, std::unique_ptr<PortPlumbing>> m_trunk_port_plumbing_map;

    /**
     * @brief Map from access port interface name to its plumbing implementation.
     */
    std::unordered_map<std::string, std::unique_ptr<PortPlumbing>> m_access_port_plumbing_map;

    /**
     * @brief Last configuration provided via configure().
     */
    sTrafficSeparationConfig m_config{};
};

} // namespace beerocks::net

#endif // TRAFFIC_SEPARATION_MANAGER
