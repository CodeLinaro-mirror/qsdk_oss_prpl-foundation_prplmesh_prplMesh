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

    if (!clear_configuration()) {
        LOG(ERROR) << "clear_configuration failed";
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
        schedule_apply(eApplyMode::Full);
        break;
    }
    case TS_NEW_BH_STA_IFACE: {
        schedule_apply(eApplyMode::AddNewSta);
        break;
    }
    case TS_CLEAR: {
        if (!cleanup_ts_runtime_state()) {
            LOG(ERROR) << "cleanup_ts_runtime_state failed";
        }

        m_pending  = false;
        m_mode     = eApplyMode::None;
        m_next_run = m_next_run.min();

        break;
    }
    default:
        break;
    }
}

TrafficSeparationTask::eApplyMode TrafficSeparationTask::stronger_mode(eApplyMode a, eApplyMode b)
{
    return (static_cast<uint8_t>(a) >= static_cast<uint8_t>(b)) ? a : b;
}

void TrafficSeparationTask::schedule_apply(eApplyMode mode)
{
    m_mode    = stronger_mode(m_mode, mode);
    m_pending = true;

    const auto now = std::chrono::steady_clock::now();
    const auto due = now + std::chrono::milliseconds(DEBOUNCE_MS);

    if (m_next_run <= now) {
        m_next_run = due;
    }
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

    m_pending  = false;
    m_next_run = m_next_run.min();

    const auto mode = m_mode;
    m_mode          = eApplyMode::None;

    switch (mode) {
    case eApplyMode::Full: {
        if (!reset()) {
            LOG(WARNING) << "full reset failed";
        }
        break;
    }
    case eApplyMode::AddNewSta: {
        if (!handle_new_sta_iface()) {
            LOG(WARNING) << "add new sta failed";
        }
        break;
    }
    default:
        break;
    }
}

bool TrafficSeparationTask::clear_configuration()
{
    if (!m_mgr) {
        return true;
    }
    return m_mgr->reset();
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

    // If we are R1 device, we don't do TS
    if (db->device_conf.multi_ap_profile ==
        wfa_map::tlvProfile2MultiApProfile::eMultiApProfile::MULTIAP_PROFILE_1) {
        LOG(DEBUG)
            << "Multi-AP profile=1, Traffic Separation is disabled -> clearing and returning";

        if (!cleanup_ts_runtime_state()) {
            LOG(ERROR) << "cleanup_ts_runtime_state failed (Profile-1 mode)";
            return false;
        }
        return true;
    }

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
    if (!collect_ports_from_db(trunks, access_ports)) {
        LOG(ERROR) << "collect_ports_from_db failed";
        return false;
    }

    for (const auto &t : trunks) {
        if (!m_mgr->add_trunk_port(t)) {
            LOG(ERROR) << "add_trunk_port failed iface=" << t.iface_name;
            if (!m_mgr->reset()) {
                LOG(ERROR) << "manager reset failed after trunk-add failure";
            }
            return false;
        }
    }

    for (const auto &a : access_ports) {
        if (!m_mgr->add_access_port(a)) {
            LOG(WARNING) << "add_access_port failed iface=" << a.iface_name;
        }
    }

    if (!m_mgr->apply_policies()) {
        LOG(ERROR) << "apply_policies failed";
        return false;
    }

    return true;
}

