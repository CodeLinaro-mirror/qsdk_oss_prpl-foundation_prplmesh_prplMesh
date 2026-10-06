/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_datamodel.h"
#include "agent_db.h"

#include <bcl/beerocks_defines.h>
#include <easylogging++.h>
#include <mapf/common/amx_mutex.h>

#include <chrono>
#include <thread>

namespace beerocks {

AgentDataModel &AgentDataModel::get()
{
    // The mutex must outlive the adapter's shared datamodel during teardown.
    // Only construct it here: get() can be called while AgentDB is locked.
    (void)amx_mutex();
    static AgentDataModel instance;
    return instance;
}

bool AgentDataModel::init_data_model(std::shared_ptr<nbapi::Ambiorix> dm)
{
    LOG_IF(AgentDB::s_lock_depth, FATAL) << "Initialize the datamodel outside AgentDB scopes";
    AmxGuard amx_guard;
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    LOG_IF(!dm, FATAL) << "Ambiorix datamodel not specified";
    LOG_IF(m_datamodel, FATAL) << "Ambiorix datamodel already set";
    m_datamodel = std::move(dm);
    return true;
}

std::shared_ptr<nbapi::Ambiorix> AgentDataModel::datamodel()
{
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    LOG_IF(!m_datamodel, FATAL) << "Ambiorix datamodel not set";
    return m_datamodel;
}

void AgentDataModel::enqueue(Update update)
{
    if (AgentDB::s_lock_depth && !AgentDB::s_publication_ready) {
        AgentDB::s_publication_ready = std::make_shared<std::atomic<bool>>(false);
    }
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        LOG_IF(!m_datamodel, FATAL) << "Ambiorix datamodel not set";
        m_updates.push_back({AgentDB::s_publication_ready, m_datamodel, std::move(update)});
    }
    // An enclosing SafeDB can belong to a caller several functions up the stack.
    // Its outermost release makes the commands ready and drains them on this thread.
    if (!AgentDB::s_lock_depth) {
        drain();
    }
}

void AgentDataModel::drain()
{
    // Avoid taking AMX for normal DB-only operations. Release the queue mutex
    // before acquiring AMX so enqueue under AgentDB never waits on AMX.
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (m_updates.empty() || (m_updates.front().ready && !m_updates.front().ready->load())) {
            return;
        }
    }

    AmxGuard amx_guard;
    if (m_draining) {
        // A datamodel callback may recursively release a SafeDB. Let the outer
        // drain finish this command before publishing any updates from callbacks.
        return;
    }
    struct DrainScope {
        explicit DrainScope(bool &draining) : flag(draining) { flag = true; }
        ~DrainScope() { flag = false; }
        bool &flag;
    } drain_scope(m_draining);
    for (;;) {
        PendingUpdate update;
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if (m_updates.empty() ||
                (m_updates.front().ready && !m_updates.front().ready->load())) {
                break;
            }
            update = std::move(m_updates.front());
            m_updates.pop_front();
        }
        update.apply(update.datamodel);
    }
}

bool AgentDataModel::dm_set_agent_mac(const std::string &mac)
{
    LOG_IF(AgentDB::s_lock_depth, FATAL) << "Publish the Agent MAC outside AgentDB scopes";
    AmxGuard amx_guard;
    drain();
    if (!datamodel()->set(AGENT_ROOT_DM ".Info", "MACAddress", mac)) {
        LOG(ERROR) << "Failed to set Agent with mac: " << mac;
        return false;
    }
    return true;
}

void AgentDataModel::dm_set_fronthaul_interfaces(const std::string &interfaces)
{
    enqueue([interfaces](const std::shared_ptr<nbapi::Ambiorix> &dm) {
        if (!dm->set(AGENT_ROOT_DM ".Info", "FronthaulIfaces", interfaces)) {
            LOG(ERROR) << "Failed to publish FronthaulIfaces: " << interfaces;
        }
    });
}

void AgentDataModel::dm_set_agent_state(const std::string &cur, const std::string &max)
{
    enqueue([cur, max](const std::shared_ptr<nbapi::Ambiorix> &dm) {
        if (!dm->set_strings(AGENT_ROOT_DM ".Info", {{"CurrentState", cur}, {"BestState", max}})) {
            LOG(ERROR) << "Failed to publish Agent state: " << cur << ", best state: " << max;
        }
    });
}

