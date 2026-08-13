/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2024 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_agent_task.h"
#include "../son_slave_thread.h"

#include <bcl/beerocks_logging.h>
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace beerocks {

// ---------------------------------------------------------------------------
// DPP TCP wire format constants (from hostapd dpp_tcp.c)
// ---------------------------------------------------------------------------

/** 802.11 Public Action frame type byte (DPP Public Action frames) */
static constexpr uint8_t WLAN_PA_VENDOR_SPECIFIC = 0x09;

/** GAS Initial Request type byte */
static constexpr uint8_t WLAN_PA_GAS_INITIAL_REQ = 0x0A;

/** GAS Initial Response type byte */
static constexpr uint8_t WLAN_PA_GAS_INITIAL_RESP = 0x0B;

/** GAS Comeback Response type byte */
static constexpr uint8_t WLAN_PA_GAS_COMEBACK_RESP = 0x0C;

/** GAS Comeback Request type byte */
static constexpr uint8_t WLAN_PA_GAS_COMEBACK_REQ = 0x0D;

// ---------------------------------------------------------------------------
// DPP Public Action frame layout (after TCP header strip)
// Offset 0-2: OUI = 50:6F:9A
// Offset 3:   OUI Type = 0x1A (DPP)
// Offset 4:   Crypto Suite = 0x01
// Offset 5:   DPP Frame Type (subtype)
// Offset 6+:  DPP Attributes (TLV-encoded)
// ---------------------------------------------------------------------------

static constexpr size_t DPP_HDR_LEN        = 6; ///< OUI(3)+OUIType(1)+CryptoSuite(1)+FrameType(1)
static constexpr size_t DPP_SUBTYPE_OFFSET = 5; ///< Byte index of DPP Frame Type in frame body

/** DPP Public Action frame subtypes (from hostapd dpp.h enum dpp_public_action_frame_type) */
static constexpr uint8_t DPP_PA_AUTHENTICATION_REQ    = 0x00;
static constexpr uint8_t DPP_PA_AUTHENTICATION_RESP   = 0x01; ///< NOT 0x02 — 0x02 is Auth Confirm
static constexpr uint8_t DPP_PA_AUTHENTICATION_CONF   = 0x02;
static constexpr uint8_t DPP_PA_CONFIGURATION_RESULT  = 0x0B; ///< 11
static constexpr uint8_t DPP_PA_PRESENCE_ANNOUNCEMENT = 0x0D; ///< 13

// ---------------------------------------------------------------------------
// DPP Attribute TLV format (little-endian IDs and lengths)
// [2 bytes ID LE][2 bytes Length LE][N bytes Value]
// ---------------------------------------------------------------------------

static constexpr uint16_t DPP_ATTR_R_BOOTSTRAP_KEY_HASH = 0x1002;

DppAgentTask::DppAgentTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::DPP_AGENT), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx)
{
}

// ---------------------------------------------------------------------------
// Public: handle_cmdu (downlink from Controller)
// ---------------------------------------------------------------------------

