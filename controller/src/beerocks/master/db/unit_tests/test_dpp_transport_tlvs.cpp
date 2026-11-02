/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include <gtest/gtest.h>
#include <tlvf/common/sMacAddr.h>
#include <tlvf/tlvftypes.h>
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
#include <tlvf/wfa_map/tlvDppBootstrappingUriNotification.h>
#include <tlvf/wfa_map/tlvDppCceIndication.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>
#include <tlvf/wfa_map/tlvDppMessage.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace {

// PUBLIC_INTERFACE
/** Direct Encap preserves binary payloads and encodes the TLV length in network byte order. */
TEST(DppTransportTlvs, DirectFrameRoundTrip)
{
    std::array<uint8_t, 1024> buffer{};
    std::vector<uint8_t> frame(300);
    for (size_t i = 0; i < frame.size(); ++i) {
        frame[i] = static_cast<uint8_t>(i);
    }
    wfa_map::tlvDppMessage tx(buffer.data(), buffer.size());
    ASSERT_TRUE(tx.isInitialized());
    ASSERT_TRUE(tx.set_dpp_frame(frame.data(), frame.size()));
    EXPECT_EQ(frame.size(), tx.dpp_frame_length());
    EXPECT_EQ(3U + frame.size(), tx.getLen());
    ASSERT_TRUE(tx.finalize());
    EXPECT_EQ(0x01, buffer[1]);
    EXPECT_EQ(0x2c, buffer[2]);
    // Parsing swaps fields in place, so do not use the TX object after parsing.
    auto wire = buffer;
    wfa_map::tlvDppMessage rx(wire.data(), tx.getLen(), true);
    ASSERT_TRUE(rx.isInitialized());
    EXPECT_EQ(wfa_map::eTlvTypeMap::TLV_DPP_MESSAGE, rx.type());
    EXPECT_EQ(frame.size(), rx.length());
    ASSERT_NE(nullptr, rx.dpp_frame());
    EXPECT_EQ(frame, std::vector<uint8_t>(rx.dpp_frame(), rx.dpp_frame() + rx.dpp_frame_length()));
    EXPECT_EQ(nullptr, rx.dpp_frame(frame.size()));
}

// PUBLIC_INTERFACE
/** Failed direct payload allocations preserve length and allow a later valid allocation. */
TEST(DppTransportTlvs, DirectSetterRejectsNullOverflowAndDuplicate)
{
    std::array<uint8_t, 32> buffer{};
    const uint8_t frame[] = {0x00, 0xff, 0x80};
    wfa_map::tlvDppMessage tx(buffer.data(), 6);
    ASSERT_TRUE(tx.isInitialized());
    EXPECT_FALSE(tx.set_dpp_frame(nullptr, 1));
    EXPECT_FALSE(tx.alloc_dpp_frame(4));
    EXPECT_EQ(0U, tx.dpp_frame_length());
    EXPECT_EQ(0U, tx.length());
    ASSERT_TRUE(tx.set_dpp_frame(frame, sizeof(frame)));
    EXPECT_FALSE(tx.set_dpp_frame(frame, sizeof(frame)));
    EXPECT_EQ(3U, tx.dpp_frame_length());
    EXPECT_EQ(6U, tx.getLen());
}

// PUBLIC_INTERFACE
/** Empty direct TLVs are representable; rejection of empty protocol frames belongs to the caller. */
TEST(DppTransportTlvs, EmptyDirectFrameRoundTrip)
{
    std::array<uint8_t, 32> buffer{};
    wfa_map::tlvDppMessage tx(buffer.data(), buffer.size());
    ASSERT_TRUE(tx.isInitialized());
    ASSERT_TRUE(tx.finalize());
    auto wire = buffer;
    wfa_map::tlvDppMessage rx(wire.data(), tx.getLen(), true);
    ASSERT_TRUE(rx.isInitialized());
    EXPECT_EQ(0U, rx.dpp_frame_length());
    EXPECT_EQ(nullptr, rx.dpp_frame());
}

