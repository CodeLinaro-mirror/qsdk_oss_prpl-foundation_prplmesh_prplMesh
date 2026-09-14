/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <gtest/gtest.h>

#include "../ieee1905_transport.h"

#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/network/bridge_state_manager_impl.h>
#include <bcl/network/bridge_state_monitor_mock.h>
#include <bcl/network/bridge_state_reader_mock.h>
#include <bcl/network/interface_state_manager_impl.h>
#include <bcl/network/interface_state_monitor_mock.h>
#include <bcl/network/interface_state_reader_mock.h>

#include <tlvf/tlvftypes.h>

#include <arpa/inet.h>
#include <easylogging++.h>
#include <linux/if_ether.h>

#include <algorithm>
#include <memory>
#include <vector>

using ::testing::StrictMock;

namespace {

// Mirror the packed IEEE1905 CMDU header layout from Ieee1905Transport (8 bytes).
#pragma pack(push, 1)
struct TestCmduHeader {
    uint8_t messageVersion  = 0;
    uint8_t _reservedField0 = 0;
    uint16_t messageType    = 0;
    uint16_t messageId      = 0;
    uint8_t fragmentId      = 0;
    uint8_t flags           = 0;

    void SetLastFragmentIndicator(bool value) { flags = (flags & ~0x80) | (value ? 0x80 : 0x00); }