void AgentDataModel::dm_set_controller_connected(bool connected)
{
    enqueue([connected](const std::shared_ptr<nbapi::Ambiorix> &dm) {
        if (!dm->set(AGENT_ROOT_DM ".Info", "ControllerConnected", connected)) {
            LOG(ERROR) << "Failed to publish ControllerConnected: " << connected;
        }
    });
}

void AgentDataModel::dm_set_management_mode(const std::string &mode)
{
    enqueue([mode](const std::shared_ptr<nbapi::Ambiorix> &dm) {
        if (!dm->set(AGENT_ROOT_DM ".Info", "ManagementMode", mode)) {
            LOG(ERROR) << "Failed to publish ManagementMode: " << mode;
        }
    });
}

std::string AgentDataModel::dm_create_fronthaul_object(const std::string &iface)
{
    LOG_IF(AgentDB::s_lock_depth, FATAL) << "Create Fronthaul instances outside AgentDB scopes";
    AmxGuard amx_guard;
    drain();
    auto dm  = datamodel();
    auto idx = dm->get_instance_index(AGENT_ROOT_DM ".Info.Fronthaul.[Iface == '%s']", iface);

    if (idx && !dm->remove_instance(AGENT_ROOT_DM ".Info.Fronthaul", idx)) {
        LOG(ERROR) << "Failed to remove fronthaul instance for " << iface;
        return "";
    }
#ifndef ENABLE_NBAPI
    return "";
#else
    auto inst = dm->add_instance(AGENT_ROOT_DM ".Info.Fronthaul");
    // Preserve the single retry for transient instance-creation failures. See PPM-3286.
    if (inst.empty()) {
        LOG(ERROR) << "Could not create " AGENT_ROOT_DM ".Info.Fronthaul instance for '" << iface
                   << "', scheduling one retry in 100 ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        inst = dm->add_instance(AGENT_ROOT_DM ".Info.Fronthaul");
        if (inst.empty()) {
            LOG(ERROR) << "Could not create " AGENT_ROOT_DM ".Info.Fronthaul instance for '"
                       << iface << " on retry";
            return "";
        }
        LOG(INFO) << "Successfully created Fronthaul instance for '" << iface << "' on retry";
    }

    // Initialize all three parameters atomically. Never return a path to an
    // instance whose initialization failed; remove it while AMX remains guarded.
    if (!dm->set_strings(
            inst, {{"Iface", iface}, {"CurrentState", "INIT (0)"}, {"BestState", "INIT (0)"}})) {
        LOG(ERROR) << "Failed to initialize Fronthaul instance for " << iface;
        const auto dot_pos = inst.find_last_of('.');
        if (dot_pos == std::string::npos ||
            !dm->remove_instance(inst.substr(0, dot_pos), atoi(inst.c_str() + dot_pos + 1))) {
            LOG(ERROR) << "Failed to remove uninitialized Fronthaul instance " << inst;
        }
        return "";
    }
    return inst;
#endif
}

void AgentDataModel::dm_set_fronthaul_state(const std::string &path, const std::string &cur,
                                            const std::string &max)
{
    if (path.empty()) {
        LOG(ERROR) << "dm_set_fronthaul_state called with empty path, skipping update";
        return;
    }
    enqueue([path, cur, max](const std::shared_ptr<nbapi::Ambiorix> &dm) {
        if (!dm->set_strings(path, {{"CurrentState", cur}, {"BestState", max}})) {
            LOG(ERROR) << "Failed to publish Fronthaul state at " << path << ": " << cur
                       << ", best state: " << max;
        }
    });
}

void AgentDataModel::dm_fronthaul_disconnected(const std::string &path)
{
    enqueue([path](const std::shared_ptr<nbapi::Ambiorix> &dm) {
        auto dot_pos = path.find_last_of('.');
        if (dot_pos == std::string::npos) {
            return;
        }
        auto idx = atoi(path.c_str() + dot_pos + 1);
        if (!dm->remove_instance(path.substr(0, dot_pos), idx)) {
            LOG(ERROR) << "Failed to remove disconnected Fronthaul instance " << path;
        }
    });
}

} // namespace beerocks