bool DppAgentTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t /*iface_index*/,
                               const sMacAddr & /*dst_mac*/, const sMacAddr &src_mac, int /*fd*/,
                               std::shared_ptr<beerocks_header> /*beerocks_header*/)
{
    auto message_type = cmdu_rx.getMessageType();

    switch (message_type) {
    case ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE: {
        LOG(DEBUG) << "DPP: PROXIED_ENCAP_DPP_MESSAGE received by DppAgentTask"
                   << ", src_mac=" << tlvf::mac_to_string(src_mac)
                   << ", hostapd_connected=" << m_hostapd_connected;

        // Drop uplink-only frames that must never appear in a downlink
        // PROXIED_ENCAP_DPP_MESSAGE from the Controller.
        //
        // Auth Response (DPP subtype 0x01), Configuration Result (subtype 0x0B),
        // GAS Initial Request (0x0A), and GAS Comeback Request (0x0D) are all sent
        // FROM the Enrollee TO the Controller.  If the Controller has no active
        // session it may echo one of these frames back; discard it here before
        // any further processing.
        {
            auto encap_tlv_drop = cmdu_rx.getClass<wfa_map::tlv1905EncapDpp>();
            if (encap_tlv_drop) {
                const uint8_t *enc_frame = encap_tlv_drop->encapsulated_frame();
                size_t enc_frame_len     = encap_tlv_drop->encapsulated_frame_length();
                if (enc_frame && enc_frame_len > 0) {
                    uint8_t type_byte = enc_frame[0];

                    // GAS Initial Request (0x0A) and GAS Comeback Request (0x0D)
                    // are uplink-only — drop them.
                    if (type_byte == WLAN_PA_GAS_INITIAL_REQ ||
                        type_byte == WLAN_PA_GAS_COMEBACK_REQ) {
                        LOG(WARNING)
                            << "DPP: handle_cmdu: dropping downlink GAS request (type=0x"
                            << std::hex << static_cast<int>(type_byte) << std::dec
                            << ") — GAS requests are uplink-only"
                            << " (mid=0x" << std::hex << cmdu_rx.getMessageId() << std::dec << ")";
                        return true;
                    }

                    // Auth Response (DPP Public Action subtype 0x01) and Configuration
                    // Result (subtype 0x0B) are uplink-only — drop them.
                    if (type_byte == WLAN_PA_VENDOR_SPECIFIC &&
                        enc_frame_len > 1 + DPP_SUBTYPE_OFFSET) {
                        uint8_t subtype = enc_frame[1 + DPP_SUBTYPE_OFFSET];
                        if (subtype == DPP_PA_AUTHENTICATION_RESP) {
                            LOG(WARNING) << "DPP: handle_cmdu: dropping downlink Auth Response"
                                         << " (subtype=0x01) — Auth Response is uplink-only"
                                         << " (mid=0x" << std::hex << cmdu_rx.getMessageId()
                                         << std::dec << ")";
                            return true;
                        }
                        if (subtype == DPP_PA_CONFIGURATION_RESULT) {
                            LOG(WARNING)
                                << "DPP: handle_cmdu: dropping downlink Configuration Result"
                                << " (subtype=0x0B) — Configuration Result is uplink-only"
                                << " (mid=0x" << std::hex << cmdu_rx.getMessageId() << std::dec
                                << ")";
                            return true;
                        }
                    }
                }
            }
        }

        handle_proxied_encap_dpp_from_controller(cmdu_rx);
        return true;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Public: relay state notifications from slave_thread's slave_wlan_hal DPP relay
// ---------------------------------------------------------------------------

void DppAgentTask::on_dpp_frame_received(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    dispatch_dpp_frame(tcp_type, frame, frame_len);
}

void DppAgentTask::on_relay_status(bool client_connected)
{
    m_relay_active      = true;
    bool was_connected  = m_hostapd_connected;
    m_hostapd_connected = client_connected;

    if (client_connected && !was_connected) {
        LOG(INFO) << "DPP: hostapd connected to relay";
    } else if (!client_connected && was_connected) {
        LOG(INFO) << "DPP: hostapd disconnected from relay";
    }
}

bool DppAgentTask::send_dpp_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    if (!m_hostapd_connected) {
        LOG(ERROR) << "DPP: send_dpp_frame() called but hostapd is not connected to the relay";
        return false;
    }

    auto slave_wlan_hal = m_btl_ctx.get_slave_wlan_hal();
    if (!slave_wlan_hal) {
        LOG(ERROR) << "DPP: send_dpp_frame() called but slave_wlan_hal is not started";
        return false;
    }

    if (!slave_wlan_hal->dpp_send_frame(tcp_type, frame, frame_len)) {
        LOG(ERROR) << "DPP: slave_wlan_hal failed to send frame to hostapd";
        return false;
    }

    LOG(DEBUG) << "DPP: sent " << frame_len << " bytes to hostapd via DPP relay"
               << " (type=0x" << std::hex << static_cast<int>(tcp_type) << std::dec
               << ", frame_len=" << frame_len << ")";
    return true;
}

// ---------------------------------------------------------------------------
// Private: Uplink frame dispatch (hostapd → Controller)
// ---------------------------------------------------------------------------

void DppAgentTask::dispatch_dpp_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    LOG(DEBUG) << "DPP: received TCP frame type=0x" << std::hex << static_cast<int>(tcp_type)
               << std::dec << " len=" << frame_len;

    if (tcp_type == WLAN_PA_VENDOR_SPECIFIC) {
        // DPP Public Action frame
        // Minimum: OUI(3) + OUIType(1) + CryptoSuite(1) + FrameType(1) = 6 bytes
        if (frame_len < DPP_HDR_LEN) {
            LOG(WARNING) << "DPP: Public Action frame too short (" << frame_len << " bytes)";
            return;
        }

        // Validate DPP OUI header: OUI[0-2]=50:6F:9A, OUI_Type[3]=0x1A, CryptoSuite[4]=0x01
        // Per Wi-Fi Easy Connect §8.2.1 / EasyMesh §5.3.4
        static constexpr uint8_t DPP_OUI_EXPECTED[] = {0x50, 0x6F, 0x9A, 0x1A, 0x01};
        if (memcmp(frame, DPP_OUI_EXPECTED, sizeof(DPP_OUI_EXPECTED)) != 0) {
            LOG(WARNING) << "DPP: Public Action frame has invalid OUI header"
                         << " (expected 50:6F:9A 0x1A 0x01)"
                         << " — discarding";
            return;
        }

        uint8_t dpp_subtype = frame[DPP_SUBTYPE_OFFSET];
        LOG(DEBUG) << "DPP: Public Action subtype=0x" << std::hex << static_cast<int>(dpp_subtype);

        switch (dpp_subtype) {
        case DPP_PA_PRESENCE_ANNOUNCEMENT:
            handle_presence_announcement(frame, frame_len);
            break;
        case DPP_PA_AUTHENTICATION_RESP:
            handle_auth_response(frame, frame_len);
            break;
        case DPP_PA_CONFIGURATION_RESULT:
            handle_config_result(frame, frame_len);
            break;
        default:
            LOG(DEBUG) << "DPP: unhandled Public Action subtype=0x" << std::hex
                       << static_cast<int>(dpp_subtype) << " — forwarding as generic";
            send_proxied_encap_dpp(frame, frame_len, false);
            break;
        }

    } else if (tcp_type == WLAN_PA_GAS_INITIAL_REQ || tcp_type == WLAN_PA_GAS_COMEBACK_REQ) {
        // GAS frame (DPP Configuration Request)
        handle_gas_request(frame, frame_len);

    } else {
        LOG(WARNING) << "DPP: unknown TCP frame type=0x" << std::hex << static_cast<int>(tcp_type)
                     << " — ignoring";
    }
}

void DppAgentTask::handle_presence_announcement(const uint8_t *frame, size_t frame_len)
{
    LOG(DEBUG) << "DPP: Presence Announcement received";

    constexpr size_t HASH_LEN = 32;
    uint8_t hash[HASH_LEN]    = {};
    bool hash_found           = false;

    if (frame_len > DPP_HDR_LEN) {
        const uint8_t *attrs     = frame + DPP_HDR_LEN;
        size_t attrs_len         = frame_len - DPP_HDR_LEN;
        const uint8_t *pos       = attrs;
        const uint8_t *attrs_end = attrs + attrs_len;

        while (pos + 4 <= attrs_end) {
            uint16_t attr_id = static_cast<uint16_t>(pos[0]) | (static_cast<uint16_t>(pos[1]) << 8);
            uint16_t attr_len =
                static_cast<uint16_t>(pos[2]) | (static_cast<uint16_t>(pos[3]) << 8);
            pos += 4;

            if (pos + attr_len > attrs_end) {
                LOG(WARNING) << "DPP: attribute length exceeds frame boundary";
                break;
            }

            if (attr_id == DPP_ATTR_R_BOOTSTRAP_KEY_HASH && attr_len == HASH_LEN) {
                std::memcpy(hash, pos, HASH_LEN);
                hash_found = true;
                LOG(DEBUG) << "DPP: found R_BOOTSTRAP_KEY_HASH in Presence Announcement";
                break;
            }

            pos += attr_len;
        }
    }

    if (!hash_found) {
        LOG(WARNING) << "DPP: Presence Announcement missing R_BOOTSTRAP_KEY_HASH attribute";
        return;
    }

    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::CHIRP_NOTIFICATION_MESSAGE)) {
        LOG(ERROR) << "DPP: failed to create CHIRP_NOTIFICATION_MESSAGE CMDU";
        return;
    }

    auto chirp_tlv = m_cmdu_tx.addClass<wfa_map::tlvDppChirpValue>();
    if (!chirp_tlv) {
        LOG(ERROR) << "DPP: failed to add tlvDppChirpValue";
        return;
    }

    chirp_tlv->flags().hash_validity                = true;
    chirp_tlv->flags().enrollee_mac_address_present = false;

    if (!chirp_tlv->set_hash(hash, HASH_LEN)) {
        LOG(ERROR) << "DPP: failed to set hash in tlvDppChirpValue";
        return;
    }
    chirp_tlv->hash_length() = HASH_LEN;

    if (!m_btl_ctx.send_cmdu_to_controller("", m_cmdu_tx)) {
        LOG(ERROR) << "DPP: failed to send CHIRP_NOTIFICATION_MESSAGE to controller";
        return;
    }

    LOG(INFO) << "DPP: sent CHIRP_NOTIFICATION_MESSAGE to controller";
}

