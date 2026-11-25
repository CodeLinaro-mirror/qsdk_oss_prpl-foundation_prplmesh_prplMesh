/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <gtest/gtest.h>

#include "../ieee1905_transport_broker.h"

#include <algorithm>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/beerocks_os_utils.h>
#include <bcl/beerocks_thread_base.h>

#include <beerocks/tlvf/beerocks_message.h>

#include <tlvf/CmduMessageTx.h>
#include <tlvf/ieee_1905_1/eMessageType.h>

#include <mapf/transport/ieee1905_transport_messages.h>

#include <easylogging++.h>

namespace beerocks {
namespace transport {
namespace broker {
namespace tests {

using namespace beerocks::transport::messages;

//////////////////////////////////////////////////////////////////////////////
////////////////////////////// Global Variables //////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// UDS file for the tests
static const std::string broker_uds_file = "beerocks_broker_test_uds";

// Broker SocketServer listen buffer depth
static constexpr int broker_listen_buffer = 1;

// Default broker server timeout
static constexpr auto broker_timeout = std::chrono::milliseconds(100);

// Invalid message magic value
static constexpr uint32_t INVALID_MAGIC = 0x12345678;

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Helper Classes ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// Wrapper class for the BrokerServer for capturing errors
class BrokerServerWrapper : public BrokerServer {
public:
    // Inherit BrokerServer's constructor
    using BrokerServer::BrokerServer;

    bool error() { return m_error_occurred; }
    int last_client_fd() const { return m_last_client_fd; }

protected:
    bool m_error_occurred = false;

    bool handle_msg(std::shared_ptr<Socket> sd, bool *fatal_error = nullptr) override
    {
        m_last_client_fd = sd->getSocketFd();
        if (BrokerServer::handle_msg(sd, fatal_error) == false) {
            m_error_occurred = true;
            return false;
        }

        m_error_occurred = false;
        return true;
    }

private:
    int m_last_client_fd = -1;
};

static constexpr size_t small_frame_size = 98;
static constexpr size_t large_frame_size = 24 * 1024;
static constexpr uint16_t query_type =
    uint16_t(ieee1905_1::eMessageType::CLIENT_CAPABILITY_QUERY_MESSAGE);

class BrokerBackpressureTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(broker.start());
        ASSERT_NO_FATAL_FAILURE(connect_and_subscribe(slow));
        const int send_buffer_size = 4096;
        ASSERT_EQ(0, setsockopt(broker.last_client_fd(), SOL_SOCKET, SO_SNDBUF, &send_buffer_size,
                                sizeof(send_buffer_size)));
        ASSERT_NO_FATAL_FAILURE(connect_and_subscribe(active));
    }

    void TearDown() override { EXPECT_TRUE(broker.stop()); }

    void publish(size_t frame_size, uint16_t cookie)
    {
        CmduRxMessage message;
        auto *metadata     = message.metadata();
        metadata->msg_type = query_type;
        metadata->cookie   = cookie;
        metadata->length   = frame_size - frame_overhead;
        std::fill_n(message.data(), frame_size - frame_overhead, uint8_t(cookie));
        ASSERT_TRUE(broker.publish(message));

        Message::ReadState state;
        Message::ReadStatus status;
        auto received = read_transport_message(*active, state, status);
        ASSERT_EQ(Message::ReadStatus::Complete, status);
        ASSERT_TRUE(received);
        ASSERT_NO_FATAL_FAILURE(expect_frame(*received, frame_size, cookie));
    }

    void read_slow_frame(size_t frame_size, uint16_t cookie)
    {
        Message::ReadState state;
        for (int attempt = 0; attempt < 16; ++attempt) {
            Message::ReadStatus status;
            auto received = read_transport_message(*slow, state, status);
            if (received) {
                ASSERT_NO_FATAL_FAILURE(expect_frame(*received, frame_size, cookie));
                return;
            }
            ASSERT_EQ(Message::ReadStatus::Incomplete, status);
            ASSERT_GT(event_loop->run(), 0);
        }
        FAIL() << "Timed out waiting for the rest of frame " << cookie;
    }

    std::shared_ptr<EventLoopImpl> event_loop = std::make_shared<EventLoopImpl>(broker_timeout);
    std::unique_ptr<SocketClient> slow;

private:
    static constexpr size_t frame_overhead =
        sizeof(Message::Header) + sizeof(CmduXxMessage::Metadata);

