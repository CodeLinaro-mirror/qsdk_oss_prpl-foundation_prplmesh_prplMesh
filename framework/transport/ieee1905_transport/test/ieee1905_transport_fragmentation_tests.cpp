/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <gtest/gtest.h>

#include "../ieee1905_fragmentation.h"

#include <arpa/inet.h>
#include <easylogging++.h>

#include <algorithm>
#include <vector>

using beerocks::transport::Ieee1905CmduHeader;

namespace {

void write_cmdu_header(std::vector<uint8_t> &buf, uint16_t msg_type, uint16_t mid, uint8_t frag_id,
                       bool last_frag)
{
    buf.resize(sizeof(Ieee1905CmduHeader));
    auto *hdr            = reinterpret_cast<Ieee1905CmduHeader *>(buf.data());
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
    const size_t hdr_size = sizeof(Ieee1905CmduHeader);

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

class Ieee1905TransportFragmentationTest : public ::testing::Test {
protected:
    static constexpr size_t kThreshold = kIeee1905FragmentationThreashold;

    void SetUp() override
    {
        el::Configurations default_conf;
        default_conf.setToDefault();
        el::Loggers::reconfigureLogger("default", default_conf);
    }

    // Same minimum length check as Ieee1905Transport::verify_packet() on the receiver.
    static bool passes_receive_validation(const std::vector<uint8_t> &fragment)
    {
        return fragment.size() >= sizeof(Ieee1905CmduHeader) + 3;
    }

    bool fragment(const std::vector<uint8_t> &payload)
    {
        m_fragments.clear();
        return ieee1905_fragment_cmdu(
            payload.data(), payload.size(), [&](uint8_t *fragment, size_t fragment_length) {
                m_fragments.emplace_back(fragment, fragment + fragment_length);
                return true;
            });
    }

    bool defragment(std::vector<uint8_t> &storage, const sMacAddr &src_mac)
    {
        uint8_t *cmdu = storage.data();
        size_t length = storage.size();
        if (!m_defragmenter.defragment(src_mac, cmdu, length)) {
            return false;
        }

        storage.assign(cmdu, cmdu + length);
        return true;
    }

    bool reassemble_fragments(const std::vector<std::vector<uint8_t>> &fragments,
                              const sMacAddr &src_mac, std::vector<uint8_t> &reassembled)
    {
        for (const auto &fragment : fragments) {
            auto fragment_storage = fragment;
            if (defragment(fragment_storage, src_mac)) {
                reassembled = std::move(fragment_storage);
                return true;
            }
        }
        return false;
    }

    Ieee1905Defragmenter m_defragmenter;
    std::vector<std::vector<uint8_t>> m_fragments;
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

    EXPECT_TRUE(defragment(storage, src_mac));
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
    EXPECT_FALSE(defragment(second_fragment, src_mac));
}

TEST_F(Ieee1905TransportFragmentationTest, small_packet_is_a_single_fragment)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x0001, 42, 0, true);
    append_tlv(payload, 0x07, 16);
    append_eom(payload);

    ASSERT_LE(payload.size(), sizeof(Ieee1905CmduHeader) + kThreshold);
    ASSERT_TRUE(fragment(payload));
    ASSERT_EQ(m_fragments.size(), 1U);
    EXPECT_EQ(m_fragments.front(), payload);
}

TEST_F(Ieee1905TransportFragmentationTest, oversized_packet_is_fragmented)
{
    std::vector<uint8_t> payload;
    write_cmdu_header(payload, 0x8049, 99, 0, true);
    append_tlv(payload, 0xE8, kThreshold + 100);
    append_eom(payload);

    ASSERT_GT(payload.size(), sizeof(Ieee1905CmduHeader) + kThreshold);
    ASSERT_TRUE(fragment(payload));
    ASSERT_GT(m_fragments.size(), 1U);

    for (size_t idx = 0; idx < m_fragments.size(); ++idx) {
        const auto &fragment = m_fragments[idx];
        const auto *hdr      = reinterpret_cast<const Ieee1905CmduHeader *>(fragment.data());
        EXPECT_EQ(hdr->fragmentId, static_cast<uint8_t>(idx));
        EXPECT_EQ(hdr->GetLastFragmentIndicator(), idx + 1 == m_fragments.size());
        EXPECT_LE(fragment.size(), sizeof(Ieee1905CmduHeader) + kThreshold);
    }

    sMacAddr src_mac{};
    src_mac.oct[5] = 0x55;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(m_fragments, src_mac, reassembled));
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
        ASSERT_TRUE(fragment(payload));
        for (const auto &fragment : m_fragments) {
            EXPECT_TRUE(passes_receive_validation(fragment))
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
        ASSERT_TRUE(passes_receive_validation(fragment));
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
    ASSERT_TRUE(fragment(payload));
    sMacAddr src_mac{};
    src_mac.oct[5] = 0x67;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(m_fragments, src_mac, reassembled));
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

        ASSERT_TRUE(fragment(payload));
        const auto &sends = m_fragments;
        ASSERT_GT(sends.size(), 1U);

        size_t total_body = 0;
        for (size_t idx = 0; idx < sends.size(); ++idx) {
            const auto &fragment = sends[idx];
            EXPECT_GE(fragment.size(), sizeof(Ieee1905CmduHeader) + 3) << "fragment " << idx;
            EXPECT_LE(fragment.size(), sizeof(Ieee1905CmduHeader) + kThreshold)
                << "fragment " << idx;
            const auto *hdr = reinterpret_cast<const Ieee1905CmduHeader *>(fragment.data());
            EXPECT_EQ(hdr->fragmentId, static_cast<uint8_t>(idx));
            total_body += fragment.size() - sizeof(Ieee1905CmduHeader);
        }
        EXPECT_EQ(total_body, payload.size() - sizeof(Ieee1905CmduHeader));

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

    ASSERT_TRUE(fragment(payload));
    const auto &sends = m_fragments;
    ASSERT_EQ(sends.size(), 3U);

    // Fragment 0: threshold - 2 bytes; fragment 1: 3-byte continuation only;
    // fragment 2: the whole small TLV plus EOM.
    EXPECT_EQ(sends[0].size(), sizeof(Ieee1905CmduHeader) + kThreshold - 2);
    EXPECT_EQ(sends[1].size(), sizeof(Ieee1905CmduHeader) + 3);
    EXPECT_EQ(sends[2].size(), sizeof(Ieee1905CmduHeader) + 103 + 3);

    sMacAddr src_mac{};
    src_mac.oct[5] = 0x77;
    std::vector<uint8_t> reassembled;
    ASSERT_TRUE(reassemble_fragments(sends, src_mac, reassembled));
    EXPECT_EQ(reassembled, payload);
}

} // namespace tests
} // namespace transport
} // namespace beerocks