    bool GetLastFragmentIndicator() const { return (flags & 0x80); }
};
#pragma pack(pop)

static_assert(sizeof(TestCmduHeader) == 8, "unexpected CMDU header size");

void write_cmdu_header(std::vector<uint8_t> &buf, uint16_t msg_type, uint16_t mid, uint8_t frag_id,
                       bool last_frag)
{
    buf.resize(sizeof(TestCmduHeader));
    auto *hdr            = reinterpret_cast<TestCmduHeader *>(buf.data());
    hdr->messageVersion  = 0;
    hdr->_reservedField0 = 0;
    hdr->messageType     = htons(msg_type);
    hdr->messageId       = htons(mid);
    hdr->fragmentId      = frag_id;
    hdr->flags           = 0;
    hdr->SetLastFragmentIndicator(last_frag);
}

void append_tlv(std::vector<uint8_t> &buf, uint8_t type, size_t value_len, uint8_t fill = 0xAB)
{
    buf.push_back(type);
    const uint16_t len = htons(static_cast<uint16_t>(value_len));
    buf.push_back(static_cast<uint8_t>(len & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    buf.insert(buf.end(), value_len, fill);
}

void append_eom(std::vector<uint8_t> &buf) { buf.insert(buf.end(), {0, 0, 0}); }

std::vector<std::vector<uint8_t>> split_body_into_fragments(const std::vector<uint8_t> &payload,
                                                            uint16_t msg_type, uint16_t mid,
                                                            size_t threshold)
{
    const size_t hdr_size = sizeof(TestCmduHeader);

    std::vector<std::vector<uint8_t>> fragments;
    size_t body_offset = hdr_size;
    size_t remaining   = payload.size() - hdr_size;
    uint8_t frag_id    = 0;

    while (remaining > 0) {
        const size_t chunk = std::min(remaining, threshold);
        std::vector<uint8_t> fragment;
        write_cmdu_header(fragment, msg_type, mid, frag_id, remaining <= chunk);
        fragment.insert(fragment.end(), payload.begin() + body_offset,
                        payload.begin() + body_offset + chunk);
        fragments.push_back(std::move(fragment));
        body_offset += chunk;
        remaining -= chunk;
        ++frag_id;
    }

    return fragments;
}

} // namespace

namespace beerocks {
namespace transport {
namespace tests {

using namespace beerocks::transport::messages;

/**
 * @brief Friend wrapper around Ieee1905Transport (same idea as BrokerServerWrapper).
 *
 * Private helpers are invoked from wrapper methods declared on this class. Private
 * nested types are not re-exported; the test file uses TestCmduHeader instead.
 *
 * send_packet_to_network_interface() is private virtual in the base class so this
 * wrapper can capture fragments without BUILD_TESTS hooks.
 */
class TestableIeee1905Transport : public Ieee1905Transport {
public:
    using Ieee1905Transport::Ieee1905Transport;

    bool verify_storage(const std::vector<uint8_t> &payload)
    {
        auto storage = payload;
        Packet packet;
        packet.ether_type       = ETH_P_1905_1;
        packet.src_if_type      = CmduRxMessage::IF_TYPE_NET;
        packet.payload.iov_base = storage.data();
        packet.payload.iov_len  = storage.size();
        return verify_packet(packet);
    }

    const std::vector<std::vector<uint8_t>> &captured_sends() const { return m_captured_sends; }

    bool de_fragment_storage(std::vector<uint8_t> &storage, const sMacAddr &src_mac)
    {
        Packet packet;
        packet.ether_type       = ETH_P_1905_1;
        packet.src_if_type      = CmduRxMessage::IF_TYPE_NET;
        packet.src              = src_mac;
        packet.payload.iov_base = storage.data();
        packet.payload.iov_len  = storage.size();

        if (!de_fragment_packet(packet)) {
            return false;
        }

        storage.assign(static_cast<uint8_t *>(packet.payload.iov_base),
                       static_cast<uint8_t *>(packet.payload.iov_base) + packet.payload.iov_len);
        return true;
    }

    bool fragment_and_send_local_bus(unsigned int if_index, const std::vector<uint8_t> &payload)
    {
        m_captured_sends.clear();
        auto storage = payload;
        Packet packet;
        packet.ether_type       = ETH_P_1905_1;
        packet.src_if_type      = CmduRxMessage::IF_TYPE_LOCAL_BUS;
        packet.payload.iov_base = storage.data();
        packet.payload.iov_len  = storage.size();
        return fragment_and_send_packet_to_network_interface(if_index, packet);
    }

    bool try_de_fragment_storage(std::vector<uint8_t> &storage, const sMacAddr &src_mac)
    {
        Packet packet;
        packet.ether_type       = ETH_P_1905_1;
        packet.src_if_type      = CmduRxMessage::IF_TYPE_NET;
        packet.src              = src_mac;
        packet.payload.iov_base = storage.data();
        packet.payload.iov_len  = storage.size();
        return de_fragment_packet(packet);
    }

private:
    bool send_packet_to_network_interface(unsigned int if_index, Packet &packet) override
    {
        (void)if_index;
        m_captured_sends.emplace_back(static_cast<uint8_t *>(packet.payload.iov_base),
                                      static_cast<uint8_t *>(packet.payload.iov_base) +
                                          packet.payload.iov_len);
        return true;
    }

    std::vector<std::vector<uint8_t>> m_captured_sends;
};

class Ieee1905TransportFragmentationTest : public ::testing::Test {
protected:
    static constexpr size_t kThreshold = tlvf::MAX_TLV_SIZE;

    void SetUp() override
    {
        el::Configurations default_conf;
        default_conf.setToDefault();
        el::Loggers::reconfigureLogger("default", default_conf);

        auto if_monitor = std::make_unique<StrictMock<beerocks::net::InterfaceStateMonitorMock>>();
        auto if_reader  = std::make_unique<StrictMock<beerocks::net::InterfaceStateReaderMock>>();
        auto br_monitor = std::make_unique<StrictMock<beerocks::net::BridgeStateMonitorMock>>();
        auto br_reader  = std::make_unique<StrictMock<beerocks::net::BridgeStateReaderMock>>();

        m_if_mgr = std::make_shared<beerocks::net::InterfaceStateManagerImpl>(std::move(if_monitor),
                                                                              std::move(if_reader));
        m_br_mgr = std::make_shared<beerocks::net::BridgeStateManagerImpl>(std::move(br_monitor),
                                                                           std::move(br_reader));

        m_event_loop = std::make_shared<EventLoopImpl>(std::chrono::milliseconds(100));
        m_broker     = std::make_shared<broker::BrokerServer>(
            std::make_shared<SocketServer>("ieee1905_transport_frag_test_uds", 1), m_event_loop);

        m_transport =
            std::make_unique<TestableIeee1905Transport>(m_if_mgr, m_br_mgr, m_broker, m_event_loop);
    }

    bool reassemble_fragments(const std::vector<std::vector<uint8_t>> &fragments,
                              const sMacAddr &src_mac, std::vector<uint8_t> &reassembled)
    {
        for (const auto &fragment : fragments) {
            auto fragment_storage = fragment;
            if (m_transport->de_fragment_storage(fragment_storage, src_mac)) {
                reassembled = std::move(fragment_storage);
                return true;
            }
        }
        return false;
    }

    std::unique_ptr<TestableIeee1905Transport> m_transport;
    std::shared_ptr<beerocks::net::InterfaceStateManager> m_if_mgr;
    std::shared_ptr<beerocks::net::BridgeStateManager> m_br_mgr;
    std::shared_ptr<EventLoopImpl> m_event_loop;
    std::shared_ptr<broker::BrokerServer> m_broker;
};

TEST_F(Ieee1905TransportFragmentationTest, non_fragmented_packet_passes_defragmentation)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x0002, 7, 0, true);
    append_tlv(payload, 0x07, 8);
    append_eom(payload);

    sMacAddr src_mac{};
    src_mac.oct[5] = 0x22;
    auto storage   = payload;

    EXPECT_TRUE(m_transport->de_fragment_storage(storage, src_mac));
    EXPECT_EQ(storage, payload);
}

TEST_F(Ieee1905TransportFragmentationTest, reassembles_byte_split_oversized_tlv)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x8049, 99, 0, true);
    append_tlv(payload, 0xE8, kThreshold + 100);
    append_eom(payload);

    const auto fragments = split_body_into_fragments(payload, 0x8049, 99, kThreshold);
    ASSERT_GT(fragments.size(), 1U);

    sMacAddr src_mac{};
    src_mac.oct[5] = 0x11;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(fragments, src_mac, reassembled));
    EXPECT_EQ(reassembled, payload);
}

