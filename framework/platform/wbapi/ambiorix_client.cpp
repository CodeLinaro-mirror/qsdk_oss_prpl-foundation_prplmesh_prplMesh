/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "ambiorix_client.h"

#include "ambiorix_connection_manager.h"

#include <mapf/common/amx_mutex.h>

namespace beerocks {
namespace wbapi {

bool AmbiorixClient::connect(const std::string &amxb_backend, const std::string &bus_uri)
{
    AmxGuard guard;
    m_connection = AmbiorixConnectionManager::get_instance()->get_connection(amxb_backend, bus_uri);
    if (!m_connection) {
        LOG(ERROR) << "Failed to connect to the " << bus_uri.c_str() << " bus";
        return false;
    }

    return true;
}

AmbiorixVariantSmartPtr AmbiorixClient::get_object(const std::string &object_path,
                                                   const int32_t depth)
{
    AmxGuard guard;
    if (!m_connection) {
        LOG(ERROR) << "Client is not connected to bus";
        return AmbiorixVariantSmartPtr{};
    }
    return m_connection->get_object(object_path, depth, true);
}

AmbiorixVariantSmartPtr AmbiorixClient::get_param(const std::string &obj_path,
                                                  const std::string &param_name)
{
    AmxGuard guard;
    if (!m_connection) {
        LOG(ERROR) << "Client is not connected to bus";
        return AmbiorixVariantSmartPtr{};
    }
    return m_connection->get_param(obj_path, param_name);
}

bool AmbiorixClient::resolve_path_multi(const std::string &search_path,
                                        std::vector<std::string> &absolute_path_list)
{
    AmxGuard guard;
    if (!m_connection) {
        LOG(ERROR) << "Client is not connected to bus";
        return false;
    }

    int amxb_status = AMXB_STATUS_OK;
    auto resolved   = m_connection->resolve_path(search_path, absolute_path_list, amxb_status);
    LOG_IF(amxb_status != AMXB_STATUS_OK, ERROR)
        << "amxb_resolve [" << search_path << "] failed, ret=" << amxb_status;
    return resolved;
}

bool AmbiorixClient::resolve_path(const std::string &search_path, std::string &absolute_path)
{
    AmxGuard guard;
    if (search_path.empty() || search_path == ".") {
        LOG(WARNING) << "resolve_path called with empty/dot search_path, absolute_path = "
                     << absolute_path;
        absolute_path.clear();
        return false;
    }

    if (!m_connection) {
        LOG(ERROR) << "Client is not connected to bus";
        absolute_path.clear();
        return false;
    }

    std::vector<std::string> absolute_path_list;
    int amxb_status = AMXB_STATUS_OK;
    if (m_connection->resolve_path(search_path, absolute_path_list, amxb_status) &&
        !absolute_path_list.empty()) {
        absolute_path = absolute_path_list[0];
        return true;
    }

    // Non-zero amxb_resolve() errors are backend specific, so log them raw.
    // Zero means the bus answered but nothing matched.
    LOG(ERROR) << "AmbiorixClient::resolve_path failed to resolve path: " << search_path
               << ", amxb_resolve ret=" << amxb_status
               << ((amxb_status == AMXB_STATUS_OK) ? " (no match)" : " (bus error)");
    absolute_path.clear();
    return false;
}

bool AmbiorixClient::update_object(const std::string &object_path, AmbiorixVariant &object_data)
{
    AmxGuard guard;
    return (m_connection && m_connection->update_object(object_path, object_data));
}

bool AmbiorixClient::add_instance(const std::string &object_path, AmbiorixVariant &object_data,
                                  int &instance_id)
{
    AmxGuard guard;
    return (m_connection && m_connection->add_instance(object_path, object_data, instance_id));
}

bool AmbiorixClient::remove_instance(const std::string &object_path, int instance_id)
{
    AmxGuard guard;
    return (m_connection && m_connection->remove_instance(object_path, instance_id));
}

bool AmbiorixClient::call(const std::string &object_path, const char *method, AmbiorixVariant &args,
                          AmbiorixVariant &result)
{
    AmxGuard guard;
    return (m_connection && m_connection->call(object_path, method, args, result));
}

bool AmbiorixClient::call_async(const std::string &object_path, const char *method,
                                AmbiorixVariant &args)
{
    AmxGuard guard;
    return (m_connection && m_connection->call_async(object_path, method, args));
}

int AmbiorixClient::get_fd()
{
    AmxGuard guard;
    return (m_connection ? m_connection->get_fd() : -1);
}

int AmbiorixClient::get_signal_fd()
{
    AmxGuard guard;
    return (m_connection ? m_connection->get_signal_fd() : -1);
}

int AmbiorixClient::read()
{
    AmxGuard guard;
    return (m_connection ? m_connection->read() : -1);
}

int AmbiorixClient::read_signal()
{
    AmxGuard guard;
    return (m_connection ? m_connection->read_signal() : -1);
}

bool AmbiorixClient::init_event_loop(std::shared_ptr<EventLoop> event_loop)
{
    AmxGuard guard;
    LOG(DEBUG) << "Register event handlers for the Ambiorix fd in the event loop.";

    auto ambiorix_fd = get_fd();
    if (ambiorix_fd < 0) {
        LOG(ERROR) << "Failed to get ambiorix file descriptor.";
        return false;
    }

    EventLoop::EventHandlers handlers = {
        .name = "ambiorix_events",
        .on_read =
            [&](int fd, EventLoop &loop) {
                auto &cnx = AmbiorixConnectionManager::get_instance()->fetch_connection(fd);
                if (cnx) {
                    cnx->read();
                }
                return true;
            },

        // Not implemented
        .on_write      = nullptr,
        .on_disconnect = nullptr,

        // Handle interface errors
        .on_error =
            [&](int fd, EventLoop &loop) {
                LOG(ERROR) << "Error on ambiorix fd.";
                return true;
            },
    };

    if (event_loop->remove_handlers(ambiorix_fd)) {
        LOG(WARNING) << "Replacing old handlers for the Amx fd " << ambiorix_fd;
    }
    if (!event_loop->register_handlers(ambiorix_fd, handlers)) {
        LOG(ERROR) << "Couldn't register event handlers for the Ambiorix fd in the event loop.";
        return false;
    }

    LOG(DEBUG) << "Event handlers for the Ambiorix fd: " << ambiorix_fd
               << " successfully registered in the event loop.";

    return true;
}

bool AmbiorixClient::init_signal_loop(std::shared_ptr<EventLoop> event_loop)
{
    AmxGuard guard;
    LOG(DEBUG) << "Register event handlers for the Ambiorix signals fd in the event loop.";

    auto ambiorix_fd = get_signal_fd();
    if (ambiorix_fd < 0) {
        LOG(ERROR) << "Failed to get ambiorix file descriptor.";
        return false;
    }

    EventLoop::EventHandlers handlers = {
        .name = "ambiorix_signal",
        .on_read =
            [&](int fd, EventLoop &loop) {
                auto &cnx = AmbiorixConnectionManager::get_instance()->fetch_connection(fd);
                if (cnx) {
                    cnx->read_signal();
                }
                return true;
            },
        // Not implemented
        .on_write      = nullptr,
        .on_disconnect = nullptr,

        // Handle interface errors
        .on_error =
            [&](int fd, EventLoop &loop) {
                LOG(ERROR) << "Error on ambiorix fd.";
                return true;
            },
    };

    if (event_loop->remove_handlers(ambiorix_fd)) {
        LOG(WARNING) << "Replacing old handlers for the Amx sig fd " << ambiorix_fd;
    }
    if (!event_loop->register_handlers(ambiorix_fd, handlers)) {
        LOG(ERROR) << "Couldn't register event handlers for the Ambiorix signals in the "
                      "event loop.";
        return false;
    }

    LOG(DEBUG) << "Event handlers for the Ambiorix signals fd: " << ambiorix_fd
               << " successfully registered in the event loop.";

    return true;
}

bool AmbiorixClient::remove_event_loop(std::shared_ptr<EventLoop> event_loop)
{
    AmxGuard guard;
    LOG(DEBUG) << "Remove event handlers for Ambiorix fd from the event loop.";

    auto ambiorix_fd = get_fd();
    if (ambiorix_fd < 0) {
        LOG(ERROR) << "Failed to get ambiorix file descriptor.";
        return false;
    }

    if (!event_loop->remove_handlers(ambiorix_fd)) {
        LOG(ERROR) << "Couldn't remove event handlers for the Ambiorix fd from the event loop.";
        return false;
    }

    LOG(DEBUG) << "Event handlers for the Ambiorix fd successfully removed from the event loop.";

    return true;
}

bool AmbiorixClient::remove_signal_loop(std::shared_ptr<EventLoop> event_loop)
{
    AmxGuard guard;
    LOG(DEBUG) << "Remove event handlers for the Ambiorix signals fd from the event loop.";

    auto ambiorix_fd = get_signal_fd();
    if (ambiorix_fd < 0) {
        LOG(ERROR) << "Failed to get ambiorix file descriptor.";
        return false;
    }

    if (!event_loop->remove_handlers(ambiorix_fd)) {
        LOG(ERROR) << "Couldn't remove event handlers for the Ambiorix signals fd from the "
                      "event loop.";
        return false;
    }

    LOG(DEBUG) << "The event handlers for the Ambiorix signals fd removed successfully from the "
                  "event loop.";

    return true;
}

bool AmbiorixClient::subscribe_to_object_event(
    const std::string &object_path, std::shared_ptr<sAmbiorixEventHandler> &event_handler,
    const std::string &filter)
{
    AmxGuard guard;
    if (!m_connection) {
        return false;
    }
    m_subscriptions.push_back({event_handler});
    if (!m_connection->subscribe(object_path, filter, m_subscriptions.back())) {
        LOG(ERROR) << "Subscribing to object events failed, path:" << object_path;
        m_subscriptions.pop_back();
        return false;
    }
    LOG(INFO) << "subscribe successfully to object events, path:" << object_path;
    return true;
}

bool AmbiorixClient::unsubscribe_from_object_event(
    std::shared_ptr<beerocks::wbapi::sAmbiorixEventHandler> &event_handler)
{
    AmxGuard guard;
    auto it = std::find_if(m_subscriptions.begin(), m_subscriptions.end(),
                           [&event_handler](const sAmbiorixSubscriptionInfo &subscription) {
                               return event_handler == subscription.handler;
                           }

    );
    if (it != m_subscriptions.end()) {
        m_connection->unsubscribe(*it);
        m_subscriptions.erase(it);
        return true;
    }
    return false;
}

AmbiorixClient::~AmbiorixClient()
{
    AmxGuard guard;
    while (!m_subscriptions.empty()) {
        m_connection->unsubscribe(m_subscriptions.back());
        m_subscriptions.pop_back();
    }
    // Release the connection while the guard still protects AMX teardown.
    m_connection.reset();
}

} // namespace wbapi
} // namespace beerocks
