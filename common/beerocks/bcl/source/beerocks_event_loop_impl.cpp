/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_event_loop_impl.h>

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>

#include <easylogging++.h>

static std::string get_fd_name_print(std::string name)
{

    if (name.empty()) {
        return {};
    }
    return name.insert(0, " of '").append("'");
}

// Debug aid: log only slow EventLoop handlers to /tmp (avoids flood).
// Enable with: touch /tmp/beerocks_eloop_trace_enable
// Log file: /tmp/beerocks_event_loop_slow.log
static constexpr int64_t SLOW_HANDLER_MS           = 50;
static constexpr const char *ELOOP_TRACE_ENABLE    = "/tmp/beerocks_eloop_trace_enable";
static constexpr const char *ELOOP_TRACE_LOG       = "/tmp/beerocks_event_loop_slow.log";

static bool eloop_trace_enabled()
{
    return access(ELOOP_TRACE_ENABLE, F_OK) == 0;
}

static void eloop_trace_slow(const char *op, int fd, const std::string &name, uint32_t events,
                             int64_t duration_ms, int batch_n)
{
    if (duration_ms < SLOW_HANDLER_MS) {
        return;
    }
    if (!eloop_trace_enabled()) {
        return;
    }

    FILE *fp = fopen(ELOOP_TRACE_LOG, "a");
    if (!fp) {
        return;
    }

    using clock = std::chrono::system_clock;
    const auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
            .count();

    std::fprintf(fp,
                 "%lld SLOW op=%s fd=%d name=%s events=0x%x duration_ms=%lld batch=%d\n",
                 static_cast<long long>(now_ms), op, fd, name.empty() ? "-" : name.c_str(), events,
                 static_cast<long long>(duration_ms), batch_n);
    std::fclose(fp);
}

namespace beerocks {

//////////////////////////////////////////////////////////////////////////////
////////////////////////// Local module definitions //////////////////////////
//////////////////////////////////////////////////////////////////////////////

// Maximal number of events to process in a single epoll_wait call
static constexpr int MAX_POLL_EVENTS = 17;

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Implementation ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

EventLoopImpl::EventLoopImpl(std::chrono::milliseconds timeout) : m_timeout(timeout)
{
    m_epoll_fd = epoll_create1(0);
    LOG_IF(m_epoll_fd == -1, FATAL) << "Failed creating epoll: " << strerror(errno);
}

EventLoopImpl::~EventLoopImpl()
{
    // Delete all the file descriptors in the poll
    LOG(DEBUG) << "Removing " << m_fd_to_event_handlers.size() << " FDs from the event loop";

    while (!m_fd_to_event_handlers.empty()) {
        int fd = m_fd_to_event_handlers.begin()->first;
        EventLoopImpl::remove_handlers(fd);
    }

    // Close the poll fd
    LOG_IF(close(m_epoll_fd) == -1, ERROR)
        << "Failed closing epoll file descriptor: " << strerror(errno);
}

bool EventLoopImpl::register_handlers(int fd, const EventLoop::EventHandlers &handlers)
{
    if (-1 == fd) {
        LOG(ERROR) << "Invalid file descriptor!";
        return false;
    }

    // Make sure that the file descriptor is not already part of the poll
    if (m_fd_to_event_handlers.find(fd) != m_fd_to_event_handlers.end()) {
        LOG(WARNING) << "Requested to add FD (" << fd << ") to the poll, but it's already there";
        return false;
    }

    LOG(INFO) << "Register handlers for FD (" << fd << ")" << get_fd_name_print(handlers.name);

    // Helper lambda function for adding a fd to the poll, and register for the following
    // events:
    // EPOLLIN: The associated fd is available for read operations.
    // EPOLLOUT: The associated fd is available for write operations.
    // EPOLLRDHUP: Socket peer closed connection, or shut down writing half of connection.
    // EPOLLERR: Error condition happened on the associated fd.
    // EPOLLHUP: Hang up happened on the associated fd.
    auto add_fd_to_epoll = [&](int fd) -> bool {
        epoll_event event = {};
        event.data.fd     = fd;
        event.events      = EPOLLRDHUP | EPOLLERR | EPOLLHUP;

        // If read handler was set, also listen for POLL-IN events
        if (handlers.on_read) {
            event.events |= EPOLLIN;
        }

        // If write handler was set, also listen for POLL-OUT events
        if (handlers.on_write) {
            event.events |= EPOLLOUT;
        }

        if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
            LOG(ERROR) << "Failed adding FD (" << fd << ")" << get_fd_name_print(handlers.name)
                       << " to the poll: " << strerror(errno);
            return false;
        }

        // Map the file descriptor to the event handlers structure
        m_fd_to_event_handlers[fd] = handlers;

        return true;
    };

    // Add the file descriptor to the poll
    if (!add_fd_to_epoll(fd)) {
        return false;
    }

    return true;
}

