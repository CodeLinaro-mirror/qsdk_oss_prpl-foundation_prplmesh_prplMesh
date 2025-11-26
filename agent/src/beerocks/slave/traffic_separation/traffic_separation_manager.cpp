/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "traffic_separation_manager.h"

#include "access_port_plumbing_8021q.h"
#include "trunk_port_plumbing_8021q.h"

#include <memory>

#include <easylogging++.h>

namespace beerocks::net {

bool TrafficSeparationManager::configure(const sTrafficSeparationConfig &cfg)
{
    if (cfg.private_bridge.empty() || cfg.guest_bridge.empty() || cfg.private_vid == 0 ||
        cfg.guest_vid == 0) {
        LOG(ERROR) << "TrafficSeparationManager::configure: invalid config";
        return false;
    }

    m_config = cfg;
    m_state  = eTsManagerState::CONFIGURED;

    return true;
}

bool TrafficSeparationManager::add_trunk_port(const sTrunkPort &trunk_port)
{
    if (trunk_port.iface_name.empty()) {
        LOG(ERROR) << "TrafficSeparationManager::add_trunk_port: empty iface_name";
        return false;
    }

    auto it = m_trunk_port_plumbing_map.find(trunk_port.iface_name);
    if (it != m_trunk_port_plumbing_map.end()) {
        LOG(DEBUG) << "TrafficSeparationManager::add_trunk_port: trunk_port iface="
                   << trunk_port.iface_name << " is already managed";
        return true;
    }

    auto plumbing = std::make_unique<TrunkPortPlumbing8021q>(trunk_port);

    // If TS is applied at runtime, immediately apply on this new trunk port.
    if (is_applied()) {
        if (!plumbing->apply(m_config)) {
            LOG(ERROR) << "TrafficSeparationManager::add_trunk_port: apply failed iface="
                       << trunk_port.iface_name;
            return false;
        }
    }

    m_trunk_port_plumbing_map.emplace(trunk_port.iface_name, std::move(plumbing));

    LOG(DEBUG) << "TrafficSeparationManager::add_trunk_port: trunk_port added iface="
               << trunk_port.iface_name;
    return true;
}

bool TrafficSeparationManager::remove_trunk_port(const std::string &iface_name)
{
    auto it = m_trunk_port_plumbing_map.find(iface_name);
    if (it == m_trunk_port_plumbing_map.end()) {
        LOG(DEBUG) << "TrafficSeparationManager::remove_trunk_port: not managed trunk_port iface="
                   << iface_name;
        return true;
    }

    bool success = true;

    // If TS is applied at runtime, clear trunk port before removing.
    if (is_applied() && it->second) {
        if (!it->second->clear()) {
            LOG(ERROR) << "TrafficSeparationManager::remove_trunk_port: clear failed iface="
                       << iface_name;
            success = false;
        }
    }

    m_trunk_port_plumbing_map.erase(it);

    LOG(DEBUG) << "TrafficSeparationManager::remove_trunk_port: removed iface=" << iface_name;
    return success;
}

bool TrafficSeparationManager::add_access_port(const sAccessPort &access_port)
{
    if (access_port.iface_name.empty()) {
        LOG(ERROR) << "TrafficSeparationManager::add_access_port: empty iface_name";
        return false;
    }

    auto it = m_access_port_plumbing_map.find(access_port.iface_name);
    if (it != m_access_port_plumbing_map.end()) {
        LOG(DEBUG) << "TrafficSeparationManager::add_access_port: iface=" << access_port.iface_name
                   << " is already managed";
        return true;
    }

    auto plumbing = std::make_unique<AccessPortPlumbing8021q>(access_port);

    // If TS is applied at runtime, immediately apply on this new access port.
    if (is_applied()) {
        if (!plumbing->apply(m_config)) {
            LOG(ERROR) << "TrafficSeparationManager::add_access_port: apply failed iface="
                       << access_port.iface_name;
            return false;
        }
    }

    m_access_port_plumbing_map.emplace(access_port.iface_name, std::move(plumbing));

    LOG(DEBUG) << "TrafficSeparationManager::add_access_port: added iface="
               << access_port.iface_name;
    return true;
}

bool TrafficSeparationManager::remove_access_port(const std::string &iface_name)
{
    auto it = m_access_port_plumbing_map.find(iface_name);
    if (it == m_access_port_plumbing_map.end()) {
        LOG(DEBUG) << "TrafficSeparationManager::remove_access_port: iface=" << iface_name
                   << " is not managed";
        return true;
    }

    bool success = true;

    // If TS is applied at runtime, clear the access port before removing.
    if (is_applied() && it->second) {
        if (!it->second->clear()) {
            LOG(ERROR) << "TrafficSeparationManager::remove_access_port: clear failed iface="
                       << iface_name;
            success = false;
        }
    }

    m_access_port_plumbing_map.erase(it);

    LOG(DEBUG) << "TrafficSeparationManager::remove_access_port: removed iface=" << iface_name;
    return success;
}

bool TrafficSeparationManager::apply_policies()
{
    if (is_applied()) {
        LOG(DEBUG) << "TrafficSeparationManager::apply_policies: already applied";
        return true;
    }

    if (!has_any_ports()) {
        LOG(DEBUG) << "TrafficSeparationManager::apply_policies: no trunk or access ports managed";
        return true;
    }

    if (!has_config()) {
        LOG(WARNING) << "TrafficSeparationManager::apply_policies: no TS config present";
        return false;
    }

    bool success = true;

    // Apply on trunk ports.
    for (auto &entry : m_trunk_port_plumbing_map) {
        const auto &iface_name = entry.first;
        auto &plumbing         = entry.second;

        if (!plumbing) {
            LOG(ERROR)
                << "TrafficSeparationManager::apply_policies: null trunk_port plumbing iface="
                << iface_name;
            success = false;
            continue;
        }

        if (!plumbing->apply(m_config)) {
            LOG(ERROR) << "TrafficSeparationManager::apply_policies: trunk_port apply failed iface="
                       << iface_name;
            success = false;
        }
    }

    // Apply on access ports.
    for (auto &entry : m_access_port_plumbing_map) {
        const auto &iface_name = entry.first;
        auto &plumbing         = entry.second;

        if (!plumbing) {
            LOG(ERROR) << "TrafficSeparationManager::apply_policies: null access plumbing iface="
                       << iface_name;
            success = false;
            continue;
        }

        if (!plumbing->apply(m_config)) {
            LOG(ERROR) << "TrafficSeparationManager::apply_policies: access apply failed iface="
                       << iface_name;
            success = false;
        }
    }

    if (!success) {
        clear_policies();
        m_state = eTsManagerState::CONFIGURED;
        LOG(ERROR) << "TrafficSeparationManager::apply_policies: TS apply failed, rollback done";
        return false;
    }

    LOG(DEBUG) << "TrafficSeparationManager::apply_policies: all policies applied successfully";

    m_state = eTsManagerState::APPLIED;
    return true;
}

bool TrafficSeparationManager::clear_policies()
{
    bool success = true;

    // Clear trunk ports.
    for (auto &entry : m_trunk_port_plumbing_map) {
        const auto &iface_name = entry.first;
        auto &plumbing         = entry.second;

        if (!plumbing) {
            LOG(ERROR)
                << "TrafficSeparationManager::clear_policies: null trunk_port plumbing iface="
                << iface_name;
            success = false;
            continue;
        }

        if (!plumbing->clear()) {
            LOG(ERROR) << "TrafficSeparationManager::clear_policies: trunk_port clear failed iface="
                       << iface_name;
            success = false;
        }
    }

    // Clear access ports.
    for (auto &entry : m_access_port_plumbing_map) {
        const auto &iface_name = entry.first;
        auto &plumbing         = entry.second;

        if (!plumbing) {
            LOG(ERROR) << "TrafficSeparationManager::clear_policies: null access plumbing iface="
                       << iface_name;
            success = false;
            continue;
        }

        if (!plumbing->clear()) {
            LOG(ERROR) << "TrafficSeparationManager::clear_policies: access clear failed iface="
                       << iface_name;
            success = false;
        }
    }

    if (success) {
        m_state = eTsManagerState::CONFIGURED;
    }

    LOG(DEBUG) << "TrafficSeparationManager::clear_policies: clear finished, success=" << success;

    return success;
}

bool TrafficSeparationManager::reset()
{
    bool success = true;

    if (!clear_policies()) {
        LOG(ERROR) << "TrafficSeparationManager::reset: clear_policies failed";
        success = false;
    }

    if (!remove_all_trunk_ports()) {
        LOG(ERROR) << "TrafficSeparationManager::reset: remove_all_trunk_ports failed";
        success = false;
    }

    if (!remove_all_access_ports()) {
        LOG(ERROR) << "TrafficSeparationManager::reset: remove_all_access_ports failed";
        success = false;
    }

    m_config = sTrafficSeparationConfig{};
    m_state  = eTsManagerState::NO_CONFIG;

    return success;
}

bool TrafficSeparationManager::remove_all_trunk_ports()
{
    if (m_trunk_port_plumbing_map.empty()) {
        LOG(DEBUG) << "TrafficSeparationManager::remove_all_trunk_ports: no trunk ports managed";
        return true;
    }

    bool success = true;

    // Collect iface names first because remove_trunk_port() erases each from the map.
    std::vector<std::string> ifaces;
    ifaces.reserve(m_trunk_port_plumbing_map.size());
    for (const auto &entry : m_trunk_port_plumbing_map) {
        ifaces.push_back(entry.first);
    }

    for (const auto &iface_name : ifaces) {
        if (!remove_trunk_port(iface_name)) {
            success = false;
        }
    }

    return success;
}

bool TrafficSeparationManager::remove_all_access_ports()
{
    if (m_access_port_plumbing_map.empty()) {
        LOG(DEBUG) << "TrafficSeparationManager::remove_all_access_ports: no access ports managed";
        return true;
    }

    bool success = true;

    std::vector<std::string> ifaces;
    ifaces.reserve(m_access_port_plumbing_map.size());
    for (const auto &entry : m_access_port_plumbing_map) {
        ifaces.push_back(entry.first);
    }

    for (const auto &iface_name : ifaces) {
        if (!remove_access_port(iface_name)) {
            success = false;
        }
    }

    return success;
}

} // namespace beerocks::net
