/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <mapf/transport/ieee1905_transport_messages.h>

#include <sys/uio.h>

#include <easylogging++.h>

namespace beerocks {
namespace transport {
namespace messages {

// Declaration of static members
constexpr uint32_t Message::kMessageMagic;
constexpr uint32_t Message::kMaxFrameLength;
constexpr uint8_t SubscribeMessage::MAX_SUBSCRIBE_TYPES;

//////////////////////////////////////////////////////////////////////////////
////////////////////////////// Helper Functions //////////////////////////////
//////////////////////////////////////////////////////////////////////////////

std::unique_ptr<Message>
create_transport_message(Type type, std::initializer_list<messages::Message::Frame> frame)
{
    switch (messages::Type(type)) {
    case messages::Type::CmduRxMessage:
        return std::unique_ptr<messages::CmduRxMessage>{new messages::CmduRxMessage(frame)};
    case messages::Type::CmduTxMessage:
        return std::unique_ptr<messages::CmduTxMessage>{new messages::CmduTxMessage(frame)};
    case messages::Type::SubscribeMessage:
        return std::unique_ptr<messages::SubscribeMessage>{new messages::SubscribeMessage(frame)};
    case messages::Type::CmduTxConfirmationMessage:
        return std::unique_ptr<messages::CmduTxConfirmationMessage>{
            new messages::CmduTxConfirmationMessage(frame)};
    case messages::Type::InterfaceConfigurationRequestMessage:
        return std::unique_ptr<messages::InterfaceConfigurationRequestMessage>{
            new messages::InterfaceConfigurationRequestMessage(frame)};
    case messages::Type::AlMacAddressConfigurationMessage:
        return std::unique_ptr<messages::AlMacAddressConfigurationMessage>{
            new messages::AlMacAddressConfigurationMessage(frame)};
    case messages::Type::VlanConfigurationRequestMessage:
        return std::unique_ptr<messages::VlanConfigurationRequestMessage>{
            new messages::VlanConfigurationRequestMessage(frame)};
    case messages::Type::DuplicateCmduNotificationMessage:
        return std::unique_ptr<messages::DuplicateCmduNotificationMessage>{
            new messages::DuplicateCmduNotificationMessage(frame)};
    default:
        LOG(WARNING) << "Received unknown message type: " << int(type);
        return std::unique_ptr<messages::Message>{new messages::Message(Type::Invalid, frame)};
    }
}

std::unique_ptr<Message> read_transport_message(Socket &sd, Message::ReadState &state,
                                                Message::ReadStatus &status)
{
    status = Message::ReadStatus::Error;

    if (!state.header_received) {
        auto bytes_ready = sd.getBytesReady();
        if (bytes_ready < 0) {
            LOG(ERROR) << "Error getting pending header length from fd = " << sd.getSocketFd();
            return nullptr;
        }
        if (bytes_ready == 0) {
            status = Message::ReadStatus::Incomplete;
            return nullptr;
        }

        auto header_bytes = reinterpret_cast<uint8_t *>(&state.header);
        auto bytes_to_read =
            std::min(size_t(bytes_ready), sizeof(state.header) - state.header_bytes_received);
        auto read_bytes = sd.readBytes(header_bytes + state.header_bytes_received, bytes_to_read,
                                       false, bytes_to_read);
        if (read_bytes <= 0) {
            LOG(ERROR) << "Error reading the message header: " << read_bytes;
            return nullptr;
        }

        state.header_bytes_received += size_t(read_bytes);
        if (state.header_bytes_received < sizeof(state.header)) {
            status = Message::ReadStatus::Incomplete;
            return nullptr;
        }

        if (state.header.magic != messages::Message::kMessageMagic) {
            LOG(ERROR) << "Invalid message header: magic = 0x" << std::hex << state.header.magic
                       << std::dec << ", length = " << state.header.len
                       << ", fd = " << sd.getSocketFd();
            return nullptr;
        }

        if (state.header.len > messages::Message::kMaxFrameLength) {
            LOG(ERROR) << "Message length is too large: " << state.header.len << " > "
                       << messages::Message::kMaxFrameLength;
            return nullptr;
        }

        state.header_received = true;
        state.payload.resize(state.header.len);
    }

    const auto &header = state.header;
    if (state.payload_bytes_received < header.len) {
        auto bytes_ready = sd.getBytesReady();
        if (bytes_ready < 0) {
            LOG(ERROR) << "Error getting pending payload length from fd = " << sd.getSocketFd();
            return nullptr;
        }
        if (bytes_ready == 0) {
            status = Message::ReadStatus::Incomplete;
            return nullptr;
        }

        auto bytes_to_read =
            std::min(size_t(bytes_ready), header.len - state.payload_bytes_received);
        auto read_bytes = sd.readBytes(state.payload.data() + state.payload_bytes_received,
                                       bytes_to_read, false, bytes_to_read);
        if (read_bytes <= 0) {
            LOG(ERROR) << "Error reading the message payload: " << read_bytes;
            return nullptr;
        }

        state.payload_bytes_received += size_t(read_bytes);
        if (state.payload_bytes_received < header.len) {
            status = Message::ReadStatus::Incomplete;
            return nullptr;
        }
    }

    std::unique_ptr<messages::Message> message;

    if (!header.len) {
        message = create_transport_message(Type(header.type), {});
    } else {
        messages::Message::Frame frame(header.len, state.payload.data());
        message = create_transport_message(Type(header.type), {frame});
    }

    LOG_IF(!message, ERROR) << "Failed creating message object for type: " << header.type;
    status = message ? Message::ReadStatus::Complete : Message::ReadStatus::Error;
    if (message) {
        state = {};
    }
    return message;
}

bool send_transport_message(Socket &sd, const Message &msg, const Message::Header *header)
{
    auto hdr    = (header) ? *header : msg.header();
    iovec iov[] = {{.iov_base = (void *)&hdr, .iov_len = sizeof(hdr)},
                   {.iov_base = (void *)(msg.frame().data()), .iov_len = hdr.len}};

    // Stream sockets may accept only part of an iovec. Continue from the first byte not written
    // so a short write cannot leave a truncated transport frame while being reported as success.
    constexpr size_t iov_count = sizeof(iov) / sizeof(iov[0]);
    size_t current_iov         = 0;
    size_t bytes_written       = 0;
    const size_t message_size  = sizeof(hdr) + hdr.len;

    while (current_iov < iov_count) {
        // Skip empty vectors, for example the payload vector of a header-only message.
        if (iov[current_iov].iov_len == 0) {
            ++current_iov;
            continue;
        }

        auto written = writev(sd.getSocketFd(), &iov[current_iov], iov_count - current_iov);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            LOG(ERROR) << "writev failed after " << bytes_written << "/" << message_size
                       << " bytes: " << strerror(errno);
            return false;
        }
        if (written == 0) {
            LOG(ERROR) << "writev made no progress after " << bytes_written << "/" << message_size
                       << " bytes";
            return false;
        }

        bytes_written += size_t(written);
        size_t remaining = size_t(written);
        while (remaining > 0 && current_iov < iov_count) {
            if (remaining >= iov[current_iov].iov_len) {
                remaining -= iov[current_iov].iov_len;
                ++current_iov;
                continue;
            }

            iov[current_iov].iov_base =
                static_cast<uint8_t *>(iov[current_iov].iov_base) + remaining;
            iov[current_iov].iov_len -= remaining;
            remaining = 0;
        }
    }

    return bytes_written == message_size;
}

} // namespace messages
} // namespace transport
} // namespace beerocks
