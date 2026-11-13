/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_event_loop_mock.h>
#include <bcl/network/netlink_event_listener_impl.h>
#include <bcl/network/sockets_mock.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace {

/**
 * @brief Creates a UDP socket with a pending ECONNREFUSED error.
 *
 * Sends a datagram to a closed port on the loopback interface. The ICMP port unreachable reply
 * sets the socket error and makes poll() report POLLERR.
 *
 * @return Socket file descriptor or -1 on error.
 */
int create_socket_with_pending_error()
{
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Find a closed port: bind a socket to any free port and close it
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    socklen_t length = sizeof(address);
    if ((bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) ||
        (getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0)) {
        close(fd);
        return -1;
    }
    close(fd);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    pollfd poll_fd{fd, 0, 0};
    if ((connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) ||
        (send(fd, "x", 1, 0) != 1) || (poll(&poll_fd, 1, 1000) != 1) ||
        !(poll_fd.revents & POLLERR)) {
        close(fd);
        return -1;
    }

    return fd;
}

class NetlinkEventListenerImplTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ON_CALL(*m_connection, socket()).WillByDefault(Return(m_socket));
        ON_CALL(*m_socket, fd()).WillByDefault(Invoke([this]() { return m_fd; }));
    }

    void TearDown() override
    {
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

    /**
     * @brief Creates the listener, saves its event handlers and records the notified events.
     */
    std::unique_ptr<beerocks::net::NetlinkEventListenerImpl> create_listener()
    {
        EXPECT_CALL(*m_event_loop, register_handlers(m_fd, _))
            .WillOnce(DoAll(SaveArg<1>(&m_handlers), Return(true)));
        EXPECT_CALL(*m_event_loop, remove_handlers(m_fd)).WillOnce(Return(true));

        auto listener =
            std::make_unique<beerocks::net::NetlinkEventListenerImpl>(m_connection, m_event_loop);
        listener->register_handler(
            [this](const nlmsghdr *msg_hdr) { m_events.push_back(msg_hdr->nlmsg_type); });

        return listener;
    }

    int m_fd = -1;
    std::shared_ptr<NiceMock<beerocks::net::SocketMock>> m_socket =
        std::make_shared<NiceMock<beerocks::net::SocketMock>>();
    std::shared_ptr<NiceMock<beerocks::net::SocketConnectionMock>> m_connection =
        std::make_shared<NiceMock<beerocks::net::SocketConnectionMock>>();
    std::shared_ptr<StrictMock<beerocks::EventLoopMock>> m_event_loop =
        std::make_shared<StrictMock<beerocks::EventLoopMock>>();
    beerocks::EventLoop::EventHandlers m_handlers;
    std::vector<uint16_t> m_events;
};

/**
 * @brief Emulates a receive that fails with given error.
 */
int receive_error(int error)
{
    errno = error;
    return -1;
}

/**
 * @brief Emulates the receive of a RTM_NEWLINK message.
 */
int receive_new_link(beerocks::net::Buffer &buffer)
{
    auto msg_hdr         = reinterpret_cast<nlmsghdr *>(buffer.data());
    msg_hdr->nlmsg_len   = NLMSG_LENGTH(sizeof(ifinfomsg));
    msg_hdr->nlmsg_type  = RTM_NEWLINK;
    msg_hdr->nlmsg_flags = 0;
    msg_hdr->nlmsg_seq   = 0;
    msg_hdr->nlmsg_pid   = 0;
    buffer.length() += msg_hdr->nlmsg_len;
    return msg_hdr->nlmsg_len;
}

/**
 * On a socket error, the listener registers the socket again, processes the queued messages and
 * then notifies that events have been lost. The socket error is cleared.
 */
TEST_F(NetlinkEventListenerImplTest, error_should_register_again_and_notify_events_lost)
{
    m_fd = create_socket_with_pending_error();
    ASSERT_GE(m_fd, 0);

    auto listener = create_listener();

    EXPECT_CALL(*m_event_loop, register_handlers(m_fd, _)).WillOnce(Return(true));
    EXPECT_CALL(*m_connection, receive(_))
        .WillOnce(Invoke(receive_new_link))
        .WillOnce(Invoke([](beerocks::net::Buffer &buffer) { return receive_error(EAGAIN); }));

    ASSERT_TRUE(m_handlers.on_error);
    ASSERT_TRUE(m_handlers.on_error(m_fd, *m_event_loop));

    std::vector<uint16_t> expected_events{RTM_NEWLINK, NLMSG_OVERRUN};
    ASSERT_EQ(m_events, expected_events);

    int error        = -1;
    socklen_t length = sizeof(error);
    ASSERT_EQ(getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &error, &length), 0);
    ASSERT_EQ(error, 0);
}

/**
 * Without a pending socket error, the listener does not register the socket again (the error
 * would be reported in a loop) and does not notify anything.
 */
TEST_F(NetlinkEventListenerImplTest, error_without_socket_error_should_not_register_again)
{
    m_fd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(m_fd, 0);

    auto listener = create_listener();

    ASSERT_TRUE(m_handlers.on_error(m_fd, *m_event_loop));
    ASSERT_TRUE(m_events.empty());
}

/**
 * If a read fails with ENOBUFS, the listener processes the queued messages and then notifies that
 * events have been lost.
 */
TEST_F(NetlinkEventListenerImplTest, read_overrun_should_notify_events_lost)
{
    m_fd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(m_fd, 0);

    auto listener = create_listener();

    EXPECT_CALL(*m_connection, receive(_))
        .WillOnce(Invoke([](beerocks::net::Buffer &buffer) { return receive_error(ENOBUFS); }))
        .WillOnce(Invoke(receive_new_link))
        .WillOnce(Invoke([](beerocks::net::Buffer &buffer) { return receive_error(EAGAIN); }));

    ASSERT_TRUE(m_handlers.on_read(m_fd, *m_event_loop));

    std::vector<uint16_t> expected_events{RTM_NEWLINK, NLMSG_OVERRUN};
    ASSERT_EQ(m_events, expected_events);
}

} // namespace