// PUBLIC_INTERFACE
/** Proxied Public Action and GAS frames retain optional destination MAC and frame metadata. */
TEST(DppTransportTlvs, ProxiedFrameRoundTripWithAndWithoutMac)
{
    const sMacAddr destination = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x03}};
    const std::vector<uint8_t> frame{0x04, 0x09, 0x00, 0xff, 0x80};
    for (bool has_mac : {false, true}) {
        for (bool gas : {false, true}) {
            SCOPED_TRACE(has_mac);
            SCOPED_TRACE(gas);
            std::array<uint8_t, 128> buffer{};
            wfa_map::tlv1905EncapDpp tx(buffer.data(), buffer.size());
            ASSERT_TRUE(tx.isInitialized());
            tx.frame_flags().enrollee_mac_address_present = has_mac;
            tx.frame_flags().dpp_frame_indicator          = gas;
            if (has_mac) {
                ASSERT_TRUE(tx.set_dest_sta_mac(destination));
            }
            tx.frame_type() = gas ? wfa_map::tlv1905EncapDpp::DPP_GAS_FRAME
                                  : wfa_map::tlv1905EncapDpp::DPP_AUTHENTICATION_REQUEST;
            ASSERT_TRUE(tx.set_encapsulated_frame(frame.data(), frame.size()));
            const size_t expected_length = 7U + (has_mac ? 6U : 0U) + frame.size();
            EXPECT_EQ(expected_length, tx.getLen());
            ASSERT_TRUE(tx.finalize());
            EXPECT_EQ((has_mac ? 0x80 : 0) | (gas ? 0x20 : 0), buffer[3]);
            auto wire = buffer;
            wfa_map::tlv1905EncapDpp rx(wire.data(), expected_length, true);
            ASSERT_TRUE(rx.isInitialized());
            EXPECT_EQ(expected_length - 3, rx.length());
            EXPECT_EQ(has_mac, bool(rx.frame_flags().enrollee_mac_address_present));
            EXPECT_EQ(gas, bool(rx.frame_flags().dpp_frame_indicator));
            EXPECT_EQ(gas ? wfa_map::tlv1905EncapDpp::DPP_GAS_FRAME
                          : wfa_map::tlv1905EncapDpp::DPP_AUTHENTICATION_REQUEST,
                      rx.frame_type());
            if (has_mac) {
                ASSERT_NE(nullptr, rx.dest_sta_mac());
                EXPECT_EQ(destination, *rx.dest_sta_mac());
            } else {
                EXPECT_EQ(nullptr, rx.dest_sta_mac());
            }
            ASSERT_EQ(frame.size(), rx.encapsulated_frame_length());
            ASSERT_NE(nullptr, rx.encapsulated_frame());
            EXPECT_EQ(frame, std::vector<uint8_t>(rx.encapsulated_frame(),
                                                  rx.encapsulated_frame() + frame.size()));
            EXPECT_EQ(nullptr, rx.encapsulated_frame(frame.size()));
        }
    }
}

// PUBLIC_INTERFACE
/** Proxied setters reject invalid allocation requests without overwriting the existing payload. */
TEST(DppTransportTlvs, ProxiedSetterRejectsNullOverflowAndDuplicate)
{
    std::array<uint8_t, 32> buffer{};
    const uint8_t frame[] = {0x04, 0x09, 0xff};
    wfa_map::tlv1905EncapDpp tx(buffer.data(), 10);
    ASSERT_TRUE(tx.isInitialized());
    EXPECT_FALSE(tx.set_encapsulated_frame(nullptr, 1));
    EXPECT_FALSE(tx.alloc_encapsulated_frame(4));
    EXPECT_EQ(0U, tx.encapsulated_frame_length());
    ASSERT_TRUE(tx.set_encapsulated_frame(frame, sizeof(frame)));
    EXPECT_FALSE(tx.set_encapsulated_frame(frame, sizeof(frame)));
    EXPECT_EQ(3U, tx.encapsulated_frame_length());
    EXPECT_TRUE(std::equal(std::begin(frame), std::end(frame), tx.encapsulated_frame()));
}