    void connect_and_subscribe(std::unique_ptr<SocketClient> &client)
    {
        client = std::make_unique<SocketClient>(broker_uds_file);
        ASSERT_EQ(1, event_loop->run()); // Accept the connection

        SubscribeMessage subscribe;
        subscribe.metadata()->type                   = SubscribeMessage::ReqType::SUBSCRIBE;
        subscribe.metadata()->msg_types_count        = 1;
        subscribe.metadata()->msg_types[0].bits.type = query_type;
        ASSERT_TRUE(send_transport_message(*client, subscribe));
        ASSERT_EQ(1, event_loop->run()); // Process the subscription
        ASSERT_FALSE(broker.error());
    }

    void expect_frame(const Message &received, size_t frame_size, uint16_t cookie)
    {
        ASSERT_EQ(Type::CmduRxMessage, received.type());
        ASSERT_EQ(frame_size, sizeof(Message::Header) + received.len());
        const auto &cmdu = static_cast<const CmduRxMessage &>(received);
        EXPECT_EQ(query_type, cmdu.metadata()->msg_type);
        EXPECT_EQ(cookie, cmdu.metadata()->cookie);
        ASSERT_EQ(frame_size - frame_overhead, cmdu.metadata()->length);
        auto *payload = cmdu.data();
        EXPECT_TRUE(std::all_of(payload, payload + cmdu.metadata()->length,
                                [cookie](uint8_t byte) { return byte == uint8_t(cookie); }));
    }

    BrokerServerWrapper broker{
        std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer), event_loop};
    std::unique_ptr<SocketClient> active;
};

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Tests ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// Suppress cppcheck syntax error for gtest TEST macro
// cppcheck-suppress syntaxError
TEST(broker_server, setup)
{
    // Configure easylogging++ formatting
    el::Configurations defaultConf;
    defaultConf.setToDefault();
    defaultConf.setGlobally(el::ConfigurationType::Format,
                            "%level %datetime{%H:%m:%s:%g} <%thread> %fbase[%line] --> %msg");

    // el::Loggers::addFlag(el::LoggingFlag::ColoredTerminalOutput);
    el::Loggers::reconfigureLogger("default", defaultConf);
}

TEST(transport_message, maximum_cmdu_fits_in_transport_frame)
{
    CmduTxMessage message;
    message.metadata()->length = Message::kMaxCmduLength;
    message.data();

    EXPECT_EQ(sizeof(CmduXxMessage::Metadata) + Message::kMaxCmduLength, message.len());
    EXPECT_LE(message.len(), Message::kMaxFrameLength);
}

TEST(broker_server, maximum_cmdu_transport_message)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    size_t received_length = 0;
    broker_wrapper.register_external_message_handler(
        [&](std::unique_ptr<messages::Message> &msg, BrokerServer &broker) -> bool {
            received_length = msg->len();
            return true;
        });
    ASSERT_TRUE(broker_wrapper.start());

    CmduTxMessage message;
    message.metadata()->length = Message::kMaxCmduLength;
    message.data();

    SocketClient sock(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection
    ASSERT_TRUE(messages::send_transport_message(sock, message));
    ASSERT_EQ(1, event_loop->run()); // Process

    EXPECT_EQ(message.len(), received_length);
    EXPECT_FALSE(broker_wrapper.error());
    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, fragmented_header_and_payload)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    size_t messages_received = 0;
    broker_wrapper.register_internal_message_handler(
        [&](std::unique_ptr<messages::Message> &msg, BrokerServer &broker) -> bool {
            ++messages_received;
            return true;
        });
    ASSERT_TRUE(broker_wrapper.start());

    Message::Frame frame(32);
    Message message(Type::InterfaceConfigurationRequestMessage, {frame});
    auto header       = message.header();
    auto header_bytes = reinterpret_cast<const uint8_t *>(&header);

    SocketClient sock(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    constexpr size_t first_header_part = 5;
    ASSERT_EQ(first_header_part, sock.writeBytes(header_bytes, first_header_part));
    ASSERT_EQ(1, event_loop->run());
    EXPECT_EQ(0U, messages_received);
    EXPECT_FALSE(broker_wrapper.error());
    EXPECT_EQ(0, event_loop->run()); // Partial header was consumed; socket is no longer readable.

    ASSERT_EQ(
        sizeof(header) - first_header_part,
        sock.writeBytes(header_bytes + first_header_part, sizeof(header) - first_header_part));
    ASSERT_EQ(1, event_loop->run());
    EXPECT_EQ(0U, messages_received);
    EXPECT_FALSE(broker_wrapper.error());
    EXPECT_EQ(0, event_loop->run()); // Header was consumed while waiting for the payload.

    constexpr size_t first_payload_part = 11;
    ASSERT_EQ(first_payload_part, sock.writeBytes(frame.data(), first_payload_part));
    ASSERT_EQ(1, event_loop->run());
    EXPECT_EQ(0U, messages_received);
    EXPECT_FALSE(broker_wrapper.error());
    EXPECT_EQ(0, event_loop->run()); // Partial payload was buffered; socket is not left readable.

    ASSERT_EQ(frame.len() - first_payload_part,
              sock.writeBytes(frame.data() + first_payload_part, frame.len() - first_payload_part));
    ASSERT_EQ(1, event_loop->run());
    EXPECT_EQ(1U, messages_received);
    EXPECT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, invalid_message_magic)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    // Register a dummy internal message handler
    broker_wrapper.register_internal_message_handler(
        [](std::unique_ptr<messages::Message> &msg, BrokerServer &broker) -> bool { return true; });