void DppAgentTask::handle_auth_response(const uint8_t *frame, size_t frame_len)
{
    LOG(DEBUG) << "DPP: Authentication Response received";
    send_proxied_encap_dpp(frame, frame_len, false /* Public Action */);
}

void DppAgentTask::handle_config_result(const uint8_t *frame, size_t frame_len)
{
    LOG(DEBUG) << "DPP: Configuration Result received";
    send_proxied_encap_dpp(frame, frame_len, false /* Public Action */);
}

void DppAgentTask::handle_gas_request(const uint8_t *frame, size_t frame_len)
{
    LOG(DEBUG) << "DPP: GAS Configuration Request received";
    send_proxied_encap_dpp(frame, frame_len, true /* GAS */);
}

void DppAgentTask::send_proxied_encap_dpp(const uint8_t *frame, size_t frame_len, bool is_gas)
{
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE)) {
        LOG(ERROR) << "DPP: failed to create PROXIED_ENCAP_DPP_MESSAGE CMDU";
        return;
    }

    auto encap_tlv = m_cmdu_tx.addClass<wfa_map::tlv1905EncapDpp>();
    if (!encap_tlv) {
        LOG(ERROR) << "DPP: failed to add tlv1905EncapDpp";
        return;
    }

    if (is_gas) {
        encap_tlv->frame_type() = static_cast<wfa_map::tlv1905EncapDpp::eFrameType>(0xFF);
        encap_tlv->frame_flags().dpp_frame_indicator = true;
    } else {
        encap_tlv->frame_type() = static_cast<wfa_map::tlv1905EncapDpp::eFrameType>(0x01);
        encap_tlv->frame_flags().dpp_frame_indicator = false;
    }

    encap_tlv->frame_flags().enrollee_mac_address_present = false;

    // Per EasyMesh spec and hostapd TCP protocol:
    // - hostapd sends GAS frames over TCP with the Action byte (0x0A) already stripped
    // - The TCP frame from hostapd starts directly with the dialog token
    // - For 1905 encapsulation, we need to prepend the Action byte back
    // - The 1905 frame should be: Action(0x0A) + dialog_token + rest_of_frame
    const uint8_t tcp_prefix = is_gas ? WLAN_PA_GAS_INITIAL_REQ : WLAN_PA_VENDOR_SPECIFIC;
    if (!encap_tlv->alloc_encapsulated_frame(1 + frame_len)) {
        LOG(ERROR) << "DPP: failed to allocate encapsulated frame in TLV";
        return;
    }
    encap_tlv->encapsulated_frame()[0] = tcp_prefix;
    std::copy(frame, frame + frame_len, encap_tlv->encapsulated_frame() + 1);
    encap_tlv->encapsulated_frame_length() = static_cast<uint16_t>(1 + frame_len);

    // Debug: Print first 16 bytes of encapsulated frame
    {
        std::ostringstream hex_dump;
        const uint8_t *enc_frame = encap_tlv->encapsulated_frame();
        size_t dump_len = std::min(static_cast<size_t>(16), static_cast<size_t>(1 + frame_len));
        for (size_t i = 0; i < dump_len; ++i) {
            hex_dump << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(enc_frame[i]) << " ";
        }
        LOG(DEBUG) << "DPP: agent sending to controller, first " << dump_len
                   << " bytes: " << hex_dump.str();
    }

    if (!m_btl_ctx.send_cmdu_to_controller("", m_cmdu_tx)) {
        LOG(ERROR) << "DPP: failed to send PROXIED_ENCAP_DPP_MESSAGE to controller";
        return;
    }

    LOG(DEBUG) << "DPP: sent PROXIED_ENCAP_DPP_MESSAGE to controller"
               << " (is_gas=" << is_gas << ", encap_frame_len=" << (1 + frame_len) << ", mid=0x"
               << std::hex << m_cmdu_tx.getMessageId() << std::dec << ")";
}