// PUBLIC_INTERFACE
/** The optional destination can be updated without allocating a second MAC field. */
TEST(DppTransportTlvs, ProxiedDestinationUpdateDoesNotGrowTlv)
{
    std::array<uint8_t, 64> buffer{};
    const sMacAddr first  = {{0x02, 0, 0, 0, 0, 1}};
    const sMacAddr second = {{0x02, 0, 0, 0, 0, 2}};
    wfa_map::tlv1905EncapDpp tx(buffer.data(), buffer.size());
    ASSERT_TRUE(tx.isInitialized());
    tx.frame_flags().enrollee_mac_address_present = true;
    ASSERT_TRUE(tx.set_dest_sta_mac(first));
    const auto length = tx.getLen();
    EXPECT_FALSE(tx.alloc_dest_sta_mac());
    ASSERT_TRUE(tx.set_dest_sta_mac(second));
    EXPECT_EQ(length, tx.getLen());
    ASSERT_NE(nullptr, tx.dest_sta_mac());
    EXPECT_EQ(second, *tx.dest_sta_mac());
}

// PUBLIC_INTERFACE
/** Chirp hashing metadata and optional destination survive serialization unchanged. */
TEST(DppTransportTlvs, ChirpRoundTripWithAndWithoutMac)
{
    const sMacAddr destination = {{0x02, 0, 0, 0, 0, 3}};
    std::array<uint8_t, 32> hash{};
    for (size_t i = 0; i < hash.size(); ++i) {
        hash[i] = static_cast<uint8_t>(0x80 + i);
    }
    for (bool has_mac : {false, true}) {
        for (bool valid : {false, true}) {
            SCOPED_TRACE(has_mac);
            SCOPED_TRACE(valid);
            std::array<uint8_t, 128> buffer{};
            wfa_map::tlvDppChirpValue tx(buffer.data(), buffer.size());
            ASSERT_TRUE(tx.isInitialized());
            tx.flags().hash_validity                = valid;
            tx.flags().enrollee_mac_address_present = has_mac;
            if (has_mac) {
                ASSERT_TRUE(tx.set_dest_sta_mac(destination));
            }
            ASSERT_TRUE(tx.set_hash(hash.data(), hash.size()));
            ASSERT_EQ(hash.size(), tx.hash_length());
            const size_t length = 5U + (has_mac ? 6U : 0U) + hash.size();
            EXPECT_EQ(length, tx.getLen());
            ASSERT_TRUE(tx.finalize());
            EXPECT_EQ((has_mac ? 0x80 : 0) | (valid ? 0x40 : 0), buffer[3]);
            auto wire = buffer;
            wfa_map::tlvDppChirpValue rx(wire.data(), length, true);
            ASSERT_TRUE(rx.isInitialized());
            EXPECT_EQ(length - 3, rx.length());
            EXPECT_EQ(valid, bool(rx.flags().hash_validity));
            EXPECT_EQ(has_mac, bool(rx.flags().enrollee_mac_address_present));
            if (has_mac) {
                ASSERT_NE(nullptr, rx.dest_sta_mac());
                EXPECT_EQ(destination, *rx.dest_sta_mac());
            } else {
                EXPECT_EQ(nullptr, rx.dest_sta_mac());
            }
            ASSERT_EQ(hash.size(), rx.hash_length());
            ASSERT_NE(nullptr, rx.hash());
            EXPECT_TRUE(std::equal(hash.begin(), hash.end(), rx.hash()));
            EXPECT_EQ(nullptr, rx.hash(hash.size()));
        }
    }
}

