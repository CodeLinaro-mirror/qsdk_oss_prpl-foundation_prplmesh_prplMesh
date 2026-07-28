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

    // NO_CONFIG marks a fully completed cleanup. Keep CONFIGURED/APPLIED on
    // failure so a subsequent policy update retries the remaining work.
    if (success && m_mgr && !m_mgr->reset()) {
        LOG(ERROR) << "manager reset failed";
        success = false;
    }

    return success;
}

void TrafficSeparationTask::handle_event(uint8_t event_enum_value, const void *event_obj)
{
    switch (eEvent(event_enum_value)) {
    case TS_APPLY: {
        request_apply();
        break;
    }
    case TS_POLICY_UPDATE: {
        request_policy_update();
        break;
    }
    case TS_NEW_FH_IFACE: {
        if (!event_obj) {
            LOG(ERROR) << "TS_NEW_FH_IFACE requires event payload";
            break;
        }

        auto iface_name = static_cast<const char *>(event_obj);
        if (!handle_new_fh_iface(iface_name)) {
            LOG(WARNING) << "add fronthaul iface failed";
        }
        break;
    }
    case TS_CLEAR_FH_IFACE: {
        if (!event_obj) {
            LOG(ERROR) << "TS_CLEAR_FH_IFACE requires event payload";
            break;
        }

        auto iface_name = static_cast<const char *>(event_obj);
        if (!handle_clear_fh_iface(iface_name)) {
            LOG(WARNING) << "clear fronthaul iface failed";
        }
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

void TrafficSeparationTask::run_at(std::chrono::steady_clock::time_point due)
{
    m_pending = true;

    if (m_next_run == std::chrono::steady_clock::time_point::min() || due < m_next_run) {
        m_next_run = due;
    }
}

void TrafficSeparationTask::request_apply()
{
    m_apply_pending = true;
    run_at(std::chrono::steady_clock::now());
}

void TrafficSeparationTask::request_policy_update()
{
    net::sTrafficSeparationConfig config{};
    const bool is_configured = m_mgr && build_ts_config(config) && m_mgr->is_applied_with(config);
    if (is_configured) {
        LOG(DEBUG) << "Effective TS policy is unchanged, preserving already applied TS state";
        return;
    }

    request_apply();
}

void TrafficSeparationTask::request_wds_retry(const std::string &iface_name,
                                              std::chrono::steady_clock::time_point not_before,
                                              std::chrono::steady_clock::time_point deadline)
{
    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return;
    }

    auto pending_it = m_pending_wds_ifaces.find(iface_name);
    if (pending_it == m_pending_wds_ifaces.end()) {
        pending_it =
            m_pending_wds_ifaces.emplace(iface_name, sPendingWdsIfaceState{not_before, deadline})
                .first;
    }

    run_at(pending_it->second.not_before);
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

    const auto apply_pending = m_apply_pending;
    m_apply_pending          = false;

    if (apply_pending && !reconcile()) {
        LOG(WARNING) << "TS reconciliation failed";
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

    const auto primary_vid = db->traffic_separation.primary_vlan_id;
    if (primary_vid > net::MAX_VLAN_ID) {
        LOG(ERROR) << "TS config: primary vlan id is out of range [" << net::MIN_VLAN_ID << "-"
                   << net::MAX_VLAN_ID << "], got=" << primary_vid;
        return false;
    }
    if (primary_vid == 0) {
        return true;
    }

    if (!bpl::cfg_get_private_bridge_iface(cfg.private_bridge)) {
        cfg.private_bridge = bpl::DEFAULT_PRIVATE_BRIDGE_IFACE;
    }
    if (!bpl::cfg_get_guest_bridge_iface(cfg.guest_bridge)) {
        cfg.guest_bridge = bpl::DEFAULT_GUEST_BRIDGE_IFACE;
    }

    cfg.private_vid = primary_vid;

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

bool TrafficSeparationTask::reconcile()
{
    uint16_t primary_vid = 0;
    {
        auto db     = AgentDB::get();
        primary_vid = db->traffic_separation.primary_vlan_id;
    }
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

    // Full TS_APPLY reconciliation must not trust cached APPLIED state:
    // WDS reconnect can recreate the parent iface without its VLAN subifaces.
    // clear_policies() forces a fresh apply while keeping manager port entries;
    // refresh/DB restore below prune stale ports and repopulate missed FH/WDS events.
    if (!m_mgr->clear_policies()) {
        LOG(ERROR) << "manager clear_policies failed";
        return false;
    }

    if (!m_mgr->refresh_ports()) {
        LOG(ERROR) << "refresh_ports failed";
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
    if (!get_ports_from_db(trunks)) {
        LOG(ERROR) << "get_ports_from_db failed";
        return false;
    }

    for (const auto &trunk : trunks) {
        if (!m_mgr->add_trunk_port(trunk)) {
            LOG(ERROR) << "add_trunk_port failed iface=" << trunk.iface_name;
            return false;
        }
    }

    if (!restore_exact_ports_from_db()) {
        LOG(ERROR) << "restore_exact_ports_from_db failed";
        return false;
    }

    if (!m_mgr->apply_policies()) {
        LOG(ERROR) << "apply_policies failed";
        return false;
    }

    return true;
}

bool TrafficSeparationTask::restore_exact_ports_from_db()
{
    auto db = AgentDB::get();

    bool success = true;
    std::unordered_set<std::string> restored_fh_ifaces;
    std::unordered_set<std::string> restored_wds_ifaces;

    for (const auto *radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }

        for (const auto &bss : radio->front.bssids) {
            if (!bss.enabled || !bss.fronthaul_bss || bss.backhaul_bss || bss.iface_name.empty()) {
                continue;
            }

            if (!restored_fh_ifaces.emplace(bss.iface_name).second) {
                continue;
            }

            if (!handle_new_fh_iface(bss.iface_name)) {
                success = false;
            }
        }

        for (const auto &client_kv : radio->associated_clients) {
            const auto &client = client_kv.second;
            if (client.wds_iface_name.empty()) {
                continue;
            }

            if (!restored_wds_ifaces.emplace(client.wds_iface_name).second) {
                continue;
            }

            if (!handle_new_wds_iface(client.wds_iface_name)) {
                success = false;
            }
        }

        for (const auto &bss : radio->front.bssids) {
            if (!bss.enabled || !bss.backhaul_bss) {
                continue;
            }

            const bool has_associated_client = std::any_of(
                radio->associated_clients.begin(), radio->associated_clients.end(),
                [&](const auto &client_kv) { return client_kv.second.bssid == bss.mac; });
            if (!has_associated_client) {
                continue;
            }

            std::string bss_iface = bss.iface_name;
            if (bss_iface.empty() &&
                !net::network_utils::linux_iface_get_name(bss.mac, bss_iface)) {
                continue;
            }

            for (const auto &wds_iface :
                 net::network_utils::get_bss_ifaces(bss_iface, db->bridge.iface_name)) {
                if (wds_iface == bss_iface) {
                    continue;
                }

                if (!restored_wds_ifaces.emplace(wds_iface).second) {
                    continue;
                }

                if (!handle_new_wds_iface(wds_iface)) {
                    success = false;
                }
            }
        }
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

bool TrafficSeparationTask::fill_wds_trunk_from_db(const std::string &iface_name,
                                                   net::sTrunkPort &trunk) const
{
    if (iface_name.empty()) {
        LOG(ERROR) << "empty iface_name";
        return false;
    }

    auto db           = AgentDB::get();
    const auto policy = db->device_conf.unsupported_profile_disallow_policy;

    const auto fill_trunk = [&](const auto &bss) {
        trunk             = {};
        trunk.iface_name  = iface_name;
        trunk.is_ethernet = false;
        // TODO(PPM-3941): WDS trunk untagged mode should primary consider downstream
        // agent multi-ap profile that it's reported via Assoc Req
        trunk.is_untagged_mode =
            net::is_untagged_mode(bss.backhaul_bss_disallow_profile1_agent_association,
                                  bss.backhaul_bss_disallow_profile2_agent_association, policy);
        return true;
    };

    for (const auto *radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }

        for (const auto &client_kv : radio->associated_clients) {
            const auto &client = client_kv.second;
            if (client.wds_iface_name != iface_name) {
                continue;
            }

            const auto bss_it = std::find_if(
                radio->front.bssids.begin(), radio->front.bssids.end(), [&](const auto &bss) {
                    return bss.enabled && bss.backhaul_bss && (bss.mac == client.bssid);
                });
            if (bss_it == radio->front.bssids.end()) {
                continue;
            }

            return fill_trunk(*bss_it);
        }
    }

    for (const auto *radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }

        for (const auto &bss : radio->front.bssids) {
            if (!bss.enabled || !bss.backhaul_bss) {
                continue;
            }

            const bool has_associated_client = std::any_of(
                radio->associated_clients.begin(), radio->associated_clients.end(),
                [&](const auto &client_kv) { return client_kv.second.bssid == bss.mac; });
            if (!has_associated_client) {
                continue;
            }

            std::string bss_iface = bss.iface_name;
            if (bss_iface.empty() &&
                !net::network_utils::linux_iface_get_name(bss.mac, bss_iface)) {
                continue;
            }

            for (const auto &bss_iface_netdev :
                 net::network_utils::get_bss_ifaces(bss_iface, db->bridge.iface_name)) {
                if (bss_iface_netdev == bss_iface) {
                    continue;
                }

                if (bss_iface_netdev != iface_name) {
                    continue;
                }

                LOG(DEBUG) << "Recovered WDS trunk from BH BSS iface scan iface=" << iface_name
                           << ", bssid=" << bss.mac;
                return fill_trunk(bss);
            }
        }
    }

    return false;
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

    net::sTrunkPort trunk{};
    if (!fill_wds_trunk_from_db(iface_name, trunk)) {
        LOG(DEBUG) << "Ignoring WDS iface without matching associated client/backhaul BSS: "
                   << iface_name;
        return true;
    }

    const auto now  = std::chrono::steady_clock::now();
    auto pending_it = m_pending_wds_ifaces.find(iface_name);
    if (pending_it == m_pending_wds_ifaces.end()) {
        const auto settle_due = now + std::chrono::milliseconds(WDS_SETTLE_MS);
        const auto timeout_at = now + std::chrono::milliseconds(WDS_RETRY_TIMEOUT_MS);

        LOG(DEBUG) << "Deferring WDS TS apply for settle window: " << iface_name;
        request_wds_retry(iface_name, settle_due, timeout_at);
        return true;
    }

    if (now < pending_it->second.not_before) {
        run_at(pending_it->second.not_before);
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

        auto retry_due = now + std::chrono::milliseconds(WDS_RETRY_INTERVAL_MS);
        if (retry_due > pending_it->second.deadline) {
            retry_due = pending_it->second.deadline;
        }
        run_at(retry_due);
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

bool TrafficSeparationTask::get_ports_from_db(std::vector<net::sTrunkPort> &trunks) const
{
    trunks.clear();

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

    return true;
}

} // namespace beerocks
