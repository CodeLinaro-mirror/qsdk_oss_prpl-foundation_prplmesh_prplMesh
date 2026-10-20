/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_db.h"

#include <bpl/bpl_cfg.h>
#include <easylogging++.h>

namespace beerocks {

thread_local unsigned AgentDB::s_lock_depth = 0;
thread_local std::shared_ptr<std::atomic<bool>> AgentDB::s_publication_ready;

AgentDB::SafeDB AgentDB::get()
{
    // Own the singleton in agentdb so shared-library consumers use the same DB.
    static AgentDB instance;
    return SafeDB(instance);
}

void AgentDB::db_lock()
{
    m_db_mutex.lock();
    ++s_lock_depth;
}

void AgentDB::db_unlock()
{
    const bool outermost = --s_lock_depth == 0;
    auto ready           = outermost ? std::move(s_publication_ready) : nullptr;
    m_db_mutex.unlock();
    if (ready) {
        // Publish only after ALL enclosing recursive DB scopes have released.
        // Readiness preserves FIFO order even if another publisher runs before
        // this thread reaches drain(). No queued command borrows database data.
        ready->store(true);
        AgentDataModel::get().drain();
    }
}

AgentDB::sRadio *AgentDB::radio(const std::string &iface_name)
{
    if (iface_name.empty()) {
        LOG(ERROR) << "Given radio iface name is empty";
        return nullptr;
    }

    auto radio_it = std::find_if(m_radios.begin(), m_radios.end(), [&](const sRadio &radio_entry) {
        return iface_name == radio_entry.front.iface_name ||
               iface_name == radio_entry.back.iface_name;
    });

    return radio_it == m_radios.end() ? nullptr : &(*radio_it);
}

AgentDB::sRadio *AgentDB::add_radio(const std::string &front_iface_name,
                                    const std::string &back_iface_name)
{
    if (front_iface_name.empty() && back_iface_name.empty()) {
        LOG(ERROR) << "Both front and back interface names are empty!";
        return nullptr;
    }

    if (!front_iface_name.empty() && radio(front_iface_name)) {
        LOG(DEBUG) << "Radio entry of front iface " << front_iface_name
                   << " already exists. Not adding.";
        return nullptr;
    }

    if (!back_iface_name.empty() && radio(back_iface_name)) {
        LOG(DEBUG) << "Radio entry of back iface " << back_iface_name
                   << " already exists. Not adding.";
        return nullptr;
    }

    m_radios.emplace_back(front_iface_name, back_iface_name);
    m_radios_list.push_back(&m_radios.back());

    return &m_radios.back();
}

void AgentDB::remove_radio_from_radios_list(const std::string &iface_name)
{
    if (iface_name.empty()) {
        LOG(ERROR) << "Given interface name is empty";
        return;
    }
    for (auto radio_it = m_radios_list.begin(); radio_it != m_radios_list.end();) {
        if ((*radio_it)->front.iface_name == iface_name ||
            (*radio_it)->back.iface_name == iface_name) {
            radio_it = m_radios_list.erase(radio_it);
            return;
        }
        radio_it++;
    }
    return;
}

AgentDB::sRadio *AgentDB::get_radio_by_mac(const sMacAddr &mac, eMacType mac_type_hint)
{
    bool all_mac_types = mac_type_hint == eMacType::ALL;
    auto radio_it = std::find_if(m_radios.begin(), m_radios.end(), [&](const sRadio &radio_entry) {
        if (all_mac_types || mac_type_hint == eMacType::RADIO) {
            if (radio_entry.front.iface_mac == mac || radio_entry.back.iface_mac == mac) {
                return true;
            }
        }
        if (all_mac_types || mac_type_hint == eMacType::BSSID) {
            auto &bssid_list = radio_entry.front.bssids;
            auto bssid_it =
                std::find_if(bssid_list.begin(), bssid_list.end(),
                             [&](const sRadio::sFront::sBssid &bssid) { return bssid.mac == mac; });
            if (bssid_it != bssid_list.end()) {
                return true;
            }
        }
        if (all_mac_types || mac_type_hint == eMacType::CLIENT) {
            auto client_it = radio_entry.associated_clients.find(mac);
            return client_it != radio_entry.associated_clients.end();
        }
        // MAC is not one of the front\back radio MACs nor bssid MAC.
        return false;
    });

    return radio_it == m_radios.end() ? nullptr : &(*radio_it);
}

void AgentDB::erase_client(const sMacAddr &client_mac, sMacAddr bssid)
{

    if (bssid != net::network_utils::ZERO_MAC) {
        // Remove legacy client from specific radio with given BSSID
        auto radio = get_radio_by_mac(bssid, eMacType::BSSID);
        if (!radio) {
            LOG(WARNING) << "Radio not found with bssid:" << bssid;
            return;
        }

        radio->associated_clients.erase(client_mac);
        LOG(DEBUG) << "Removed client " << client_mac << " from radio with BSSID " << bssid;
    } else {
        // Remove client from all radios
        for (auto &radio : m_radios) {
            radio.associated_clients.erase(client_mac);
        }
    }

    // Always remove MLO client entry if present independent from bssid argument
    auto mld_it = associated_sta_mlds.find(client_mac);
    if (mld_it != associated_sta_mlds.end()) {
        LOG(DEBUG) << "Removing MLO client from associated_sta_mlds: STA MLD=" << client_mac
                   << ", AP MLD=" << mld_it->second.mld_config.ap_mld_mac;
        associated_sta_mlds.erase(mld_it);
    }
}

bool AgentDB::get_mac_by_ssid(const sMacAddr &ruid, const std::string &ssid, sMacAddr &value)
{
    value      = net::network_utils::ZERO_MAC;
    auto radio = get_radio_by_mac(ruid, AgentDB::eMacType::RADIO);
    if (!radio) {
        LOG(ERROR) << "No radio with ruid '" << ruid << "' found!";
        return false;
    }

    for (const auto &bssid : radio->front.bssids) {
        if (bssid.ssid == ssid) {
            value = bssid.mac;
            return true;
        }
    }
    return false;
}

bool AgentDB::get_ap_mld_mac_by_ssid(const std::string &ssid, sMacAddr &value)
{
    value = net::network_utils::ZERO_MAC;
    for (auto &ap_mld_conf : ap_mld_configurations) {
        if (ap_mld_conf.mld_config.mld_ssid == ssid) {
            value = ap_mld_conf.mld_config.mld_mac;
            return true;
        }
    }
    return false;
}

bool AgentDB::get_bsta_mld_mac_by_ssid(const std::string &ssid, sMacAddr &ruid, sMacAddr &value)
{
    value = net::network_utils::ZERO_MAC;
    if (bsta_mld_configuration) {
        if (bsta_mld_configuration->mld_config.mld_ssid == ssid) {

            // Return bSTA MLD MAC
            if (ruid == net::network_utils::ZERO_MAC) {
                value = bsta_mld_configuration->mld_config.mld_mac;
                return true;
            }

            // Search Affiliated MAC by RUID (map key)
            auto it = bsta_mld_configuration->affiliated_bstas.find(ruid);
            if (it == bsta_mld_configuration->affiliated_bstas.end()) {
                LOG(ERROR) << "RUID: " << ruid << "not found in configuration";
                return false;
            }
            value = it->second.mac_addr;
            if (value == net::network_utils::ZERO_MAC) {
                value = bsta_mld_configuration->mld_config.mld_mac;
                LOG(INFO) << "Affiliated MAC is zero, fallback to bSTA MLD MAC ruid: " << ruid;
            }
            return true;
        }
    }
    return false;
}

bool AgentDB::agent_is_dummy() const
{
    return device_conf.management_mode == BPL_MGMT_MODE_MULTIAP_CONTROLLER ||
           device_conf.management_mode == BPL_MGMT_MODE_NOT_MULTIAP;
}

} // namespace beerocks
