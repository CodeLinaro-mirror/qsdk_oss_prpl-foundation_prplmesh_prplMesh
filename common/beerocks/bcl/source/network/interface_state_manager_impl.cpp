/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/network/interface_state_manager_impl.h>

#include <easylogging++.h>

#include <utility>
#include <vector>

namespace beerocks {
namespace net {

InterfaceStateManagerImpl::InterfaceStateManagerImpl(
    std::unique_ptr<InterfaceStateMonitor> interface_state_monitor,
    std::unique_ptr<InterfaceStateReader> interface_state_reader)
    : m_interface_state_monitor(std::move(interface_state_monitor)),
      m_interface_state_reader(std::move(interface_state_reader))
{
    m_interface_state_monitor->set_handler([&](const std::string &iface_name, bool iface_state) {
        bool last_iface_state;
        if (get_state(iface_name, last_iface_state)) {
            if (last_iface_state == iface_state) {
                return;
            }
        }

        set_state(iface_name, iface_state);
        notify_state_changed(iface_name, iface_state);
    });
    m_interface_state_monitor->set_events_lost_handler([&]() { resync(); });
}

InterfaceStateManagerImpl::~InterfaceStateManagerImpl()
{
    m_interface_state_monitor->set_events_lost_handler(nullptr);
    m_interface_state_monitor->clear_handler();
}

bool InterfaceStateManagerImpl::read_state(const std::string &iface_name, bool &iface_state)
{
    if (get_state(iface_name, iface_state)) {
        return true;
    }

    if (!m_interface_state_reader->read_state(iface_name, iface_state)) {
        return false;
    }

    set_state(iface_name, iface_state);

    return true;
}

bool InterfaceStateManagerImpl::get_state(const std::string &iface_name, bool &iface_state)
{
    const auto &it = m_interface_states.find(iface_name);
    if (m_interface_states.end() == it) {
        return false;
    }

    iface_state = it->second;

    return true;
}

void InterfaceStateManagerImpl::set_state(const std::string &iface_name, bool iface_state)
{
    m_interface_states[iface_name] = iface_state;
}

void InterfaceStateManagerImpl::resync()
{
    LOG(INFO) << "Reading the state of " << m_interface_states.size() << " interfaces again";

    // Update the cache first and notify afterwards: handlers may call read_state(), which can
    // insert into the map while it is being iterated
    std::vector<std::pair<std::string, bool>> iface_states;
    for (auto &entry : m_interface_states) {
        bool iface_state = false;
        if (!m_interface_state_reader->read_state(entry.first, iface_state)) {
            // Most likely, the interface does not exist any more
            iface_state = false;
        }
        entry.second = iface_state;
        iface_states.emplace_back(entry.first, iface_state);
    }

    // An interface may have been deleted and created again while the events were lost, so an
    // interface that is up is notified down first: the handler must stop using the old interface
    // (e.g. close a socket bound to the old interface index) and then use the new one
    for (const auto &iface_state : iface_states) {
        notify_state_changed(iface_state.first, false);
        if (iface_state.second) {
            notify_state_changed(iface_state.first, true);
        }
    }
}

} // namespace net
} // namespace beerocks
