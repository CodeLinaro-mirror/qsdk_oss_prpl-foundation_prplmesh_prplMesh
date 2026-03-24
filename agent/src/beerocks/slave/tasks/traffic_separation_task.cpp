/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "traffic_separation_task.h"

#include "agent_db.h"
#include "son_slave_thread.h"

#include "traffic_separation/traffic_separation_utils.h"

#include <bcl/network/network_utils.h>
#include <bpl/bpl_cfg.h>

#include <algorithm>
#include <unordered_set>

namespace beerocks {

TrafficSeparationTask::TrafficSeparationTask(slave_thread &btl_ctx)
    : Task(eTaskType::TRAFFIC_SEPARATION), m_btl_ctx(btl_ctx),
      m_mgr(std::make_unique<net::TrafficSeparationManager>())
{
}

bool TrafficSeparationTask::cleanup_ts_runtime_state()
{
    bool success = true;

    if (m_mgr && !m_mgr->clear_policies()) {
        LOG(ERROR) << "manager clear_policies failed";
        success = false;
    }

    if (!ensure_transport_primary_vlan(0)) {
        LOG(ERROR) << "ensure_transport_primary_vlan(0) failed";
        success = false;
    }

    if (!reset_transport_monitoring_on_bridge()) {
        LOG(ERROR) << "reset_transport_monitoring_on_bridge failed";
        success = false;
    }

    return success;
}

void TrafficSeparationTask::handle_event(uint8_t event_enum_value, const void * /*event_obj*/)
{
    switch (eEvent(event_enum_value)) {
    case TS_ENABLE: {
        schedule_apply();
        break;
    }
    case TS_NEW_BH_STA_IFACE: {
        schedule_apply();
        break;
    }
    case TS_CLEAR_BH_STA_IFACE: {
        schedule_apply();
        break;
    }
    case TS_CLEAR: {
        if (!cleanup_ts_runtime_state()) {
            LOG(ERROR) << "cleanup_ts_runtime_state failed";
        }

        clear_pending_apply();
        break;
    }
    default:
        break;
    }
}

void TrafficSeparationTask::schedule_apply()
{
    m_pending = true;

    const auto now = std::chrono::steady_clock::now();
    const auto due = now + std::chrono::milliseconds(DEBOUNCE_MS);

    if (m_next_run <= now) {
        m_next_run = due;
    }
}

void TrafficSeparationTask::clear_pending_apply()
{
    m_pending  = false;
    m_next_run = m_next_run.min();
}

bool TrafficSeparationTask::should_run_now() const
{
    if (!m_pending) {
        return false;
    }
    return std::chrono::steady_clock::now() >= m_next_run;
}

void TrafficSeparationTask::work()
{
    if (!should_run_now()) {
        return;
    }

    clear_pending_apply();

    if (!reset()) {
        LOG(WARNING) << "TS reset failed";
    }
}

bool TrafficSeparationTask::ensure_transport_primary_vlan(uint16_t primary_vid)
{
    LOG(TRACE) << "Configuring transport primary VLAN id=" << primary_vid;
    if (primary_vid == m_last_primary_vid) {
        return true;
    }

    const bool add = (primary_vid != 0);
    if (!m_btl_ctx.m_broker_client) {
        LOG(WARNING) << "broker_client is null, skip";
        return false;
    }
    if (!m_btl_ctx.m_broker_client->configure_primary_vlan_id(primary_vid, add)) {
        LOG(ERROR) << "configure_primary_vlan_id failed vid=" << primary_vid;
        return false;
    }

    m_last_primary_vid = primary_vid;
    return true;
}

bool TrafficSeparationTask::reset_transport_monitoring_on_bridge()
{
    LOG(TRACE) << "Resetting transport monitoring on bridge";
    if (!m_btl_ctx.m_broker_client) {
        LOG(WARNING) << "broker_client is null, skip";
        return false;
    }

    auto db = AgentDB::get();
    if (!m_btl_ctx.m_broker_client->configure_interfaces(db->bridge.iface_name, {}, true, true)) {
        LOG(ERROR) << "configure_interfaces(bridge) failed";
        return false;
    }

    return true;
}

bool TrafficSeparationTask::build_ts_config(net::sTrafficSeparationConfig &cfg) const
{
    auto db = AgentDB::get();

    if (db->traffic_separation.primary_vlan_id == 0 ||
        db->traffic_separation.primary_vlan_id > net::MAX_VLAN_ID) {
        LOG(ERROR) << "TS config: primary vlan id is out of range [" << net::MIN_VLAN_ID << "-"
                   << net::MAX_VLAN_ID << "], got=" << db->traffic_separation.primary_vlan_id;
        return false;
    }

    if (!bpl::cfg_get_private_bridge_iface(cfg.private_bridge)) {
        cfg.private_bridge = bpl::DEFAULT_PRIVATE_BRIDGE_IFACE;
    }
    if (!bpl::cfg_get_guest_bridge_iface(cfg.guest_bridge)) {
        cfg.guest_bridge = bpl::DEFAULT_GUEST_BRIDGE_IFACE;
    }

    cfg.private_vid = db->traffic_separation.primary_vlan_id;

    const auto default_guest_vid = static_cast<uint32_t>(bpl::DEFAULT_GUEST_VLAN_ID);
    if (!db->traffic_separation.secondary_vlans_ids.empty()) {
        cfg.guest_vid = *db->traffic_separation.secondary_vlans_ids.begin();
    } else {
        cfg.guest_vid = default_guest_vid;
        LOG(WARNING) << "TS config: no secondary vlan id, using default guest_vid="
                     << cfg.guest_vid;
    }

    if (cfg.guest_vid < net::MIN_VLAN_ID || cfg.guest_vid > net::MAX_VLAN_ID) {
        LOG(WARNING) << "TS config: guest vlan id is out of range [" << net::MIN_VLAN_ID << "-"
                     << net::MAX_VLAN_ID << "], got=" << cfg.guest_vid
                     << ", using default guest_vid=" << default_guest_vid;
        cfg.guest_vid = default_guest_vid;
    }

    return true;
}

bool TrafficSeparationTask::reset()
{
    auto db = AgentDB::get();

    const uint16_t primary_vid = db->traffic_separation.primary_vlan_id;
    if (primary_vid == 0 || primary_vid > net::MAX_VLAN_ID) {
        if (!cleanup_ts_runtime_state()) {
            LOG(ERROR) << "cleanup_ts_runtime_state failed (invalid/disabled primary_vlan_id)";
            return false;
        }

        if (primary_vid == 0) {
            LOG(INFO) << "primary_vlan_id=0, TS is disabled, clearing";
            return true;
        } else {
            LOG(ERROR) << "primary_vlan_id is out of range [" << net::MIN_VLAN_ID << "-"
                       << net::MAX_VLAN_ID << "], got=" << primary_vid;
            return false;
        }
    }

    if (!ensure_transport_primary_vlan(primary_vid)) {
        LOG(ERROR) << "failed to set transport primary_vlan_id=" << primary_vid;
        return false;
    }

    if (!m_mgr) {
        m_mgr = std::make_unique<net::TrafficSeparationManager>();
    }

    if (!m_mgr->clear_policies()) {
        LOG(ERROR) << "manager clear_policies failed";
        return false;
    }

    net::sTrafficSeparationConfig cfg{};
    if (!build_ts_config(cfg)) {
        LOG(ERROR) << "failed to build config";
        return false;
    }

    if (!m_mgr->configure(cfg)) {
        LOG(ERROR) << "manager configure failed";
        return false;
    }

    if (!handle_clear_sta_iface()) {
        LOG(ERROR) << "handle_clear_sta_iface failed";
        return false;
    }

    std::vector<net::sTrunkPort> trunks;
    std::vector<net::sAccessPort> access_ports;
    if (!collect_ports_from_db(trunks, access_ports)) {
        LOG(ERROR) << "collect_ports_from_db failed";
        return false;
    }

    for (const auto &trunk : trunks) {
        if (!m_mgr->add_trunk_port(trunk)) {
            LOG(ERROR) << "add_trunk_port failed iface=" << trunk.iface_name;
            return false;
        }
    }

    for (const auto &access_port : access_ports) {
        if (!m_mgr->add_access_port(access_port)) {
            LOG(WARNING) << "add_access_port failed iface=" << access_port.iface_name;
        }
    }

    if (!handle_new_sta_iface()) {
        LOG(ERROR) << "handle_new_sta_iface failed";
        return false;
    }

    if (!m_mgr->apply_policies()) {
        LOG(ERROR) << "apply_policies failed";
        return false;
    }

    return true;
}

bool TrafficSeparationTask::handle_new_sta_iface()
{
    if (!m_mgr) {
        LOG(ERROR) << "TS manager is nullptr";
        return false;
    }

    const auto current_wds_ifaces = collect_current_wds_ifaces();
    bool success                  = true;

    for (const auto &wds_iface : current_wds_ifaces) {
        if (m_tracked_wds_ifaces.count(wds_iface.first) != 0) {
            continue;
        }

        net::sTrunkPort trunk{};
        trunk.iface_name       = wds_iface.first;
        trunk.is_ethernet      = false;
        trunk.is_untagged_mode = wds_iface.second;

        if (!m_mgr->add_trunk_port(trunk)) {
            LOG(ERROR) << "add_trunk_port failed iface=" << trunk.iface_name;
            success = false;
            continue;
        }

        m_tracked_wds_ifaces.insert(trunk.iface_name);
    }

    return success;
}

bool TrafficSeparationTask::handle_clear_sta_iface()
{
    if (!m_mgr) {
        LOG(ERROR) << "TS manager is nullptr";
        return false;
    }

    const auto current_wds_ifaces = collect_current_wds_ifaces();
    auto next_tracked_wds_ifaces  = m_tracked_wds_ifaces;
    bool success                  = true;

    for (const auto &tracked_wds_iface : m_tracked_wds_ifaces) {
        if (current_wds_ifaces.count(tracked_wds_iface) != 0) {
            continue;
        }

        if (!m_mgr->remove_trunk_port(tracked_wds_iface)) {
            LOG(ERROR) << "remove_trunk_port failed iface=" << tracked_wds_iface;
            success = false;
            continue;
        }

        next_tracked_wds_ifaces.erase(tracked_wds_iface);
    }

    m_tracked_wds_ifaces = next_tracked_wds_ifaces;
    return success;
}

std::unordered_map<std::string, bool> TrafficSeparationTask::collect_current_wds_ifaces() const
{
    std::unordered_map<std::string, bool> current_wds_ifaces;
    auto db = AgentDB::get();

    for (const auto *radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }

        for (const auto &bss : radio->front.bssids) {
            if (!bss.active || !bss.backhaul_bss || bss.iface_name.empty()) {
                continue;
            }

            const auto policy            = db->device_conf.unsupported_profile_disallow_policy;
            const bool disallow_profile1 = bss.backhaul_bss_disallow_profile1_agent_association;
            const bool disallow_profile2 = bss.backhaul_bss_disallow_profile2_agent_association;
            const bool untagged_mode =
                net::is_untagged_mode(disallow_profile1, disallow_profile2, policy);

            LOG(TRACE) << "bBSS iface=" << bss.iface_name
                       << " profile1_disallow=" << disallow_profile1
                       << " profile2_disallow=" << disallow_profile2
                       << " policy=" << unsupported_profile_disallow_policy_to_string(policy)
                       << " resolved_mode=" << (untagged_mode ? "untagged" : "tagged");

            if (disallow_profile1 == disallow_profile2) {
                if (policy == eUnsupportedProfileDisallowPolicy::NO_OVERRIDE) {
                    LOG(WARNING) << "Unsupported bBSS profile-disallow configuration for iface="
                                 << bss.iface_name << " (profile1_disallow=" << disallow_profile1
                                 << ", profile2_disallow=" << disallow_profile2 << ", policy="
                                 << unsupported_profile_disallow_policy_to_string(policy)
                                 << "), defaulting to tagged mode";
                } else {
                    LOG(INFO) << "Overriding unsupported bBSS profile-disallow configuration for "
                                 "iface="
                              << bss.iface_name << " (profile1_disallow=" << disallow_profile1
                              << ", profile2_disallow=" << disallow_profile2 << ", policy="
                              << unsupported_profile_disallow_policy_to_string(policy)
                              << ", resolved_mode=" << (untagged_mode ? "untagged" : "tagged")
                              << ")";
                }
            }

            const auto sta_ifaces = get_all_sta_ifaces_for_bss(bss.iface_name);
            for (const auto &sta : sta_ifaces) {
                current_wds_ifaces.emplace(sta, untagged_mode);
            }
        }
    }

