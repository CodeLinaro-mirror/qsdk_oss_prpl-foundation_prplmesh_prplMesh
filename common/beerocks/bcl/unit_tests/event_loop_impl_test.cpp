/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_event_loop_impl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sys/socket.h>

#include <easylogging++.h>

using namespace beerocks;

using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::StrictMock;

/**
 * @brief Mockable event handlers class
 */
class EventHandlersMock : public EventLoop::EventHandlers {

public:
    EventHandlersMock()
    {
        on_read       = [&](int fd, EventLoop &loop) { return handle_read(fd, &loop); };
        on_write      = [&](int fd, EventLoop &loop) { return handle_write(fd, &loop); };
        on_disconnect = [&](int fd, EventLoop &loop) { return handle_disconnect(fd, &loop); };
        on_error      = [&](int fd, EventLoop &loop) { return handle_error(fd, &loop); };
    }

    MOCK_METHOD(bool, handle_read, (int fd, EventLoop *));
    MOCK_METHOD(bool, handle_write, (int fd, EventLoop *));
    MOCK_METHOD(bool, handle_disconnect, (int fd, EventLoop *));
    MOCK_METHOD(bool, handle_error, (int fd, EventLoop *));
};

TEST(beerocks_event_loop_impl, setup)
{
    // Configure easylogging++ formatting
    el::Configurations defaultConf;
    defaultConf.setToDefault();
    defaultConf.setGlobally(el::ConfigurationType::Format,
                            "%level %datetime{%H:%m:%s:%g} <%thread> %fbase[%line] --> %msg");

    // el::Loggers::addFlag(el::LoggingFlag::ColoredTerminalOutput);
    el::Loggers::reconfigureLogger("default", defaultConf);
}

TEST(beerocks_event_loop_impl, simple_read_write)
{
    EventLoopImpl loop;
    StrictMock<EventHandlersMock> reader;
    StrictMock<EventHandlersMock> writer;

    // Disable the on_write handler (to prevent the loop from firing "write ready" events)
    reader.on_write = nullptr;

    // Create a socketpair for reader/writer simulation
    int sv[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    ASSERT_NE(-1, rc);

    int writer_fd = sv[0];
    int reader_fd = sv[1];

    {
        InSequence sequence;

        EXPECT_CALL(writer, handle_write(writer_fd, &loop))
            .WillOnce(Invoke([](int fd, EventLoop *loop) -> bool {
                // Send a dummy byte and remove the socket from the loop to prevent additional events
                EXPECT_EQ(1, send(fd, "X", 1, 0));
                EXPECT_TRUE(loop->remove_handlers(fd));
                return true;
            }));

        EXPECT_CALL(reader, handle_read(reader_fd, &loop))
            .WillOnce(Invoke([](int fd, EventLoop *loop) -> bool {
                char dummy;
                EXPECT_EQ(1, read(fd, &dummy, 1));
                return true;
            }));
    };

    // Add the reader/writer sockets and handlers to the loop
    ASSERT_TRUE(loop.register_handlers(writer_fd, writer));
    ASSERT_TRUE(loop.register_handlers(reader_fd, reader));

    // Run the loop
    ASSERT_EQ(1, loop.run());
    ASSERT_EQ(1, loop.run());

    // Delete the reader socket
    ASSERT_TRUE(loop.remove_handlers(reader_fd));

    // Close the file descriptors
    close(writer_fd);
    close(reader_fd);
}

/**
 * In this test, two sockets have pending events. The handler of the first socket removes the
 * second one from the poll and puts a new socket with the same file descriptor number in its place,
 * as when a socket is closed and opened again. The pending event of the removed socket must not be
 * passed to the handlers of the new one. The events of the new socket are reported by the next run.
 */
TEST(beerocks_event_loop_impl, removed_fd_should_not_get_stale_event)
{
    EventLoopImpl loop;

    int sv_0[2];
    int sv_1[2];
    int sv_new[2];
    ASSERT_NE(-1, socketpair(AF_UNIX, SOCK_STREAM, 0, sv_0));
    ASSERT_NE(-1, socketpair(AF_UNIX, SOCK_STREAM, 0, sv_1));
    ASSERT_NE(-1, socketpair(AF_UNIX, SOCK_STREAM, 0, sv_new));

    int reader_fd_0 = sv_0[1];
    int reader_fd_1 = sv_1[1];

    int new_reads = 0;
    EventLoop::EventHandlers new_handlers;
    new_handlers.on_read = [&](int fd, EventLoop &event_loop) -> bool {
        new_reads++;
        char dummy;
        recv(fd, &dummy, 1, MSG_DONTWAIT);
        return true;
    };

    // Whichever reader is handled first replaces the other one
    int old_reads   = 0;
    int replaced_fd = -1;
    EventLoop::EventHandlers old_handlers;
    old_handlers.on_read = [&](int fd, EventLoop &event_loop) -> bool {
        old_reads++;
        char dummy;
        EXPECT_EQ(1, recv(fd, &dummy, 1, MSG_DONTWAIT));

        replaced_fd = (fd == reader_fd_0) ? reader_fd_1 : reader_fd_0;
        EXPECT_TRUE(event_loop.remove_handlers(replaced_fd));
        EXPECT_EQ(replaced_fd, dup2(sv_new[1], replaced_fd));
        EXPECT_TRUE(event_loop.register_handlers(replaced_fd, new_handlers));
        return true;
    };

    ASSERT_TRUE(loop.register_handlers(reader_fd_0, old_handlers));
    ASSERT_TRUE(loop.register_handlers(reader_fd_1, old_handlers));

    // Both readers have a pending event
    ASSERT_EQ(1, send(sv_0[0], "X", 1, 0));
    ASSERT_EQ(1, send(sv_1[0], "X", 1, 0));

    ASSERT_EQ(2, loop.run());
    ASSERT_EQ(1, old_reads);
    ASSERT_EQ(0, new_reads);

    // The new socket gets its own events
    ASSERT_EQ(1, send(sv_new[0], "X", 1, 0));
    ASSERT_EQ(1, loop.run());
    ASSERT_EQ(1, old_reads);
    ASSERT_EQ(1, new_reads);

    int remaining_fd = (replaced_fd == reader_fd_0) ? reader_fd_1 : reader_fd_0;
    ASSERT_TRUE(loop.remove_handlers(remaining_fd));
    ASSERT_TRUE(loop.remove_handlers(replaced_fd));

    for (int fd : {sv_0[0], sv_0[1], sv_1[0], sv_1[1], sv_new[0], sv_new[1]}) {
        close(fd);
    }
}