// PUBLIC_INTERFACE
/** Chirp setters respect available tailroom and refuse duplicate or null hash input. */
TEST(DppTransportTlvs, ChirpSetterRejectsNullOverflowAndDuplicate)
{
    std::array<uint8_t, 32> buffer{};
    const uint8_t hash[] = {0x00, 0xff, 0x80};
    wfa_map::tlvDppChirpValue tx(buffer.data(), 8);
    ASSERT_TRUE(tx.isInitialized());
    EXPECT_FALSE(tx.set_hash(nullptr, 1));
    EXPECT_FALSE(tx.alloc_hash(4));
    EXPECT_EQ(0U, tx.hash_length());
    ASSERT_TRUE(tx.set_hash(hash, sizeof(hash)));
    EXPECT_FALSE(tx.set_hash(hash, sizeof(hash)));
    EXPECT_EQ(3U, tx.hash_length());
    EXPECT_TRUE(std::equal(std::begin(hash), std::end(hash), tx.hash()));
}

// PUBLIC_INTERFACE
/** Parsers reject missing fixed headers, wrong TLV types, and truncated declared payloads. */
TEST(DppTransportTlvs, ParsersRejectMalformedWireInput)
{
    // Exact wire fixtures avoid making the parser oracle depend on the same serializer.
    const std::vector<std::vector<uint8_t>> direct{
        {0, 0, 0}, {static_cast<uint8_t>(wfa_map::eTlvTypeMap::TLV_DPP_MESSAGE), 0, 2, 0xaa}};
    for (auto wire : direct) {
        wfa_map::tlvDppMessage rx(wire.data(), wire.size(), true);
        EXPECT_FALSE(rx.isInitialized());
    }
    const std::vector<std::vector<uint8_t>> proxied{
        {0, 0, 4, 0, 0, 0, 0},
        {static_cast<uint8_t>(wfa_map::eTlvTypeMap::TLV_1905_ENCAP_DPP), 0, 6, 0, 0, 0, 2, 0xaa},
        {static_cast<uint8_t>(wfa_map::eTlvTypeMap::TLV_1905_ENCAP_DPP), 0, 10, 0x80, 0, 0, 0}};
    for (auto wire : proxied) {
        wfa_map::tlv1905EncapDpp rx(wire.data(), wire.size(), true);
        EXPECT_FALSE(rx.isInitialized());
    }
    const std::vector<std::vector<uint8_t>> chirps{
        {0, 0, 2, 0, 0},
        {static_cast<uint8_t>(wfa_map::eTlvTypeMap::TLV_DPP_CHIRP_VALUE), 0, 4, 0x40, 2, 0xaa}};
    for (auto wire : chirps) {
        wfa_map::tlvDppChirpValue rx(wire.data(), wire.size(), true);
        EXPECT_FALSE(rx.isInitialized());
    }
    std::array<uint8_t, 32> short_header{};
    wfa_map::tlvDppMessage direct_short(short_header.data(), 2, true);
    wfa_map::tlv1905EncapDpp proxied_short(short_header.data(), 6, true);
    wfa_map::tlvDppChirpValue chirp_short(short_header.data(), 4, true);
    EXPECT_FALSE(direct_short.isInitialized());
    EXPECT_FALSE(proxied_short.isInitialized());
    EXPECT_FALSE(chirp_short.isInitialized());
}

// PUBLIC_INTERFACE
/** CCE enable/disable uses a one-byte value; repeated finalization is idempotent. */
TEST(DppTransportTlvs, CceEnableDisableRoundTrip)
{
    for (auto value :
         {wfa_map::tlvDppCceIndication::DISABLE, wfa_map::tlvDppCceIndication::ENABLE}) {
        std::array<uint8_t, 16> buffer{};
        wfa_map::tlvDppCceIndication tx(buffer.data(), buffer.size());
        ASSERT_TRUE(tx.isInitialized());
        tx.advertise_cee() = value;
        EXPECT_EQ(4U, tx.getLen());
        ASSERT_TRUE(tx.finalize());
        const auto once = buffer;
        ASSERT_TRUE(tx.finalize());
        EXPECT_EQ(once, buffer);
        EXPECT_EQ(0, buffer[1]);
        EXPECT_EQ(1, buffer[2]);
        EXPECT_EQ(static_cast<uint8_t>(value), buffer[3]);
        auto wire = buffer;
        wfa_map::tlvDppCceIndication rx(wire.data(), 4, true);
        ASSERT_TRUE(rx.isInitialized());
        EXPECT_EQ(1U, rx.length());
        EXPECT_EQ(value, rx.advertise_cee());
    }
}

