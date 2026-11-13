/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_event_loop.h>
#include <bcl/network/buffer_impl.h>
#include <bcl/network/netlink_event_listener_impl.h>

#include <easylogging++.h>

#include <cerrno>
#include <cstring>
#include <sys/socket.h>

using namespace beerocks;

namespace beerocks {
namespace net {

static constexpr size_t netlink_buffer_size = 8192;

NetlinkEventListenerImpl::NetlinkEventListenerImpl(std::shared_ptr<Socket::Connection> connection,
                                                   std::shared_ptr<EventLoop> event_loop)
    : m_connection(connection), m_event_loop(event_loop)
{
    m_handlers.name    = "Netlink Event Listener";
    m_handlers.on_read = [&](int fd, EventLoop &loop) -> bool {
        BufferImpl<netlink_buffer_size> buffer;
        int result = m_connection->receive(buffer);
        if (result > 0) {
            parse(buffer);
        } else if ((result < 0) && (errno == ENOBUFS)) {
            LOG(ERROR) << "Netlink socket receive buffer overrun, events have been lost";
            handle_overrun();
        }

        return true;
    };
    // The event loop removes the socket from the poll on error. A receive buffer overrun
    // (ENOBUFS) is reported as an error, so register the socket again to keep receiving events,
    // and recover from the events lost in the overrun.
    m_handlers.on_error = [&](int fd, EventLoop &loop) -> bool {
        // Reading SO_ERROR also clears it, so the error is not reported again
        int error        = 0;
        socklen_t length = sizeof(error);
        if ((getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0) || (error == 0)) {
            LOG(ERROR) << "Netlink socket failed, events are no longer received";
            return true;
        }

        LOG(ERROR) << "Netlink socket error: " << strerror(error) << " (" << error
                   << "), events have been lost";

        if (!loop.register_handlers(fd, m_handlers)) {
            LOG(ERROR) << "Unable to register Netlink socket again, events are no longer received";
            return true;
        }

        handle_overrun();

        return true;
    };

    if (!m_event_loop->register_handlers(m_connection->socket()->fd(), m_handlers)) {
        LOG(ERROR) << "Unable to register Netlink socket, events are not received";
    }
}

NetlinkEventListenerImpl::~NetlinkEventListenerImpl()
{
    m_event_loop->remove_handlers(m_connection->socket()->fd());
}

void NetlinkEventListenerImpl::handle_overrun()
{
    // After an overrun, the kernel queues no new events until the receive queue is empty, so
    // empty it right away. A read does not wait, so the loop ends when the queue is empty
    while (true) {
        BufferImpl<netlink_buffer_size> buffer;
        int result = m_connection->receive(buffer);
        if (result > 0) {
            parse(buffer);
        } else if ((result == 0) || (errno != ENOBUFS)) {
            break;
        }
    }

    // The handlers must read the current state again. NLMSG_OVERRUN is the Netlink message type
    // reserved for lost data
    LOG(INFO) << "Notifying Netlink event handlers that events have been lost";

    nlmsghdr msg_hdr{};
    msg_hdr.nlmsg_len  = NLMSG_LENGTH(0);
    msg_hdr.nlmsg_type = NLMSG_OVERRUN;
    notify_netlink_event(&msg_hdr);
}

void NetlinkEventListenerImpl::parse(const Buffer &buffer) const
{
    const nlmsghdr *msg_hdr = reinterpret_cast<const nlmsghdr *>(buffer.data());
    size_t length           = buffer.length();

    while (NLMSG_OK(msg_hdr, length)) {

        notify_netlink_event(msg_hdr);

        msg_hdr = NLMSG_NEXT(msg_hdr, length);
    }
}

} // namespace net
} // namespace beerocks
