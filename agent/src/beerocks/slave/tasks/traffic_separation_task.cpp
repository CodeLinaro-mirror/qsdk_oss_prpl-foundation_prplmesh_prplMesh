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
#include <bcl/network/network_utils.h>

#include <algorithm>
#include <utility>

namespace beerocks {

constexpr int TrafficSeparationTask::WDS_RETRY_TIMEOUT_MS;

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

        m_pending_wds_ifaces.clear();
        clear_pending_apply();
        break;
    }
    default:
        break;
    }
}

void TrafficSeparationTask::schedule_apply()
{
    m_apply_pending = true;
    schedule_deferred_work(std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(DEBOUNCE_MS));
}

void TrafficSeparationTask::schedule_deferred_work(std::chrono::steady_clock::time_point due)
{
    m_pending = true;

    if (m_next_run == std::chrono::steady_clock::time_point::min() || due < m_next_run) {
        m_next_run = due;
    }
}

void TrafficSeparationTask::clear_pending_apply()
{
    m_apply_pending = false;
    if (m_pending_wds_ifaces.empty()) {
        clear_scheduled_work();
    }
}

void TrafficSeparationTask::clear_scheduled_work()
{
    m_pending  = false;
    m_next_run = std::chrono::steady_clock::time_point::min();
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

    clear_scheduled_work();

<<<<<<< HEAD
    if (!reset()) {
        LOG(WARNING) << "TS reset failed";
=======
    const auto apply_pending = m_apply_pending;
    m_apply_pending          = false;

    if (apply_pending && !clear()) {
        LOG(WARNING) << "TS clear failed";
>>>>>>> 24fe785a3 (fixup! agent: traffic_separation: handle exact WDS ifaces)
    }

    if (!retry_pending_wds_ifaces()) {
        LOG(WARNING) << "pending WDS retry failed";
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
            LOG(ERROR) << "cleanup_ts_runtime_state failed (invalid/disabled "
                          "primary_vlan_id)";
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

    // Exact WDS ifaces are owned by TS_NEW_WDS_IFACE / TS_CLEAR_WDS_IFACE events.
    // Keep the manager port maps intact here and only refresh the active
    // policies.
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

    std::vector<net::sTrunkPort> trunks;
    std::vector<net::sAccessPort> access_ports;
    if (!get_ports_from_db(trunks, access_ports)) {
        LOG(ERROR) << "get_ports_from_db failed";
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

    if (!m_mgr->apply_policies()) {
        LOG(ERROR) << "apply_policies failed";
        return false;
    }

    return true;
}

<<<<<<< HEAD
=======
bool TrafficSeparationTask::reset()
{
    bool success = true;

    m_pending_wds_ifaces.clear();
    clear_pending_apply();

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

bool TrafficSeparationTask::handle_new_fh_iface(const std::string &iface_name)
{
    if (!m_mgr) {
        LOG(ERROR) << "TS manager is nullptr";
        return false;
    }

    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return false;
    }

    net::sAccessPort access_port{};
    access_port.iface_name = iface_name;

    if (!m_mgr->add_access_port(access_port)) {
        LOG(ERROR) << "add_access_port failed iface=" << iface_name;
        return false;
    }

    return true;
}

bool TrafficSeparationTask::handle_clear_fh_iface(const std::string &iface_name)
{
    if (!m_mgr) {
        LOG(ERROR) << "TS manager is nullptr";
        return false;
    }

    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return false;
    }

    if (!m_mgr->remove_access_port(iface_name)) {
        LOG(ERROR) << "remove_access_port failed iface=" << iface_name;
        return false;
    }

    return true;
}

>>>>>>> 24fe785a3 (fixup! agent: traffic_separation: handle exact WDS ifaces)
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
    // TODO(PPM-3941): WDS trunk untagged mode should primary consider downstream agent multi-ap
    // profile that it's reported via Assoc Req
    trunk.is_untagged_mode = net::is_untagged_mode(disallow_profile1, disallow_profile2, policy);

    const auto now = std::chrono::steady_clock::now();
    auto pending_it = m_pending_wds_ifaces.find(iface_name);
    if (pending_it == m_pending_wds_ifaces.end()) {
        const auto settle_due = now + std::chrono::milliseconds(WDS_SETTLE_MS);
        const auto timeout_at = now + std::chrono::milliseconds(WDS_RETRY_TIMEOUT_MS);

        pending_it =
            m_pending_wds_ifaces
                .emplace(iface_name, sPendingWdsIfaceState{settle_due, timeout_at})
                .first;

        LOG(DEBUG) << "Deferring WDS TS apply for settle window: " << iface_name;
    }

    if (now < pending_it->second.not_before) {
        schedule_deferred_work(pending_it->second.not_before);
        return true;
    }

    const bool iface_exists = net::network_utils::linux_iface_exists(iface_name);
    const bool iface_ready =
        iface_exists &&
        (trunk.is_untagged_mode || net::network_utils::linux_iface_is_up_and_running(iface_name));

    if (!iface_ready) {
        if (now >= pending_it->second.deadline) {
            if (!iface_exists) {
                LOG(WARNING) << "WDS iface is still missing after " << WDS_RETRY_TIMEOUT_MS
                             << "ms, skip TS apply iface=" << iface_name;
            } else {
                LOG(WARNING) << "Tagged WDS iface is still not up and running after "
                             << WDS_RETRY_TIMEOUT_MS << "ms, skip TS apply iface=" << iface_name;
            }
            m_pending_wds_ifaces.erase(pending_it);
            return true;
        }

        auto retry_due = now + std::chrono::milliseconds(DEBOUNCE_MS);
        if (retry_due > pending_it->second.deadline) {
            retry_due = pending_it->second.deadline;
        }
        schedule_deferred_work(retry_due);
        return true;
    }

    // A reconnect may reuse the same WDS iface name while the old exact entry
    // is still managed. Refresh it so trunk mode is recalculated from the
    // latest event-derived state before the new add path runs.
    if (!m_mgr->remove_trunk_port(iface_name)) {
        m_pending_wds_ifaces.erase(pending_it);
        LOG(ERROR) << "remove_trunk_port failed iface=" << iface_name;
        return false;
    }

    if (!m_mgr->add_trunk_port(trunk)) {
        m_pending_wds_ifaces.erase(pending_it);
        LOG(ERROR) << "add_trunk_port failed iface=" << iface_name;
        return false;
    }

    m_pending_wds_ifaces.erase(pending_it);
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

    m_pending_wds_ifaces.erase(iface_name);
    if (!m_apply_pending && m_pending_wds_ifaces.empty()) {
        clear_scheduled_work();
    }

    if (!m_mgr->remove_trunk_port(iface_name)) {
        LOG(ERROR) << "remove_trunk_port failed iface=" << iface_name;
        return false;
    }

    return true;
}

bool TrafficSeparationTask::retry_pending_wds_ifaces()
{
    if (m_pending_wds_ifaces.empty()) {
        return true;
    }

    bool success = true;
    std::vector<std::string> ifaces;
    ifaces.reserve(m_pending_wds_ifaces.size());

    for (const auto &pending_iface : m_pending_wds_ifaces) {
        ifaces.push_back(pending_iface.first);
    }

    for (const auto &iface_name : ifaces) {
        if (!handle_new_wds_iface(iface_name)) {
            success = false;
        }
    }

    return success;
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
            access_ports.push_back(std::move(access_port));
        }
    }

    return true;
}

} // namespace beerocks