    return current_wds_ifaces;
}

bool TrafficSeparationTask::add_backhaul_connection_trunk(
    std::vector<net::sTrunkPort> &trunks) const
{
    auto db = AgentDB::get();

    const auto con_type    = db->backhaul.connection_type;
    const auto &wifi_iface = db->backhaul.selected_iface_name;
    const auto &eth_iface  = db->ethernet.wan.iface_name;

    if (con_type == AgentDB::sBackhaul::eConnectionType::Invalid) {
        return true;
    }

    net::sTrunkPort trunk{};

    switch (con_type) {
    case AgentDB::sBackhaul::eConnectionType::Wireless: {
        if (wifi_iface.empty()) {
            return true;
        }
        trunk.iface_name  = wifi_iface;
        trunk.is_ethernet = false;

        const uint8_t local = static_cast<uint8_t>(db->device_conf.multi_ap_profile);
        const uint8_t peer  = static_cast<uint8_t>(db->backhaul.backhaul_bss_multi_ap_profile);
        const uint8_t eff   = std::min(local, peer);

        trunk.is_untagged_mode = (eff <= 1);
        break;
    }
    case AgentDB::sBackhaul::eConnectionType::Wired: {
        if (eth_iface.empty()) {
            return true;
        }
        trunk.iface_name       = eth_iface;
        trunk.is_ethernet      = true;
        trunk.is_untagged_mode = false;
        break;
    }
    default:
        LOG(ERROR) << "unknown backhaul connection type";
        return false;
    }

    trunks.push_back(trunk);
    return true;
}

