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

#include <bpl/bpl_cfg.h>

#include <algorithm>

namespace beerocks {

TrafficSeparationTask::TrafficSeparationTask(slave_thread &btl_ctx)
    : Task(eTaskType::TRAFFIC_SEPARATION), m_btl_ctx(btl_ctx),
      m_mgr(std::make_unique<net::TrafficSeparationManager>())
{
}

bool TrafficSeparationTask::cleanup_ts_runtime_state()
{
    bool success = true;

    if (m_mgr && !m_mgr->reset()) {
        LOG(ERROR) << "manager reset failed";
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

void TrafficSeparationTask::handle_event(uint8_t event_enum_value, const void *event_obj)
{
    switch (eEvent(event_enum_value)) {
    case TS_ENABLE: {
        schedule_apply();
        break;
    }
    case TS_NEW_WDS_IFACE: {
        if (!event_obj) {
            LOG(ERROR) << "TS_NEW_WDS_IFACE requires event payload";
            break;
        }

        auto iface_name = static_cast<const char *>(event_obj);
        if (!handle_new_wds_iface(iface_name)) {
            LOG(WARNING) << "add WDS iface failed";
        }
        break;
    }
    case TS_CLEAR_WDS_IFACE: {
        if (!event_obj) {
            LOG(ERROR) << "TS_CLEAR_WDS_IFACE requires event payload";
            break;
        }

        auto iface_name = static_cast<const char *>(event_obj);
        if (!handle_clear_wds_iface(iface_name)) {
            LOG(WARNING) << "clear WDS iface failed";
        }
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
        LOG(WARNING) << "full reset failed";
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

    if (!m_mgr->reset()) {
        LOG(ERROR) << "manager reset failed";
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

    std::vector<net::sTrunkPort> trunks;
    std::vector<net::sAccessPort> access_ports;
    if (!get_ports_from_db(trunks, access_ports)) {
        LOG(ERROR) << "get_ports_from_db failed";
        return false;
    }

    for (const auto &trunk : trunks) {
        if (!m_mgr->add_trunk_port(trunk)) {
            LOG(ERROR) << "add_trunk_port failed iface=" << trunk.iface_name;
            if (!m_mgr->reset()) {
                LOG(ERROR) << "manager reset failed after trunk-add failure";
            }
            return false;
        }
    }

    for (const auto &access_port : access_ports) {
        if (!m_mgr->add_access_port(access_port)) {
            LOG(WARNING) << "add_access_port failed iface=" << access_port.iface_name;
        }
    }

    if (!m_mgr->apply_policies()) {
        LOG(ERROR) << "apply_policies failed";
        return false;
    }

    return true;
}

bool TrafficSeparationTask::handle_new_wds_iface(const std::string &iface_name)
{
    if (!m_mgr) {
        LOG(ERROR) << "TS manager is nullptr";
        return false;
    }

    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return false;
    }

    auto db = AgentDB::get();

    if (db->traffic_separation.primary_vlan_id == 0 ||
        db->traffic_separation.primary_vlan_id > net::MAX_VLAN_ID) {
        LOG(DEBUG) << "primary_vlan_id is out of range [" << net::MIN_VLAN_ID << "-"
                   << net::MAX_VLAN_ID << "], nothing to do";
        return true;
    }

    const auto sta_pos = iface_name.rfind(".sta");
    if (sta_pos == std::string::npos || (sta_pos + 4) >= iface_name.size()) {
        LOG(DEBUG) << "Ignoring WDS iface without matching backhaul BSS: " << iface_name;
        return true;
    }

    const auto bss_iface_name = iface_name.substr(0, sta_pos);
    const auto policy         = db->device_conf.unsupported_profile_disallow_policy;
    bool disallow_profile1    = false;
    bool disallow_profile2    = false;
    bool bss_found            = false;

    for (const auto *radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }

        const auto bss_it = std::find_if(
            radio->front.bssids.begin(), radio->front.bssids.end(), [&](const auto &bss) {
                return bss.active && bss.backhaul_bss && (bss.iface_name == bss_iface_name);
            });
        if (bss_it == radio->front.bssids.end()) {
            continue;
        }

        disallow_profile1 = bss_it->backhaul_bss_disallow_profile1_agent_association;
        disallow_profile2 = bss_it->backhaul_bss_disallow_profile2_agent_association;
        bss_found         = true;
        break;
    }

    if (!bss_found) {
        LOG(DEBUG) << "Ignoring WDS iface without matching backhaul BSS: " << iface_name;
        return true;
    }

    net::sTrunkPort trunk{};
    trunk.iface_name  = iface_name;
    trunk.is_ethernet = false;
    // TODO(PPM-3906): WDS trunk untagged mode should consider both the bBSS disallow
    // flags and the downstream agent profile.
    trunk.is_untagged_mode = net::is_untagged_mode(disallow_profile1, disallow_profile2, policy);

    if (!m_mgr->add_trunk_port(trunk)) {
        LOG(ERROR) << "add_trunk_port failed iface=" << iface_name;
        return false;
    }

    return true;
}

bool TrafficSeparationTask::handle_clear_wds_iface(const std::string &iface_name)
{
    if (!m_mgr) {
        LOG(ERROR) << "TS manager is nullptr";
        return false;
    }

    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return false;
    }

    if (!m_mgr->remove_trunk_port(iface_name)) {
        LOG(ERROR) << "remove_trunk_port failed iface=" << iface_name;
        return false;
    }

    return true;
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

bool TrafficSeparationTask::get_ports_from_db(std::vector<net::sTrunkPort> &trunks,
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
            if (!bss.active || !bss.fronthaul_bss || bss.backhaul_bss || bss.iface_name.empty()) {
                continue;
            }

            net::sAccessPort access_port{};
            access_port.iface_name = bss.iface_name;
            access_ports.push_back(access_port);
        }
    }

    return true;
}

} // namespace beerocks