TEST_F(Ieee1905TransportFragmentationTest, reassembles_multiple_tlv_fragments)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x8049, 55, 0, true);
    append_tlv(payload, 0x07, 700, 0x01);
    append_tlv(payload, 0x08, 700, 0x02);
    append_tlv(payload, 0x09, 200, 0x03);
    append_eom(payload);

    const auto fragments = split_body_into_fragments(payload, 0x8049, 55, kThreshold);
    ASSERT_GT(fragments.size(), 1U);

    sMacAddr src_mac{};
    src_mac.oct[5] = 0x33;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(fragments, src_mac, reassembled));
    EXPECT_EQ(reassembled, payload);
}

TEST_F(Ieee1905TransportFragmentationTest, rejects_out_of_order_fragment)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x8049, 77, 0, true);
    append_tlv(payload, 0xE8, kThreshold + 50);
    append_eom(payload);

    const auto fragments = split_body_into_fragments(payload, 0x8049, 77, kThreshold);
    ASSERT_GE(fragments.size(), 2U);

    sMacAddr src_mac{};
    src_mac.oct[5] = 0x44;

    auto second_fragment = fragments[1];
    EXPECT_FALSE(m_transport->try_de_fragment_storage(second_fragment, src_mac));
}

TEST_F(Ieee1905TransportFragmentationTest, small_packet_is_sent_without_fragmentation)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x0001, 42, 0, true);
    append_tlv(payload, 0x07, 16);
    append_eom(payload);

    ASSERT_LE(payload.size(), sizeof(TestCmduHeader) + kThreshold);
    ASSERT_TRUE(m_transport->fragment_and_send_local_bus(1, payload));
    ASSERT_EQ(m_transport->captured_sends().size(), 1U);
    EXPECT_EQ(m_transport->captured_sends().front(), payload);
}

