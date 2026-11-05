/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_dm.h"

#include <bcl/beerocks_defines.h>
#include <easylogging++.h>
#include <mapf/common/amx_mutex.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace beerocks {

AgentDM::SafeDM AgentDM::get()
{
    // The datamodel can take the AMX mutex while AgentDM is destroyed at exit.
    // Construct the mutex first so that it is destroyed after AgentDM.
    (void)amx_mutex();
    static AgentDM instance;
    return SafeDM(instance);
}

void AgentDM::dm_lock()
{
    amx_mutex().lock();
    m_dm_mutex.lock();
}

void AgentDM::dm_unlock()
{
    m_dm_mutex.unlock();
    amx_mutex().unlock();
}

bool AgentDM::init_data_model(std::shared_ptr<nbapi::Ambiorix> dm)
{
    if (!dm) {
        LOG(ERROR) << "Ambiorix datamodel not specified";
        return false;
    }
    if (m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel already set";
        return false;
    }
    m_ambiorix_datamodel = std::move(dm);
    return true;
}

bool AgentDM::dm_set_agent_mac(const std::string &mac)
{
    if (!m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel not set";
        return false;
    }
    if (!m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "MACAddress", mac)) {
        LOG(ERROR) << "Failed to set Agent with mac: " << mac;
        return false;
    }
    return true;
}

bool AgentDM::dm_set_fronthaul_interfaces(const std::string &interfaces)
{
    if (!m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel not set";
        return false;
    }
    if (!m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "FronthaulIfaces", interfaces)) {
        LOG(ERROR) << "Failed to set FronthaulIfaces: " << interfaces;
        return false;
    }
    return true;
}

bool AgentDM::dm_set_management_mode(const std::string &mode)
{
    if (!m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel not set";
        return false;
    }
    if (!m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "ManagementMode", mode)) {
        LOG(ERROR) << "Failed to set ManagementMode: " << mode;
        return false;
    }
    return true;
}

bool AgentDM::dm_set_agent_state(const std::string &cur, const std::string &max)
{
    if (!m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel not set";
        return false;
    }
    const bool current_set = m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "CurrentState", cur);
    const bool best_set    = m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "BestState", max);
    if (!current_set || !best_set) {
        LOG(ERROR) << "Failed to set Agent state: " << cur << ", best state: " << max;
        return false;
    }
    return true;
}

bool AgentDM::dm_set_controller_connected(bool connected)
{
    if (!m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel not set";
        return false;
    }
    if (!m_ambiorix_datamodel->set(AGENT_ROOT_DM ".Info", "ControllerConnected", connected)) {
        LOG(ERROR) << "Failed to set ControllerConnected: " << connected;
        return false;
    }
    return true;
}

std::string AgentDM::dm_create_fronthaul_object(const std::string &iface)
{
    if (!m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel not set";
        return "";
    }
    auto idx = m_ambiorix_datamodel->get_instance_index(
        AGENT_ROOT_DM ".Info.Fronthaul.[Iface == '%s']", iface);

    if (idx && !m_ambiorix_datamodel->remove_instance(AGENT_ROOT_DM ".Info.Fronthaul", idx)) {
        LOG(ERROR) << "Failed to remove fronthaul instance for " << iface;
        return "";
    }
#ifndef ENABLE_NBAPI
    return "";
#else
    auto inst = m_ambiorix_datamodel->add_instance(AGENT_ROOT_DM ".Info.Fronthaul");
    // Preserve the single retry for transient instance-creation failures. See PPM-3286.
    if (inst.empty()) {
        LOG(ERROR) << "Could not create " AGENT_ROOT_DM ".Info.Fronthaul instance for '" << iface
                   << "', scheduling one retry in 100 ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        inst = m_ambiorix_datamodel->add_instance(AGENT_ROOT_DM ".Info.Fronthaul");
        if (inst.empty()) {
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

bool AgentDM::dm_set_fronthaul_state(const std::string &path, const std::string &cur,
                                     const std::string &max)
{
    if (path.empty()) {
        LOG(ERROR) << "dm_set_fronthaul_state called with empty path, skipping update";
        return false;
    }
    if (!m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel not set";
        return false;
    }
    const bool current_set = m_ambiorix_datamodel->set(path, "CurrentState", cur);
    const bool best_set    = m_ambiorix_datamodel->set(path, "BestState", max);
    if (!current_set || !best_set) {
        LOG(ERROR) << "Failed to set Fronthaul state at " << path << ": " << cur
                   << ", best state: " << max;
        return false;
    }
    return true;
}

bool AgentDM::dm_fronthaul_disconnected(const std::string &path)
{
    auto dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return false;
    }
    if (!m_ambiorix_datamodel) {
        LOG(ERROR) << "Ambiorix datamodel not set";
        return false;
    }
    auto idx = atoi(path.c_str() + dot_pos + 1);
    if (!m_ambiorix_datamodel->remove_instance(path.substr(0, dot_pos), idx)) {
        LOG(ERROR) << "Failed to remove disconnected Fronthaul instance " << path;
        return false;
    }
    return true;
}

} // namespace beerocks