bool EventLoopImpl::remove_handlers(int fd)
{
    if (-1 == fd) {
        LOG(ERROR) << "Invalid file descriptor!";
        return false;
    }

    // Make sure that the file descriptor was previously added to the poll
    const auto &it = m_fd_to_event_handlers.find(fd);
    if (it == m_fd_to_event_handlers.end()) {
        LOG(WARNING) << "Requested to delete FD (" << fd
                     << ") from the poll, but it wasn't previously added.";

        return false;
    }

    // Delete the file descriptor from the poll
    auto error = false;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        LOG(ERROR) << "Failed deleting FD (" << fd << ")" << get_fd_name_print(it->second.name)
                   << " from the poll: " << strerror(errno);

        error = true;
    }
    LOG(INFO) << "Removed handlers for FD (" << fd << ")" << get_fd_name_print(it->second.name);

    // Erase the file descriptor from the map
    m_fd_to_event_handlers.erase(fd);

    return !error;
}

bool EventLoopImpl::set_handler_name(int fd, const std::string &name)
{
    auto it = m_fd_to_event_handlers.find(fd);
    if (it == m_fd_to_event_handlers.end()) {
        LOG(ERROR) << "Unable to find fd " << fd << " in the registered handlers";
        return false;
    }

    it->second.name = name;
    return true;
}

int EventLoopImpl::run()
{
    // Poll events
    epoll_event events[MAX_POLL_EVENTS]{};
    const size_t events_size = sizeof(events) / sizeof(events[0]);

    // Convert the global event loop timeout (if set) to milliseconds
    int timeout_millis =
        (m_timeout > std::chrono::milliseconds::zero()) ? static_cast<int>(m_timeout.count()) : -1;

    // Poll the file descriptors
    int num_events = epoll_wait(m_epoll_fd, events, events_size, timeout_millis);
    int err        = errno; // Save errno before any logging

    if (num_events == -1) {
        if (EINTR == err) {
            LOG(INFO) << strerror(err) << " during epoll_wait";
            return 0;
        }
        LOG(ERROR) << "Error during epoll_wait: " << strerror(err);
        return -1;
    }

    if (num_events == 0) {
        // Timeout... Do nothing
        return 0;
    }

    // Trigger event handlers
    for (int i = 0; i < num_events; i++) {
        int fd = events[i].data.fd;

        const auto &it = m_fd_to_event_handlers.find(fd);
        if (it == m_fd_to_event_handlers.end()) {
            LOG(ERROR) << "Event on unknown FD: " << fd;
            continue;
        }

        // Copy by value because it will be destroyed by remove_handlers below.
        auto handlers = it->second;

        // Handle errors
        if (events[i].events & EPOLLERR) {

            // Remove the file descriptor from the poll
            remove_handlers(fd);

            // Call the on_error handler of this file descriptor
            if (handlers.on_error && (!handlers.on_error(fd, *this))) {
                LOG(ERROR) << "Error handler on FD (" << fd << ")"
                           << get_fd_name_print(handlers.name) << " failed";
                return -1;
            }

            // Handle disconnected sockets (stream socket peer closed connection)
        } else if ((events[i].events & EPOLLRDHUP) || (events[i].events & EPOLLHUP)) {
            LOG(DEBUG) << "Socket with FD (" << fd << ")" << get_fd_name_print(handlers.name)
                       << " disconnected";

            // Remove the file descriptor from the poll
            remove_handlers(fd);

            // Call the on_disconnect handler of this file descriptor
            if (handlers.on_disconnect && (!handlers.on_disconnect(fd, *this))) {
                LOG(ERROR) << "Disconnect handler on FD (" << fd << ")"
                           << get_fd_name_print(handlers.name) << " failed";
                return -1;
            }

            // Handle incoming data
        } else if (events[i].events & EPOLLIN) {
            if (handlers.on_read) {
                const auto t0 = std::chrono::steady_clock::now();
                const bool ok = handlers.on_read(fd, *this);
                const auto t1 = std::chrono::steady_clock::now();
                const auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                eloop_trace_slow("read", fd, handlers.name, events[i].events, ms, num_events);
                if (!ok) {
                    LOG(ERROR) << "Read handler on FD (" << fd << ")"
                               << get_fd_name_print(handlers.name) << " failed";
                    return -1;
                }
            }

            // Handle write operations
        } else if (events[i].events & EPOLLOUT) {
            if (handlers.on_write) {
                const auto t0 = std::chrono::steady_clock::now();
                const bool ok = handlers.on_write(fd, *this);
                const auto t1 = std::chrono::steady_clock::now();
                const auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                eloop_trace_slow("write", fd, handlers.name, events[i].events, ms, num_events);
                if (!ok) {
                    LOG(ERROR) << "Write handler on FD (" << fd << ")"
                               << get_fd_name_print(handlers.name) << " failed";
                    return -1;
                }
            }

        } else {
            LOG(ERROR) << "FD (" << fd << ")" << get_fd_name_print(handlers.name)
                       << " generated unknown event: " << events[i].events;
        }
    }

    return num_events;
}

} // namespace beerocks