// ---------------------------------------------------------------------------
// Private: Downlink frame dispatch (Controller → hostapd)
// ---------------------------------------------------------------------------

void DppAgentTask::handle_proxied_encap_dpp_from_controller(ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto mid       = cmdu_rx.getMessageId();
    auto encap_tlv = cmdu_rx.getClass<wfa_map::tlv1905EncapDpp>();
    if (!encap_tlv) {
        LOG(ERROR) << "DPP: Proxied Encap DPP from controller (mid=" << mid
                   << ") missing tlv1905EncapDpp (0xCD)";
        return;
    }

    uint8_t tcp_type;
    if (encap_tlv->frame_type() == static_cast<wfa_map::tlv1905EncapDpp::eFrameType>(0xFF)) {
        tcp_type = WLAN_PA_GAS_INITIAL_RESP;
    } else {
        tcp_type = WLAN_PA_VENDOR_SPECIFIC;
    }

    const uint8_t *frame = encap_tlv->encapsulated_frame();
    size_t frame_len     = encap_tlv->encapsulated_frame_length();

    if (!frame || frame_len == 0) {
        LOG(ERROR) << "DPP: Proxied Encap DPP from controller (mid=" << mid
                   << ") has empty frame body";
        return;
    }

    // The 1905 TLV's encapsulated frame for DPP_PUBLIC_ACTION_FRAME starts with
    // the 802.11 Action byte (0x09 = Vendor Specific), per the Wi-Fi Easy Connect
    // spec ("starting from the Action field"). For GAS frames, keep the TCP type
    // byte (0x0a/0x0d) in the encapsulated payload so the full hostapd framing
    // is preserved.
    const uint8_t *send_frame = frame;
    size_t send_frame_len     = frame_len;
    if (tcp_type == WLAN_PA_VENDOR_SPECIFIC && send_frame_len > 1 &&
        send_frame[0] == WLAN_PA_VENDOR_SPECIFIC) {
        LOG(DEBUG) << "DPP: stripping leading Action byte (0x09) from encapsulated frame"
                   << " before sending to hostapd";
        send_frame     = frame + 1;
        send_frame_len = frame_len - 1;
    }

    // Log the specific DPP frame subtype being sent to hostapd for easier tracing.
    // IMPORTANT: Auth Response (0x01) is an UPLINK-only frame — it is sent FROM the Enrollee
    // TO the Controller. The Controller must never send an Auth Response back downlink to the
    // Enrollee. If we receive an Auth Response in a downlink PROXIED_ENCAP_DPP_MESSAGE, it
    // means the Controller has no active session and is echoing the frame back. Discard it.
    if (tcp_type == WLAN_PA_VENDOR_SPECIFIC && send_frame_len > DPP_HDR_LEN) {
        uint8_t subtype          = send_frame[DPP_SUBTYPE_OFFSET];
        const char *subtype_name = "Unknown";
        switch (subtype) {
        case DPP_PA_AUTHENTICATION_REQ:
            subtype_name = "Auth Request";
            break;
        case DPP_PA_AUTHENTICATION_RESP:
            subtype_name = "Auth Response";
            break;
        case DPP_PA_AUTHENTICATION_CONF:
            subtype_name = "Auth Confirm";
            break;
        default:
            break;
        }

        // Auth Response (0x01) must never be sent downlink to the Enrollee.
        // This can happen when the Controller has no active session and the message
        // is routed to a secondary handler that echoes it back. Discard it.
        if (subtype == DPP_PA_AUTHENTICATION_RESP) {
            LOG(WARNING) << "DPP: discarding downlink Auth Response (subtype=0x01) — "
                         << "Auth Response is uplink-only; Controller has no active session "
                         << "(mid=0x" << std::hex << mid << std::dec << ")";
            return;
        }

        LOG(INFO) << "DPP: sending " << subtype_name << " (subtype=0x" << std::hex
                  << static_cast<int>(subtype) << std::dec << ") to hostapd"
                  << ", mid=0x" << std::hex << mid << std::dec << ", frame_len=" << send_frame_len;
    } else if (tcp_type == WLAN_PA_GAS_INITIAL_RESP || tcp_type == WLAN_PA_GAS_COMEBACK_RESP) {
        // Per EasyMesh spec and hostapd TCP protocol:
        // - The 1905 encapsulated GAS Response starts with Action byte (0x0B)
        // - hostapd TCP protocol expects GAS frames WITHOUT the Action byte
        // - Strip the Action byte before forwarding to hostapd
        if (frame_len > 1 && frame[0] == tcp_type) {
            send_frame     = frame + 1;
            send_frame_len = frame_len - 1;
            LOG(INFO) << "DPP: sending GAS Response (type=0x" << std::hex
                      << static_cast<int>(tcp_type) << std::dec << ") to hostapd"
                      << " (stripped Action byte)"
                      << ", mid=0x" << std::hex << mid << std::dec
                      << ", frame_len=" << send_frame_len;
        } else {
            LOG(WARNING) << "DPP: GAS Response frame doesn't start with expected Action byte"
                         << " (expected 0x" << std::hex << static_cast<int>(tcp_type) << std::dec
                         << ", got 0x" << std::hex << static_cast<int>(frame[0]) << std::dec << ")";
            send_frame     = frame;
            send_frame_len = frame_len;
        }
    }

    if (!send_dpp_frame(tcp_type, send_frame, send_frame_len)) {
        LOG(ERROR) << "DPP: failed to relay frame to hostapd (mid=" << mid << ")";
        return;
    }

    LOG(DEBUG) << "DPP: relayed " << send_frame_len << " bytes to hostapd"
               << " (type=0x" << std::hex << static_cast<int>(tcp_type) << std::dec
               << ", mid=" << mid << ")";
}

} // namespace beerocks