TEST_F(Ieee1905TransportFragmentationTest, oversized_packet_is_fragmented_on_send)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x8049, 99, 0, true);
    append_tlv(payload, 0xE8, kThreshold + 100);
    append_eom(payload);

    ASSERT_GT(payload.size(), sizeof(TestCmduHeader) + kThreshold);
    ASSERT_TRUE(m_transport->fragment_and_send_local_bus(1, payload));
    ASSERT_GT(m_transport->captured_sends().size(), 1U);

    for (size_t idx = 0; idx < m_transport->captured_sends().size(); ++idx) {
        const auto &fragment = m_transport->captured_sends()[idx];
        const auto *hdr      = reinterpret_cast<const TestCmduHeader *>(fragment.data());
        EXPECT_EQ(hdr->fragmentId, static_cast<uint8_t>(idx));
        EXPECT_EQ(hdr->GetLastFragmentIndicator(), idx + 1 == m_transport->captured_sends().size());
        EXPECT_LE(fragment.size(), sizeof(TestCmduHeader) + kThreshold);
    }

    sMacAddr src_mac{};
    src_mac.oct[5] = 0x55;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(m_transport->captured_sends(), src_mac, reassembled));
    EXPECT_EQ(reassembled, payload);
}

TEST_F(Ieee1905TransportFragmentationTest, tiny_continuations_pass_receive_validation)
{
    for (size_t tail : {1U, 2U}) {
        SCOPED_TRACE(tail);
        std::vector<uint8_t> payload;
        write_cmdu_header(payload, 0x8049, 100 + tail, 0, true);
        append_tlv(payload, 0xE8, kThreshold + tail - 3);
        append_eom(payload);
        ASSERT_TRUE(m_transport->fragment_and_send_local_bus(1, payload));
        for (const auto &fragment : m_transport->captured_sends()) {
            EXPECT_TRUE(m_transport->verify_storage(fragment))
                << "TX emitted a " << fragment.size() << "-byte CMDU that RX rejects";
        }
    }
}

TEST_F(Ieee1905TransportFragmentationTest, reassembles_fragment_ending_in_partial_tlv_header)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x8049, 123, 0, true);
    // The first TLV ends one byte before the end of fragment 1.
    // Only the next TLV's type fits; its length follows in fragment 2.
    append_tlv(payload, 0xE8, 2 * kThreshold - 4);
    append_tlv(payload, 0x07, 4);
    append_eom(payload);
    auto fragments = split_body_into_fragments(payload, 0x8049, 123, kThreshold);
    ASSERT_EQ(fragments.size(), 3U);
    for (const auto &fragment : fragments) {
        ASSERT_TRUE(m_transport->verify_storage(fragment));
    }
    sMacAddr src_mac{};
    src_mac.oct[5] = 0x66;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(fragments, src_mac, reassembled));
    EXPECT_EQ(reassembled, payload);
}

TEST_F(Ieee1905TransportFragmentationTest, whole_tlvs_and_oversized_tlv_roundtrip)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x8049, 124, 0, true);
    append_tlv(payload, 0x07, 200);
    append_tlv(payload, 0xE8, 3 * kThreshold + 57);
    append_tlv(payload, 0x08, 100);
    append_eom(payload);
    ASSERT_TRUE(m_transport->fragment_and_send_local_bus(1, payload));
    sMacAddr src_mac{};
    src_mac.oct[5] = 0x67;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(m_transport->captured_sends(), src_mac, reassembled));
    EXPECT_EQ(reassembled, payload);
}