// PUBLIC_INTERFACE
/** Bootstrap notifications preserve all three addresses and a URI without a trailing NUL. */
TEST(DppTransportTlvs, BootstrapNotificationRoundTrip)
{
    std::array<uint8_t, 256> buffer{};
    const sMacAddr radio  = {{0x02, 0, 0, 0, 0, 1}};
    const sMacAddr bssid  = {{0x02, 0, 0, 0, 0, 2}};
    const sMacAddr sta    = {{0x02, 0, 0, 0, 0, 3}};
    const std::string uri = "DPP:V:2;K:unit-test-wire-value;;";
    wfa_map::tlvDppBootstrappingUriNotification tx(buffer.data(), buffer.size());
    ASSERT_TRUE(tx.isInitialized());
    tx.ruid()                 = radio;
    tx.bssid()                = bssid;
    tx.backhaul_sta_address() = sta;
    ASSERT_TRUE(tx.set_dpp_uri(uri));
    const size_t length = 21U + uri.size();
    EXPECT_EQ(length, tx.getLen());
    ASSERT_TRUE(tx.finalize());
    auto wire = buffer;
    wfa_map::tlvDppBootstrappingUriNotification rx(wire.data(), length, true);
    ASSERT_TRUE(rx.isInitialized());
    EXPECT_EQ(radio, rx.ruid());
    EXPECT_EQ(bssid, rx.bssid());
    EXPECT_EQ(sta, rx.backhaul_sta_address());
    EXPECT_EQ(uri.size(), rx.dpp_uri_length());
    EXPECT_EQ(uri, rx.dpp_uri_str());
    EXPECT_EQ(nullptr, rx.dpp_uri(uri.size() + 1));
}

// PUBLIC_INTERFACE
/** URI allocation failures leave the notification available for a later valid URI. */
TEST(DppTransportTlvs, BootstrapNotificationRejectsInvalidSetters)
{
    std::array<uint8_t, 64> buffer{};
    wfa_map::tlvDppBootstrappingUriNotification tx(buffer.data(), 24);
    ASSERT_TRUE(tx.isInitialized());
    EXPECT_FALSE(tx.set_dpp_uri(nullptr, 1));
    EXPECT_FALSE(tx.set_dpp_uri("four"));
    EXPECT_EQ(0U, tx.dpp_uri_length());
    ASSERT_TRUE(tx.set_dpp_uri("abc"));
    EXPECT_FALSE(tx.set_dpp_uri("xyz"));
    EXPECT_EQ("abc", tx.dpp_uri_str());
}

// PUBLIC_INTERFACE
/** Fixed-size CCE and variable URI notifications reject wrong types and truncated buffers. */
TEST(DppTransportTlvs, NotificationParsersRejectMalformedInput)
{
    std::array<uint8_t, 32> buffer{};
    wfa_map::tlvDppCceIndication short_cce(buffer.data(), 3, true);
    EXPECT_FALSE(short_cce.isInitialized());
    wfa_map::tlvDppCceIndication wrong_cce(buffer.data(), 4, true);
    EXPECT_FALSE(wrong_cce.isInitialized());
    wfa_map::tlvDppBootstrappingUriNotification short_uri(buffer.data(), 20, true);
    EXPECT_FALSE(short_uri.isInitialized());
    // Three MAC fields plus a declared URI byte that is absent from the buffer.
    buffer.fill(0);
    buffer[0] = static_cast<uint8_t>(wfa_map::eTlvTypeMap::TLV_DPP_BOOTSTRAPPING_URI_NOTIFICATION);
    buffer[2] = 19;
    wfa_map::tlvDppBootstrappingUriNotification truncated_uri(buffer.data(), 21, true);
    EXPECT_FALSE(truncated_uri.isInitialized());
}

} // namespace