    // Create a random message
    messages::Message dummy;

    // Invalid header
    messages::Message::Header header;
    header.magic = INVALID_MAGIC;

    SocketClient sock1(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    LOG(DEBUG) << "Sending INVALID magic...";
    ASSERT_TRUE(messages::send_transport_message(sock1, dummy, &header));
    ASSERT_EQ(1, event_loop->run()); // Process
    ASSERT_TRUE(broker_wrapper.error());
    ASSERT_EQ(1, event_loop->run()); // Process the shutdown socket event.

    // A framing error closes the offending client. Reconnect before sending another message.
    SocketClient sock2(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    LOG(DEBUG) << "Sending VALID magic...";
    ASSERT_TRUE(messages::send_transport_message(sock2, dummy));
    ASSERT_EQ(1, event_loop->run());
    ASSERT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, oversized_message_length)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    messages::Message::Header header;
    header.len = Message::kMaxFrameLength + 1;

    SocketClient sock(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    ASSERT_EQ(sizeof(header),
              sock.writeBytes(reinterpret_cast<const uint8_t *>(&header), sizeof(header)));
    ASSERT_EQ(1, event_loop->run()); // Process and disconnect the client
    ASSERT_TRUE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, subscribe_empty_message)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    // Create a subscribe message
    SubscribeMessage subscribe;
    subscribe.metadata()->type = SubscribeMessage::ReqType::SUBSCRIBE;

    // Connect to the broker and send the message
    SocketClient sock1(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process

    ASSERT_TRUE(broker_wrapper.error());

    // A valid frame with invalid message contents is a non-fatal handler error. The same client
    // connection must remain usable for subsequent messages.
    subscribe.metadata()->msg_types_count    = 1;
    subscribe.metadata()->msg_types[0].value = 5;
    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process on the existing connection
    ASSERT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, subscribe_single_type)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    // Create a subscribe message
    SubscribeMessage subscribe;
    subscribe.metadata()->type               = SubscribeMessage::ReqType::SUBSCRIBE;
    subscribe.metadata()->msg_types_count    = 1;
    subscribe.metadata()->msg_types[0].value = 5;

    // Connect to the broker and send the message
    SocketClient sock1(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process

    ASSERT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST_F(BrokerBackpressureTest, resumes_after_zero_progress_write)
{
    constexpr uint16_t frame_count = 64;
    for (uint16_t cookie = 1; cookie <= frame_count; ++cookie) {
        ASSERT_NO_FATAL_FAILURE(publish(small_frame_size, cookie));
    }

    // The slow socket was full while there were still frames to publish (PPM-4379).
    const auto readable = slow->getBytesReady();
    ASSERT_GT(readable, 0);
    ASSERT_LT(size_t(readable), frame_count * small_frame_size);
    ASSERT_EQ(0U, size_t(readable) % small_frame_size);

    for (uint16_t cookie = 1; cookie <= frame_count; ++cookie) {
        ASSERT_NO_FATAL_FAILURE(read_slow_frame(small_frame_size, cookie));
    }
    EXPECT_EQ(0, event_loop->run()); // Write readiness is disabled when the queue is empty.

    ASSERT_NO_FATAL_FAILURE(publish(small_frame_size, frame_count + 1));
    ASSERT_NO_FATAL_FAILURE(read_slow_frame(small_frame_size, frame_count + 1));
}

TEST_F(BrokerBackpressureTest, completes_partial_frame_before_next_frame)
{
    ASSERT_NO_FATAL_FAILURE(publish(large_frame_size, 1));
    const auto readable = slow->getBytesReady();
    ASSERT_GT(readable, 0);
    ASSERT_LT(size_t(readable), large_frame_size);

    ASSERT_NO_FATAL_FAILURE(publish(small_frame_size, 2));
    ASSERT_EQ(readable, slow->getBytesReady());
    ASSERT_NO_FATAL_FAILURE(read_slow_frame(large_frame_size, 1));
    ASSERT_NO_FATAL_FAILURE(read_slow_frame(small_frame_size, 2));
    EXPECT_EQ(0, event_loop->run());
}

TEST_F(BrokerBackpressureTest, disconnects_only_subscriber_with_full_queue)
{
    pollfd slow_fd{slow->getSocketFd(), POLLRDHUP, 0};
    ASSERT_NO_FATAL_FAILURE(publish(large_frame_size, 1));
    ASSERT_EQ(0, poll(&slow_fd, 1, 0));

    constexpr uint16_t frame_count = 32;
    for (uint16_t cookie = 2; cookie <= frame_count; ++cookie) {
        ASSERT_NO_FATAL_FAILURE(publish(large_frame_size, cookie));
        ASSERT_GE(poll(&slow_fd, 1, 0), 0);
        if (slow_fd.revents & POLLRDHUP) {
            break;
        }
    }
    ASSERT_TRUE(slow_fd.revents & POLLRDHUP);
    ASSERT_EQ(1, event_loop->run()); // Remove the disconnected socket from the event loop.
    ASSERT_NO_FATAL_FAILURE(publish(small_frame_size, frame_count + 1));
    EXPECT_EQ(0, event_loop->run());
}

TEST(broker_server, subscribe_multiple_types)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    // Create a subscribe message
    SubscribeMessage subscribe;
    subscribe.metadata()->type               = SubscribeMessage::ReqType::SUBSCRIBE;
    subscribe.metadata()->msg_types_count    = 3;
    subscribe.metadata()->msg_types[0].value = 1;
    subscribe.metadata()->msg_types[1].value = 2;
    subscribe.metadata()->msg_types[2].value = 3;

    // Connect to the broker and send the message
    SocketClient sock1(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process

    ASSERT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, unsubscribe_empty_message)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    // Create a subscribe message
    SubscribeMessage subscribe;
    subscribe.metadata()->type = SubscribeMessage::ReqType::UNSUBSCRIBE;

    // Connect to the broker and send the message
    SocketClient sock1(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process

    ASSERT_TRUE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, unsubscribe_single_type)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    // Create a subscribe message
    SubscribeMessage subscribe;
    subscribe.metadata()->type               = SubscribeMessage::ReqType::UNSUBSCRIBE;
    subscribe.metadata()->msg_types_count    = 1;
    subscribe.metadata()->msg_types[0].value = 5;

    // Connect to the broker and send the message
    SocketClient sock1(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process

    ASSERT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, unsubscribe_multiple_types)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    // Create a subscribe message
    SubscribeMessage subscribe;
    subscribe.metadata()->type               = SubscribeMessage::ReqType::UNSUBSCRIBE;
    subscribe.metadata()->msg_types_count    = 3;
    subscribe.metadata()->msg_types[0].value = 1;
    subscribe.metadata()->msg_types[1].value = 2;
    subscribe.metadata()->msg_types[2].value = 3;

    // Connect to the broker and send the message
    SocketClient sock1(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection

    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process

    ASSERT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

TEST(broker_server, subscribe_unsubscribe)
{
    auto server_socket = std::make_shared<SocketServer>(broker_uds_file, broker_listen_buffer);
    auto event_loop    = std::make_shared<EventLoopImpl>(broker_timeout);
    BrokerServerWrapper broker_wrapper(server_socket, event_loop);

    ASSERT_TRUE(broker_wrapper.start());

    // Create a subscribe message
    SubscribeMessage subscribe;
    subscribe.metadata()->type               = SubscribeMessage::ReqType::SUBSCRIBE;
    subscribe.metadata()->msg_types_count    = 3;
    subscribe.metadata()->msg_types[0].value = 1;
    subscribe.metadata()->msg_types[1].value = 2;
    subscribe.metadata()->msg_types[2].value = 3;

    // Connect to the broker and send the message
    SocketClient sock1(broker_uds_file);
    ASSERT_EQ(1, event_loop->run()); // Accept the connection
    ASSERT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process
    ASSERT_FALSE(broker_wrapper.error());

    // Unsubscribe
    subscribe.metadata()->type = SubscribeMessage::ReqType::UNSUBSCRIBE;
    ASSERT_TRUE(messages::send_transport_message(sock1, subscribe));
    ASSERT_EQ(1, event_loop->run()); // Process
    ASSERT_FALSE(broker_wrapper.error());

    ASSERT_TRUE(broker_wrapper.stop());
}

} // namespace tests
} // namespace broker
} // namespace transport
} // namespace beerocks