/**
 * @brief Oversized TLV byte-splits must never produce a fragment whose body is 1 or 2 bytes
 * (the receiver's verify_packet() drops IEEE1905 packets shorter than header + 3 bytes).
 *
 * Covers tails left by the initial split (TLV size = threshold + 1/2) and by the
 * continuation split (TLV size = 2 * threshold + 1/2, e.g. 2985 with a 1492 threshold).
 */
TEST_F(Ieee1905TransportFragmentationTest, oversized_tlv_split_never_leaves_tiny_fragment)
{
    const size_t tlv_header                   = 3;
    const std::vector<size_t> tlv_total_sizes = {
        kThreshold + 1,     kThreshold + 2,     kThreshold + 3,     2 * kThreshold + 1,
        2 * kThreshold + 2, 2 * kThreshold + 3, 3 * kThreshold + 1, 3 * kThreshold + 2,
    };

    uint16_t mid  = 200;
    uint8_t macid = 0x60;
    for (const auto tlv_size : tlv_total_sizes) {
        SCOPED_TRACE("TLV total size " + std::to_string(tlv_size));

        std::vector<uint8_t> payload;
        write_cmdu_header(payload, 0x8049, mid, 0, true);
        append_tlv(payload, 0xE8, tlv_size - tlv_header);
        append_eom(payload);

        ASSERT_TRUE(m_transport->fragment_and_send_local_bus(1, payload));
        const auto &sends = m_transport->captured_sends();
        ASSERT_GT(sends.size(), 1U);

        size_t total_body = 0;
        for (size_t idx = 0; idx < sends.size(); ++idx) {
            const auto &fragment = sends[idx];
            EXPECT_GE(fragment.size(), sizeof(TestCmduHeader) + 3) << "fragment " << idx;
            EXPECT_LE(fragment.size(), sizeof(TestCmduHeader) + kThreshold) << "fragment " << idx;
            const auto *hdr = reinterpret_cast<const TestCmduHeader *>(fragment.data());
            EXPECT_EQ(hdr->fragmentId, static_cast<uint8_t>(idx));
            total_body += fragment.size() - sizeof(TestCmduHeader);
        }
        EXPECT_EQ(total_body, payload.size() - sizeof(TestCmduHeader));

        sMacAddr src_mac{};
        src_mac.oct[5] = macid++;
        std::vector<uint8_t> reassembled;
        ASSERT_TRUE(reassemble_fragments(sends, src_mac, reassembled));
        EXPECT_EQ(reassembled, payload);
        ++mid;
    }
}

/**
 * @brief TLVs that fit within the threshold keep whole-TLV boundaries; following TLVs are
 * not packed into a continuation fragment of a byte-split TLV.
 */
TEST_F(Ieee1905TransportFragmentationTest, split_tlv_followed_by_whole_tlvs_keeps_boundaries)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x8049, 300, 0, true);
    // Oversized TLV whose split leaves a 1-byte tail without rebalancing.
    append_tlv(payload, 0xE8, kThreshold + 1 - 3, 0x11);
    append_tlv(payload, 0x07, 100, 0x22);
    append_eom(payload);

    ASSERT_TRUE(m_transport->fragment_and_send_local_bus(1, payload));
    const auto &sends = m_transport->captured_sends();
    ASSERT_EQ(sends.size(), 3U);

    // Fragment 0: threshold - 2 bytes; fragment 1: 3-byte continuation only;
    // fragment 2: the whole small TLV plus EOM.
    EXPECT_EQ(sends[0].size(), sizeof(TestCmduHeader) + kThreshold - 2);
    EXPECT_EQ(sends[1].size(), sizeof(TestCmduHeader) + 3);
    EXPECT_EQ(sends[2].size(), sizeof(TestCmduHeader) + 103 + 3);

    sMacAddr src_mac{};
    src_mac.oct[5] = 0x77;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(sends, src_mac, reassembled));
    EXPECT_EQ(reassembled, payload);
}

} // namespace tests
} // namespace transport
} // namespace beerocks