bool TrafficSeparationTask::collect_ports_from_db(std::vector<net::sTrunkPort> &trunks,
                                                  std::vector<net::sAccessPort> &access_ports) const
{
    trunks.clear();
    access_ports.clear();

    auto db = AgentDB::get();

    if (!add_backhaul_connection_trunk(trunks)) {
        LOG(ERROR) << "failed to add backhaul trunk";
        return false;
    }

    for (const auto &lan : db->ethernet.lan) {
        if (lan.iface_name.empty()) {
            continue;
        }
        net::sTrunkPort trunk{};
        trunk.iface_name       = lan.iface_name;
        trunk.is_ethernet      = true;
        trunk.is_untagged_mode = false;
        trunks.push_back(trunk);
    }

    for (const auto *radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }

        for (const auto &bss : radio->front.bssids) {
            if (!bss.active) {
                continue;
            }

            if (bss.fronthaul_bss && !bss.backhaul_bss) {
                if (!bss.iface_name.empty()) {
                    net::sAccessPort access_port{};
                    access_port.iface_name = bss.iface_name;
                    access_ports.push_back(access_port);
                }
            }

        }
    }

    return true;
}

int TrafficSeparationTask::sta_index_for_prefix(const std::string &ifname,
                                                const std::string &prefix)
{
    // "starts_with" equivalent / cppcheck performance issue
    if (ifname.size() <= prefix.size() || ifname.compare(0, prefix.size(), prefix) != 0) {
        return -1;
    }

    int v = 0;
    for (size_t i = prefix.size(); i < ifname.size(); ++i) {
        const char c = ifname[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        v = (v * 10) + (c - '0');
    }
    return v;
}

std::vector<std::string>
TrafficSeparationTask::get_all_sta_ifaces_for_bss(const std::string &bss_iface)
{
    std::vector<std::string> out;
    if (bss_iface.empty()) {
        return out;
    }

    const std::string prefix = bss_iface + ".sta";
    const auto all_ifaces    = beerocks::net::network_utils::linux_get_iface_list();
    std::unordered_set<std::string> seen;
    seen.reserve(all_ifaces.size());

    for (const auto &iface : all_ifaces) {
        const int idx = sta_index_for_prefix(iface, prefix);
        if (idx >= 0 && seen.insert(iface).second) {
            out.push_back(iface);
        }
    }

    return out;
}

} // namespace beerocks
