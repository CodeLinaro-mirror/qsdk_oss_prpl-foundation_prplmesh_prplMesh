/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_db.h"

#include <easylogging++.h>

namespace beerocks {

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

    LOG(INFO) << "MMMMMMMM showing bsta MLD info";
    LOG(INFO) << "MMMMMMMM SSID: " << bsta_mld_configuration->mld_config.mld_ssid;
    LOG(INFO) << "MMMMMMMM MAC: " << bsta_mld_configuration->mld_config.mld_mac;
    LOG(INFO) << "MMMMMMMM mldunit: " << bsta_mld_configuration->mld_config.mld_unit;

    if ("Multi-AP-MLDC-2" == ssid) {
        sMacAddr mldMAC = {.oct = {0xe8, 0xc7, 0xcf, 0xb1, 0x35, 0x9f}};
        value =  mldMAC; //bsta_mld_configuration->mld_config.mld_mac;
        return true;
    }

    return false;
}

bool AgentDB::init_data_model(std::shared_ptr<beerocks::nbapi::Ambiorix> dm)
{
    LOG_IF(!dm, FATAL) << "Ambiorix datamodel not specified";
    LOG_IF(m_ambiorix_datamodel, FATAL) << "Ambiorix datamodel already set";

    m_ambiorix_datamodel = dm;
    return true;
}

bool AgentDB::dm_set_agent_mac(const std::string &mac)
{
    LOG_IF(!m_ambiorix_datamodel, FATAL) << "m_ambiorix_datamodel not set";

    // Set MACAddress, Data model path: AGENT_ROOT_DM.Info.MACAddress
    if (!m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "MACAddress", mac)) {
        LOG(ERROR) << "Failed to set Agent with mac: " << mac;
        return false;
    }
    return true;
}

void AgentDB::dm_set_fronthaul_interfaces(const std::string &interfaces)
{
    LOG_IF(!m_ambiorix_datamodel, FATAL) << "m_ambiorix_datamodel not set";

    m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "FronthaulIfaces", interfaces);
}

void AgentDB::dm_set_agent_state(const std::string &cur, const std::string &max)
{
    LOG_IF(!m_ambiorix_datamodel, FATAL) << "m_ambiorix_datamodel not set";

    m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "CurrentState", cur);
    m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "BestState", max);
}

void AgentDB::dm_set_management_mode(const std::string &mode)
{
    LOG_IF(!m_ambiorix_datamodel, FATAL) << "m_ambiorix_datamodel not set";

    m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "ManagementMode", mode);
}

std::string AgentDB::dm_create_fronthaul_object(const std::string &iface)
{
    auto idx = m_ambiorix_datamodel->get_instance_index(
        AGENT_ROOT_DM ".Info.Fronthaul.[Iface == '%s']", iface);

    if (idx) {
        if (!m_ambiorix_datamodel->remove_instance(AGENT_ROOT_DM ".Info.Fronthaul", idx)) {
            LOG(ERROR) << "Failed to remove fronthaul instance for " << iface;
            return "";
        }
    }
#ifndef ENABLE_NBAPI
    return "";
#else
    auto inst = m_ambiorix_datamodel->add_instance(AGENT_ROOT_DM ".Info.Fronthaul");
    // If this fails due to a race condition, schedule one retry. See PPM-3286.
    if (!inst.size()) {
        LOG(ERROR) << "Could not create " AGENT_ROOT_DM ".Info.Fronthaul instance for '" << iface
                   << "', scheduling one retry in 100 ms";

        // wait some time before second try
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        inst = m_ambiorix_datamodel->add_instance(AGENT_ROOT_DM ".Info.Fronthaul");
        if (!inst.size()) {
            LOG(ERROR) << "Could not create " AGENT_ROOT_DM ".Info.Fronthaul instance for '"
                       << iface << " on retry";
            return "";
        }
        LOG(INFO) << "Successfully created Fronthaul instance for '" << iface << "' on retry";
    }

    m_ambiorix_datamodel->set(inst, "Iface", iface);
    m_ambiorix_datamodel->set(inst, "CurrentState", std::string("INIT (0)"));
    m_ambiorix_datamodel->set(inst, "BestState", std::string("INIT (0)"));

    return inst;
#endif
}

void AgentDB::dm_set_fronthaul_state(const std::string &path, const std::string &cur,
                                     const std::string &max)
{
    if (path.empty()) {
        LOG(ERROR) << "dm_set_fronthaul_state called with empty path, skipping update";
        return;
    }
    m_ambiorix_datamodel->set(path, "CurrentState", cur);
    m_ambiorix_datamodel->set(path, "BestState", max);
}

void AgentDB::dm_fronthaul_disconnected(const std::string &path)
{
    auto dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return;
    }

    auto idx = atoi(path.c_str() + dot_pos + 1);
    m_ambiorix_datamodel->remove_instance(path.substr(0, dot_pos), idx);
}

} // namespace beerocks
