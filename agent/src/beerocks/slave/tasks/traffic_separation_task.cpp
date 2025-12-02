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

namespace beerocks {

TrafficSeparationTask::TrafficSeparationTask(slave_thread &btl_ctx,
                                             ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::TRAFFIC_SEPARATION), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx),
      m_mgr(std::make_unique<net::TrafficSeparationManager>())
{
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
        (void)clear_configuration();
        (void)ensure_transport_primary_vlan(0);
        (void)reset_transport_monitoring_on_bridge();

        m_pending  = false;
        m_mode     = eApplyMode::None;
        m_next_run = std::chrono::steady_clock::time_point{};

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

    if (m_next_run.time_since_epoch().count() == 0 || m_next_run < now) {
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
    m_next_run = std::chrono::steady_clock::time_point{};

    const auto mode = m_mode;
    m_mode          = eApplyMode::None;

    switch (mode) {
    case eApplyMode::Full: {
        if (!reset()) {
            LOG(WARNING) << "TrafficSeparationTask: full reset failed";
        }
        break;
    }
    case eApplyMode::AddNewSta: {
        if (!handle_new_sta_iface()) {
            LOG(WARNING) << "TrafficSeparationTask: add new sta failed";
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
    if (primary_vid == m_last_primary_vid) {
        return true;
    }

    const bool add = (primary_vid != 0);
    if (!m_btl_ctx.m_broker_client->configure_primary_vlan_id(primary_vid, add)) {
        LOG(ERROR) << "TrafficSeparationTask: configure_primary_vlan_id failed vid=" << primary_vid;
        return false;
    }

    m_last_primary_vid = primary_vid;
    return true;
}

bool TrafficSeparationTask::reset_transport_monitoring_on_bridge()
{
    auto db = AgentDB::get();
    if (!m_btl_ctx.m_broker_client->configure_interfaces(db->bridge.iface_name, {}, true, true)) {
        LOG(ERROR) << "TrafficSeparationTask: configure_interfaces(bridge) failed";
        return false;
    }
    return true;
}

bool TrafficSeparationTask::build_ts_config(net::sTrafficSeparationConfig &cfg) const
{
    auto db = AgentDB::get();

    if (db->traffic_separation.primary_vlan_id == 0) {
        LOG(ERROR) << "TS config: primary vlan id is 0";
        return false;
    }

    if (!bpl::cfg_get_private_bridge_iface(cfg.private_bridge)) {
        cfg.private_bridge = bpl::DEFAULT_PRIVATE_BRIDGE_IFACE;
    }
    if (!bpl::cfg_get_guest_bridge_iface(cfg.guest_bridge)) {
        cfg.guest_bridge = bpl::DEFAULT_GUEST_BRIDGE_IFACE;
    }

    cfg.private_vid = db->traffic_separation.primary_vlan_id;

    if (!db->traffic_separation.secondary_vlans_ids.empty()) {
        cfg.guest_vid = *db->traffic_separation.secondary_vlans_ids.begin();
    } else {
        cfg.guest_vid = DEFAULT_GUEST_VLAN_ID;
        LOG(WARNING) << "TS config: no secondary vlan id, using default guest_vid="
                     << cfg.guest_vid;
    }

    return true;
}

bool TrafficSeparationTask::reset()
{
    auto db = AgentDB::get();

    const uint16_t primary_vid = db->traffic_separation.primary_vlan_id;
    if (primary_vid == 0) {
        LOG(DEBUG) << "TrafficSeparationTask::reset: primary_vlan_id=0, nothing to do";
        return true;
    }

    (void)ensure_transport_primary_vlan(primary_vid);

    if (!m_mgr) {
        m_mgr = std::make_unique<net::TrafficSeparationManager>();
    }

    if (!m_mgr->reset()) {
        LOG(ERROR) << "TrafficSeparationTask::reset: manager reset failed";
        return false;
    }

    net::sTrafficSeparationConfig cfg{};
    if (!build_ts_config(cfg)) {
        LOG(ERROR) << "TrafficSeparationTask::reset: failed to build config";
        return false;
    }

    if (!m_mgr->configure(cfg)) {
        LOG(ERROR) << "TrafficSeparationTask::reset: manager configure failed";
        return false;
    }

    std::vector<net::sTrunkPort> trunks;
    std::vector<net::sAccessPort> access_ports;
    if (!collect_ports_from_db(trunks, access_ports)) {
        LOG(ERROR) << "TrafficSeparationTask::reset: collect_ports_from_db failed";
        return false;
    }

    for (const auto &t : trunks) {
        if (!m_mgr->add_trunk_port(t)) {
            LOG(ERROR) << "TrafficSeparationTask::reset: add_trunk_port failed iface="
                       << t.iface_name;
            return false;
        }
    }

    for (const auto &a : access_ports) {
        if (!m_mgr->add_access_port(a)) {
            LOG(WARNING) << "TrafficSeparationTask::reset: add_access_port failed iface="
                         << a.iface_name;
        }
    }

    if (!m_mgr->apply_policies()) {
        LOG(ERROR) << "TrafficSeparationTask::reset: apply_policies failed";
        return false;
    }

    return true;
}

bool TrafficSeparationTask::handle_new_sta_iface()
{
    auto db = AgentDB::get();

    if (db->traffic_separation.primary_vlan_id == 0) {
        LOG(DEBUG) << "handle_new_sta_iface: primary_vlan_id=0, nothing to do";
        return true;
    }

    // If we have no manager, do full reset
    if (!m_mgr) {
        LOG(DEBUG) << "handle_new_sta_iface: No TS manager, doing full reset";
        return reset();
    }

    (void)ensure_transport_primary_vlan(db->traffic_separation.primary_vlan_id);

    // Scan ALL backhaul BSS and re-add all sta ifaces.
    // Manager is idempotent => OK to call add_trunk_port() repeatedly.
    for (const auto *radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }

        for (const auto &bss : radio->front.bssids) {
            if (!bss.active || !bss.backhaul_bss || bss.iface_name.empty()) {
                continue;
            }

            const bool untagged_mode =
                net::is_untagged_mode(bss.backhaul_bss_disallow_profile1_agent_association,
                                      bss.backhaul_bss_disallow_profile2_agent_association,
                                      db->device_conf.unsupported_profile_disallow_policy);

            const auto sta_ifaces = get_all_sta_ifaces_for_bss(bss.iface_name);

            for (const auto &sta : sta_ifaces) {
                net::sTrunkPort t{};
                t.iface_name       = sta;
                t.is_ethernet      = false;
                t.is_untagged_mode = untagged_mode;

                if (!m_mgr->add_trunk_port(t)) {
                    LOG(ERROR) << "TrafficSeparationTask: add_trunk_port failed iface=" << sta;
                    // Continue to try other ifaces (best effort)
                }
            }
        }
    }

    return true;
}

bool TrafficSeparationTask::collect_backhaul_bss_ifaces(
    std::vector<std::string> &out_bss_ifaces,
    std::unordered_map<std::string, bool> &out_untagged_mode) const
{
    out_bss_ifaces.clear();
    out_untagged_mode.clear();

    auto db = AgentDB::get();

    for (const auto *radio : db->get_radios_list()) {
        if (!radio) {
            continue;
        }
        for (const auto &bss : radio->front.bssids) {
            if (!bss.active) {
                continue;
            }
            if (!bss.backhaul_bss) {
                continue;
            }
            if (bss.iface_name.empty()) {
                continue;
            }

            const bool untagged =
                net::is_untagged_mode(bss.backhaul_bss_disallow_profile1_agent_association,
                                      bss.backhaul_bss_disallow_profile2_agent_association,
                                      db->device_conf.unsupported_profile_disallow_policy);

            out_bss_ifaces.push_back(bss.iface_name);
            out_untagged_mode[bss.iface_name] = untagged;
        }
    }

    std::sort(out_bss_ifaces.begin(), out_bss_ifaces.end());
    out_bss_ifaces.erase(std::unique(out_bss_ifaces.begin(), out_bss_ifaces.end()),
                         out_bss_ifaces.end());

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
        LOG(ERROR) << "TrafficSeparationTask: unknown backhaul connection type";
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

    (void)add_backhaul_connection_trunk(trunks);

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
                const bool untagged_mode =
                    net::is_untagged_mode(bss.backhaul_bss_disallow_profile1_agent_association,
                                          bss.backhaul_bss_disallow_profile2_agent_association,
                                          db->device_conf.unsupported_profile_disallow_policy);

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

    auto trunk_less = [](const net::sTrunkPort &a, const net::sTrunkPort &b) {
        return a.iface_name < b.iface_name;
    };
    auto trunk_eq = [](const net::sTrunkPort &a, const net::sTrunkPort &b) {
        return a.iface_name == b.iface_name;
    };

    std::sort(trunks.begin(), trunks.end(), trunk_less);
    trunks.erase(std::unique(trunks.begin(), trunks.end(), trunk_eq), trunks.end());

    auto access_less = [](const net::sAccessPort &a, const net::sAccessPort &b) {
        return a.iface_name < b.iface_name;
    };
    auto access_eq = [](const net::sAccessPort &a, const net::sAccessPort &b) {
        return a.iface_name == b.iface_name;
    };

    std::sort(access_ports.begin(), access_ports.end(), access_less);
    access_ports.erase(std::unique(access_ports.begin(), access_ports.end(), access_eq),
                       access_ports.end());

    return true;
}

int TrafficSeparationTask::sta_index_for_prefix(const std::string &ifname,
                                                const std::string &prefix)
{
    // "starts_with" equivalent / cppcheck performance issue
    if (ifname.size() < prefix.size() || ifname.compare(0, prefix.size(), prefix) != 0) {
        return -1;
    }

    const size_t digits_pos = prefix.size();
    if (digits_pos >= ifname.size()) {
        return -1;
    }

    int v = 0;
    for (size_t i = digits_pos; i < ifname.size(); ++i) {
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

    std::vector<std::pair<int, std::string>> matches;
    matches.reserve(all_ifaces.size());

    for (const auto &iface : all_ifaces) {
        const int idx = sta_index_for_prefix(iface, prefix);
        if (idx >= 0) {
            matches.emplace_back(idx, iface);
        }
    }

    std::sort(matches.begin(), matches.end(),
              [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b) {
                  if (a.first != b.first) {
                      return a.first < b.first;
                  }
                  return a.second < b.second; // deterministic
              });

    out.reserve(matches.size());
    for (auto &p : matches) {
        out.push_back(std::move(p.second));
    }

    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace beerocks
