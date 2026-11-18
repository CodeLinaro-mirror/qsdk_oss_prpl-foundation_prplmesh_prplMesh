/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../dpp_agent_task.h"
#include "../dpp_relay_frames.h"

#include <bcl/network/network_utils.h>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/tlvftypes.h>
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace beerocks {
namespace {

using namespace dpp_relay;
using Bytes      = std::vector<uint8_t>;
using eFrameType = wfa_map::tlv1905EncapDpp::eFrameType;

constexpr size_t k_buf_len = 1500;

/** DPP Public Action body: OUI 50:6F:9A, OUI type 0x1A, crypto suite 0x01, subtype, attrs. */
Bytes dpp_pa(uint8_t subtype, const Bytes &attrs = {0xAA, 0xBB})
{
    Bytes frame = {0x50, 0x6F, 0x9A, 0x1A, 0x01, subtype};
    frame.insert(frame.end(), attrs.begin(), attrs.end());
    return frame;
}

/** Presence Announcement whose R-Bootstrap Key Hash is 32 bytes of hash_fill. */
Bytes presence_announcement(uint8_t hash_fill)
{
    Bytes attrs = {0x02, 0x10, 0x20, 0x00}; // attribute 0x1002, length 32
    attrs.insert(attrs.end(), DPP_BOOTSTRAP_KEY_HASH_LEN, hash_fill);
    return dpp_pa(DPP_PA_PRESENCE_ANNOUNCEMENT, attrs);
}

Bytes prepend(const Bytes &prefix, const Bytes &body)
{
    Bytes out = prefix;
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

/** Records what DppAgentTask sends to the Controller and to hostapd. */
class FakeDppAgentContext : public DppAgentTaskContext {
public:
    struct HostapdFrame {
        uint8_t tcp_type;
        Bytes body;
    };

    bool send_dpp_cmdu_to_controller(ieee1905_1::CmduMessageTx &cmdu_tx) override
    {
        if (!cmdu_tx.finalize()) {
            return false;
        }
        const uint8_t *msg = cmdu_tx.getMessageBuff();
        controller_cmdus.emplace_back(msg, msg + cmdu_tx.getMessageBuffLength());
        return true;
    }

    bool send_dpp_frame_to_hostapd(uint8_t tcp_type, const uint8_t *frame,
                                   size_t frame_len) override
    {
        if (fail_hostapd_sends > 0) {
            --fail_hostapd_sends;
            return false;
        }
        hostapd_frames.push_back({tcp_type, Bytes(frame, frame + frame_len)});
        return true;
    }

    sMacAddr get_dpp_controller_mac() const override { return controller_mac; }

    std::vector<Bytes> controller_cmdus;
    std::vector<HostapdFrame> hostapd_frames;
    sMacAddr controller_mac = tlvf::mac_from_string("02:00:00:00:00:01");
    int fail_hostapd_sends  = 0;
};

class DppAgentTaskTest : public ::testing::Test {
protected:
    DppAgentTaskTest() : m_cmdu_tx(m_tx_buf, sizeof(m_tx_buf)), m_task(m_ctx, m_cmdu_tx) {}

    void uplink(uint8_t tcp_type, const Bytes &body)
    {
        m_task.on_dpp_frame_received(tcp_type, body.data(), body.size());
    }

    /** Parse the idx-th CMDU sent to the Controller as it would arrive on the wire. */
    ieee1905_1::CmduMessageRx *sent_cmdu(size_t idx)
    {
        if (idx >= m_ctx.controller_cmdus.size()) {
            ADD_FAILURE() << "no CMDU #" << idx << " was sent to the Controller";
            return nullptr;
        }
        const auto &msg = m_ctx.controller_cmdus[idx];
        std::fill(std::begin(m_parse_buf), std::end(m_parse_buf), 0);
        std::copy(msg.begin(), msg.begin() + std::min(msg.size(), k_buf_len), m_parse_buf);
        m_parsed.reset(new ieee1905_1::CmduMessageRx(m_parse_buf, sizeof(m_parse_buf)));
        if (!m_parsed->parse()) {
            ADD_FAILURE() << "CMDU #" << idx << " does not parse";
            return nullptr;
        }
        return m_parsed.get();
    }

    /** Encapsulated frame of the idx-th PROXIED_ENCAP_DPP_MESSAGE sent to the Controller. */
    wfa_map::tlv1905EncapDpp *sent_encap(size_t idx)
    {
        auto cmdu = sent_cmdu(idx);
        if (!cmdu) {
            return nullptr;
        }
        EXPECT_EQ(cmdu->getMessageType(), ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE);
        auto encap = cmdu->getClass<wfa_map::tlv1905EncapDpp>();
        EXPECT_NE(encap, nullptr);
        return encap.get();
    }

    /**
     * Deliver a PROXIED_ENCAP_DPP_MESSAGE to the task as received from src.
     * @return the handle_cmdu() result.
     */
    bool downlink(const Bytes &frame, eFrameType frame_type, bool gas,
                  const sMacAddr *enrollee = nullptr, const sMacAddr *src = nullptr)
    {
        std::fill(std::begin(m_dl_buf), std::end(m_dl_buf), 0);
        ieee1905_1::CmduMessageTx tx(m_dl_buf, sizeof(m_dl_buf));
        if (!tx.create(0x1234, ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE)) {
            ADD_FAILURE() << "create() failed";
            return false;
        }
        auto tlv = tx.addClass<wfa_map::tlv1905EncapDpp>();
        if (!tlv) {
            ADD_FAILURE() << "addClass<tlv1905EncapDpp>() failed";
            return false;
        }
        tlv->frame_flags().dpp_frame_indicator          = gas;
        tlv->frame_flags().enrollee_mac_address_present = enrollee != nullptr;
        if (enrollee && !tlv->set_dest_sta_mac(*enrollee)) {
            ADD_FAILURE() << "set_dest_sta_mac() failed";
            return false;
        }
        tlv->frame_type() = frame_type;
        if (!tlv->set_encapsulated_frame(frame.data(), frame.size()) || !tx.finalize()) {
            ADD_FAILURE() << "building the encapsulated frame failed";
            return false;
        }
        return deliver(src ? *src : m_ctx.controller_mac);
    }

    bool deliver(const sMacAddr &src)
    {
        ieee1905_1::CmduMessageRx rx(m_dl_buf, sizeof(m_dl_buf));
        if (!rx.parse()) {
            ADD_FAILURE() << "downlink CMDU does not parse";
            return false;
        }
        return m_task.handle_cmdu(rx, 0, net::network_utils::ZERO_MAC, src, -1, nullptr);
    }

    uint8_t m_tx_buf[k_buf_len]    = {};
    uint8_t m_dl_buf[k_buf_len]    = {};
    uint8_t m_parse_buf[k_buf_len] = {};
    FakeDppAgentContext m_ctx;
    ieee1905_1::CmduMessageTx m_cmdu_tx;
    DppAgentTask m_task;
    std::unique_ptr<ieee1905_1::CmduMessageRx> m_parsed;

    const sMacAddr m_enrollee = tlvf::mac_from_string("02:11:22:33:44:55");
};

Bytes encap_bytes(wfa_map::tlv1905EncapDpp &encap)
{
    const uint8_t *frame = encap.encapsulated_frame();
    return Bytes(frame, frame + encap.encapsulated_frame_length());
}

} // namespace

// ---------------------------------------------------------------------------
// Uplink: hostapd -> Controller
// ---------------------------------------------------------------------------

// cppcheck does not understand GoogleTest TEST_F() macros.
// cppcheck-suppress syntaxError
TEST_F(DppAgentTaskTest, uplink_auth_response_is_encapsulated_as_public_action)
{
    const Bytes body = dpp_pa(DPP_PA_AUTHENTICATION_RESP);
    uplink(WLAN_PA_VENDOR_SPECIFIC, body);

    ASSERT_EQ(m_ctx.controller_cmdus.size(), 1u);
    auto encap = sent_encap(0);
    ASSERT_NE(encap, nullptr);
    EXPECT_EQ(static_cast<int>(encap->frame_type()),
              static_cast<int>(eFrameType::DPP_AUTHENTICATION_RESPONSE));
    EXPECT_FALSE(bool(encap->frame_flags().dpp_frame_indicator));
    EXPECT_FALSE(bool(encap->frame_flags().enrollee_mac_address_present));
    EXPECT_EQ(encap_bytes(*encap), prepend({WLAN_PA_VENDOR_SPECIFIC}, body));
}

TEST_F(DppAgentTaskTest, uplink_gas_request_keeps_action_byte)
{
    const Bytes body = {0x01, 0x02, 0x03};
    uplink(WLAN_PA_GAS_INITIAL_REQ, body);

    ASSERT_EQ(m_ctx.controller_cmdus.size(), 1u);
    auto encap = sent_encap(0);
    ASSERT_NE(encap, nullptr);
    EXPECT_EQ(static_cast<int>(encap->frame_type()), static_cast<int>(eFrameType::DPP_GAS_FRAME));
    EXPECT_TRUE(bool(encap->frame_flags().dpp_frame_indicator));
    EXPECT_EQ(encap_bytes(*encap), prepend({WLAN_PA_GAS_INITIAL_REQ}, body));
}

TEST_F(DppAgentTaskTest, uplink_drops_invalid_frames)
{
    uplink(WLAN_PA_VENDOR_SPECIFIC, {0x50, 0x6F, 0x9A});                   // too short
    uplink(WLAN_PA_VENDOR_SPECIFIC, {0x50, 0x6F, 0x9A, 0x1A, 0x02, 0x01}); // bad crypto suite
    uplink(0x07, {0x01, 0x02});                                            // not a DPP TCP type

    EXPECT_TRUE(m_ctx.controller_cmdus.empty());
}

TEST_F(DppAgentTaskTest, uplink_duplicate_is_forwarded_once_except_gas_comeback)
{
    const Bytes body = dpp_pa(DPP_PA_AUTHENTICATION_RESP);
    uplink(WLAN_PA_VENDOR_SPECIFIC, body);
    uplink(WLAN_PA_VENDOR_SPECIFIC, body);
    EXPECT_EQ(m_ctx.controller_cmdus.size(), 1u);

    // GAS Comeback Requests repeat with an identical payload by design.
    uplink(WLAN_PA_GAS_COMEBACK_REQ, {0x05});
    uplink(WLAN_PA_GAS_COMEBACK_REQ, {0x05});
    EXPECT_EQ(m_ctx.controller_cmdus.size(), 3u);
}

TEST_F(DppAgentTaskTest, presence_with_known_chirp_sends_chirp_notification_with_mac)
{
    uint8_t hash[DPP_BOOTSTRAP_KEY_HASH_LEN];
    std::memset(hash, 0x5A, sizeof(hash));
    m_task.on_chirp_received(m_enrollee, hash);

    uplink(WLAN_PA_VENDOR_SPECIFIC, presence_announcement(0x5A));

    ASSERT_EQ(m_ctx.controller_cmdus.size(), 1u);
    auto cmdu = sent_cmdu(0);
    ASSERT_NE(cmdu, nullptr);
    EXPECT_EQ(cmdu->getMessageType(), ieee1905_1::eMessageType::CHIRP_NOTIFICATION_MESSAGE);
    auto chirp = cmdu->getClass<wfa_map::tlvDppChirpValue>();
    ASSERT_NE(chirp, nullptr);
    EXPECT_TRUE(bool(chirp->flags().hash_validity));
    ASSERT_TRUE(bool(chirp->flags().enrollee_mac_address_present));
    ASSERT_NE(chirp->dest_sta_mac(), nullptr);
    EXPECT_EQ(*chirp->dest_sta_mac(), m_enrollee);
    ASSERT_EQ(size_t(chirp->hash_length()), DPP_BOOTSTRAP_KEY_HASH_LEN);
    EXPECT_EQ(std::memcmp(chirp->hash(), hash, sizeof(hash)), 0);
}

TEST_F(DppAgentTaskTest, presence_is_held_until_chirp_supplies_mac)
{
    uplink(WLAN_PA_VENDOR_SPECIFIC, presence_announcement(0x33));
    m_task.work(); // well before the 1 s deadline
    EXPECT_TRUE(m_ctx.controller_cmdus.empty());

    uint8_t hash[DPP_BOOTSTRAP_KEY_HASH_LEN];
    std::memset(hash, 0x33, sizeof(hash));
    m_task.on_chirp_received(m_enrollee, hash);

    ASSERT_EQ(m_ctx.controller_cmdus.size(), 1u);
    auto cmdu = sent_cmdu(0);
    ASSERT_NE(cmdu, nullptr);
    auto chirp = cmdu->getClass<wfa_map::tlvDppChirpValue>();
    ASSERT_NE(chirp, nullptr);
    ASSERT_TRUE(bool(chirp->flags().enrollee_mac_address_present));
    EXPECT_EQ(*chirp->dest_sta_mac(), m_enrollee);
}

// ---------------------------------------------------------------------------
// Downlink: Controller -> hostapd
// ---------------------------------------------------------------------------

TEST_F(DppAgentTaskTest, downlink_auth_request_strips_category_and_action)
{
    m_task.on_relay_status(true);
    const Bytes body = dpp_pa(DPP_PA_AUTHENTICATION_REQ);
    ASSERT_TRUE(downlink(prepend({WLAN_ACTION_PUBLIC, WLAN_PA_VENDOR_SPECIFIC}, body),
                         eFrameType::DPP_AUTHENTICATION_REQUEST, false, &m_enrollee));

    ASSERT_EQ(m_ctx.hostapd_frames.size(), 1u);
    EXPECT_EQ(m_ctx.hostapd_frames[0].tcp_type, WLAN_PA_VENDOR_SPECIFIC);
    EXPECT_EQ(m_ctx.hostapd_frames[0].body, body);

    // The Enrollee MAC learned from the downlink is reported on the uplink reply.
    uplink(WLAN_PA_VENDOR_SPECIFIC, dpp_pa(DPP_PA_AUTHENTICATION_RESP));
    ASSERT_EQ(m_ctx.controller_cmdus.size(), 1u);
    auto encap = sent_encap(0);
    ASSERT_NE(encap, nullptr);
    ASSERT_TRUE(bool(encap->frame_flags().enrollee_mac_address_present));
    ASSERT_NE(encap->dest_sta_mac(), nullptr);
    EXPECT_EQ(*encap->dest_sta_mac(), m_enrollee);
}

TEST_F(DppAgentTaskTest, downlink_gas_response_strips_action_byte)
{
    m_task.on_relay_status(true);
    const Bytes body = {0x01, 0x02, 0x03};
    ASSERT_TRUE(downlink(prepend({WLAN_PA_GAS_INITIAL_RESP}, body), eFrameType::DPP_GAS_FRAME, true,
                         &m_enrollee));

    ASSERT_EQ(m_ctx.hostapd_frames.size(), 1u);
    EXPECT_EQ(m_ctx.hostapd_frames[0].tcp_type, WLAN_PA_GAS_INITIAL_RESP);
    EXPECT_EQ(m_ctx.hostapd_frames[0].body, body);
}

TEST_F(DppAgentTaskTest, downlink_is_queued_until_hostapd_connects)
{
    const Bytes auth_req = dpp_pa(DPP_PA_AUTHENTICATION_REQ);
    const Bytes gas_resp = {0x01, 0x02, 0x03};
    ASSERT_TRUE(downlink(prepend({WLAN_PA_VENDOR_SPECIFIC}, auth_req),
                         eFrameType::DPP_AUTHENTICATION_REQUEST, false));
    ASSERT_TRUE(
        downlink(prepend({WLAN_PA_GAS_INITIAL_RESP}, gas_resp), eFrameType::DPP_GAS_FRAME, true));
    EXPECT_TRUE(m_ctx.hostapd_frames.empty());

    m_task.on_relay_status(true);

    ASSERT_EQ(m_ctx.hostapd_frames.size(), 2u);
    EXPECT_EQ(m_ctx.hostapd_frames[0].tcp_type, WLAN_PA_VENDOR_SPECIFIC);
    EXPECT_EQ(m_ctx.hostapd_frames[0].body, auth_req);
    EXPECT_EQ(m_ctx.hostapd_frames[1].tcp_type, WLAN_PA_GAS_INITIAL_RESP);
    EXPECT_EQ(m_ctx.hostapd_frames[1].body, gas_resp);
}

TEST_F(DppAgentTaskTest, downlink_send_failure_is_retried_on_reconnect)
{
    m_task.on_relay_status(true);
    m_ctx.fail_hostapd_sends = 1;
    const Bytes body         = dpp_pa(DPP_PA_AUTHENTICATION_REQ);
    ASSERT_TRUE(downlink(prepend({WLAN_PA_VENDOR_SPECIFIC}, body),
                         eFrameType::DPP_AUTHENTICATION_REQUEST, false));
    EXPECT_TRUE(m_ctx.hostapd_frames.empty());

    m_task.on_relay_status(false);
    m_task.on_relay_status(true);

    ASSERT_EQ(m_ctx.hostapd_frames.size(), 1u);
    EXPECT_EQ(m_ctx.hostapd_frames[0].body, body);
}

TEST_F(DppAgentTaskTest, downlink_from_other_agent_is_ignored)
{
    m_task.on_relay_status(true);
    const sMacAddr other_agent = tlvf::mac_from_string("02:00:00:00:00:99");
    EXPECT_TRUE(downlink(prepend({WLAN_PA_VENDOR_SPECIFIC}, dpp_pa(DPP_PA_AUTHENTICATION_REQ)),
                         eFrameType::DPP_AUTHENTICATION_REQUEST, false, nullptr, &other_agent));

    EXPECT_TRUE(m_ctx.hostapd_frames.empty());
}

TEST_F(DppAgentTaskTest, downlink_uplink_only_frame_is_dropped)
{
    m_task.on_relay_status(true);
    EXPECT_TRUE(downlink(prepend({WLAN_PA_VENDOR_SPECIFIC}, dpp_pa(DPP_PA_AUTHENTICATION_RESP)),
                         eFrameType::DPP_AUTHENTICATION_RESPONSE, false));
    EXPECT_TRUE(downlink({WLAN_PA_GAS_INITIAL_REQ, 0x01}, eFrameType::DPP_GAS_FRAME, true));

    EXPECT_TRUE(m_ctx.hostapd_frames.empty());
}

TEST_F(DppAgentTaskTest, other_messages_are_not_handled)
{
    ieee1905_1::CmduMessageTx tx(m_dl_buf, sizeof(m_dl_buf));
    ASSERT_TRUE(tx.create(0x1234, ieee1905_1::eMessageType::TOPOLOGY_QUERY_MESSAGE));
    ASSERT_TRUE(tx.finalize());

    EXPECT_FALSE(deliver(m_ctx.controller_mac));
    EXPECT_TRUE(m_ctx.hostapd_frames.empty());
    EXPECT_TRUE(m_ctx.controller_cmdus.empty());
}

} // namespace beerocks