bool TrafficSeparationTask::handle_new_sta_iface()
{
    auto db = AgentDB::get();

    // If we are R1 device, we don't apply TS to the newly created iface
    if (db->device_conf.multi_ap_profile ==
        wfa_map::tlvProfile2MultiApProfile::eMultiApProfile::MULTIAP_PROFILE_1) {
        LOG(DEBUG) << "Multi-AP profile=1, skip TS on new STA iface";
        return true;
    }

    if (db->traffic_separation.primary_vlan_id == 0 ||
        db->traffic_separation.primary_vlan_id > net::MAX_VLAN_ID) {
        LOG(DEBUG) << "primary_vlan_id is out of range [" << net::MIN_VLAN_ID << "-"
                   << net::MAX_VLAN_ID << "], nothing to do";
        return true;
    }

    // If we have no manager, do full reset
    if (!m_mgr) {
        LOG(DEBUG) << "No TS manager, doing full reset";
        return reset();
    }

    if (!ensure_transport_primary_vlan(db->traffic_separation.primary_vlan_id)) {
        LOG(ERROR) << "failed to set transport primary_vlan_id="
                   << db->traffic_separation.primary_vlan_id;
        return false;
    }

    // Scan ALL backhaul BSS and re-add all sta ifaces.
    // Manager is idempotent => OK to call add_trunk_port() repeatedly.
    bool success = true;
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

            // Equal bBSS profile-disallow flags are unsupported unless override policy is enabled.
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
                net::sTrunkPort t{};
                t.iface_name       = sta;
                t.is_ethernet      = false;
                t.is_untagged_mode = untagged_mode;

                if (!m_mgr->add_trunk_port(t)) {
                    LOG(ERROR) << "add_trunk_port failed iface=" << sta;
                    success = false;
                    // Continue to try other ifaces and report aggregate status.
                }
            }
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

    net::sTrunkPort t{};

    switch (con_type) {
    case AgentDB::sBackhaul::eConnectionType::Wireless: {
        if (wifi_iface.empty()) {
            return true;
        }
        t.iface_name  = wifi_iface;
        t.is_ethernet = false;

        const uint8_t local = static_cast<uint8_t>(db->device_conf.multi_ap_profile);
        const uint8_t peer  = static_cast<uint8_t>(db->backhaul.backhaul_bss_multi_ap_profile);
        const uint8_t eff   = std::min(local, peer);

        t.is_untagged_mode = (eff <= 1);
        break;
    }
    case AgentDB::sBackhaul::eConnectionType::Wired: {
        if (eth_iface.empty()) {
            return true;
        }
        t.iface_name       = eth_iface;
        t.is_ethernet      = true;
        t.is_untagged_mode = false;
        break;
    }
    default:
        LOG(ERROR) << "unknown backhaul connection type";
        return false;
    }

    trunks.push_back(t);
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
        net::sTrunkPort t{};
        t.iface_name       = lan.iface_name;
        t.is_ethernet      = true;
        t.is_untagged_mode = false;
        trunks.push_back(t);
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
                    net::sAccessPort ap{};
                    ap.iface_name = bss.iface_name;
                    access_ports.push_back(ap);
                }
            }

            if (bss.backhaul_bss && !bss.iface_name.empty()) {
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

                // Equal bBSS profile-disallow flags are unsupported unless override policy is enabled.
                if (disallow_profile1 == disallow_profile2) {
                    if (policy == eUnsupportedProfileDisallowPolicy::NO_OVERRIDE) {
                        LOG(WARNING)
                            << "Unsupported bBSS profile-disallow configuration for iface="
                            << bss.iface_name << " (profile1_disallow=" << disallow_profile1
                            << ", profile2_disallow=" << disallow_profile2
                            << ", policy=" << unsupported_profile_disallow_policy_to_string(policy)
                            << "), defaulting to tagged mode";
                    } else {
                        LOG(INFO) << "Overriding unsupported bBSS profile-disallow configuration "
                                     "for iface="
                                  << bss.iface_name << " (profile1_disallow=" << disallow_profile1
                                  << ", profile2_disallow=" << disallow_profile2 << ", policy="
                                  << unsupported_profile_disallow_policy_to_string(policy)
                                  << ", resolved_mode=" << (untagged_mode ? "untagged" : "tagged")
                                  << ")";
                    }
                }

                const auto sta_ifaces = get_all_sta_ifaces_for_bss(bss.iface_name);
                for (const auto &sta : sta_ifaces) {
                    net::sTrunkPort t{};
                    t.iface_name       = sta;
                    t.is_ethernet      = false;
                    t.is_untagged_mode = untagged_mode;
                    trunks.push_back(t);
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
