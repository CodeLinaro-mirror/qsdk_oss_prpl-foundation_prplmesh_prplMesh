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
#include <bcl/beerocks_string_utils.h>
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>

#include <algorithm>
#include <cstring>

namespace beerocks {

// ---------------------------------------------------------------------------
// DPP TCP wire format constants (from hostapd dpp_tcp.c)
// ---------------------------------------------------------------------------

/** 802.11 Public Action frame type byte (DPP Public Action frames) */
static constexpr uint8_t WLAN_PA_VENDOR_SPECIFIC = 0x09;

/** GAS Initial Request type byte */
static constexpr uint8_t WLAN_PA_GAS_INITIAL_REQ = 0x0A;

/** GAS Initial Response type byte (IEEE 802.11 / Easy Connect Table 61) */
static constexpr uint8_t WLAN_PA_GAS_INITIAL_RESP = 0x0B;

/** GAS Comeback Request type byte (Easy Connect Table 60) */
static constexpr uint8_t WLAN_PA_GAS_COMEBACK_REQ = 0x0C;

/** GAS Comeback Response type byte (Easy Connect Table 63) */
static constexpr uint8_t WLAN_PA_GAS_COMEBACK_RESP = 0x0D;

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

namespace {
std::string dpp_prefix_hex(const uint8_t *p, size_t len)
{
    if (!p || len == 0) {
        return "<empty>";
    }
    constexpr size_t k_prefix = 12;
    return string_utils::bytes_to_hex_string(p, std::min(k_prefix, len));
}

const char *dpp_pa_subtype_name(uint8_t subtype)
{
    switch (subtype) {
    case DPP_PA_AUTHENTICATION_REQ:
        return "Auth Request";
    case DPP_PA_AUTHENTICATION_RESP:
        return "Auth Response";
    case DPP_PA_AUTHENTICATION_CONF:
        return "Auth Confirm";
    case DPP_PA_CONFIGURATION_RESULT:
        return "Config Result";
    case DPP_PA_PRESENCE_ANNOUNCEMENT:
        return "Presence";
    default:
        return "Unknown";
    }
}
} // namespace

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
        LOG(INFO) << "DPP: PROXIED_ENCAP_DPP_MESSAGE received by DppAgentTask"
                  << ", src_mac=" << tlvf::mac_to_string(src_mac)
                  << ", hostapd_connected=" << m_hostapd_connected;

        // Drop uplink-only frames that must never appear in a downlink
        // PROXIED_ENCAP_DPP_MESSAGE from the Controller.
        //
        // Auth Response (DPP subtype 0x01), Configuration Result (subtype 0x0B),
        // GAS Initial Request (0x0A), and GAS Comeback Request (0x0C) are all sent
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

                    // GAS Initial Request (0x0A) and GAS Comeback Request (0x0C)
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

                    // Auth Response (subtype 0x01) and Configuration Result (0x0B)
                    // are uplink-only. Controller frames may start at 0x09 or at 04 09.
                    size_t pa_off = 0;
                    if (type_byte == 0x04 && enc_frame_len > 1 &&
                        enc_frame[1] == WLAN_PA_VENDOR_SPECIFIC) {
                        pa_off = 2;
                    } else if (type_byte == WLAN_PA_VENDOR_SPECIFIC) {
                        pa_off = 1;
                    }
                    if (pa_off != 0 && enc_frame_len > pa_off + DPP_SUBTYPE_OFFSET) {
                        uint8_t subtype = enc_frame[pa_off + DPP_SUBTYPE_OFFSET];
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
        m_last_enrollee_mac_valid = false;
    }
}

void DppAgentTask::on_relay_stopped()
{
    m_relay_active            = false;
    m_hostapd_connected       = false;
    m_last_enrollee_mac_valid = false;
    LOG(INFO) << "DPP: TCP relay stopped; relay can be re-armed";
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

    LOG(INFO) << "DPP: sent " << frame_len << " bytes to hostapd via DPP relay"
              << " (type=0x" << std::hex << static_cast<int>(tcp_type) << std::dec
              << ", frame_len=" << frame_len << ", prefix=" << dpp_prefix_hex(frame, frame_len)
              << ")";
    return true;
}

// ---------------------------------------------------------------------------
// Private: Uplink frame dispatch (hostapd → Controller)
// ---------------------------------------------------------------------------

void DppAgentTask::dispatch_dpp_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    LOG(INFO) << "DPP: received TCP frame type=0x" << std::hex << static_cast<int>(tcp_type)
              << std::dec << " len=" << frame_len << " prefix=" << dpp_prefix_hex(frame, frame_len);

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
        LOG(INFO) << "DPP: Public Action " << dpp_pa_subtype_name(dpp_subtype) << " subtype=0x"
                  << std::hex << static_cast<int>(dpp_subtype) << std::dec;

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
        // GAS frame (DPP Configuration Request) — keep Action byte for EasyMesh encap.
        handle_gas_request(frame, frame_len, tcp_type);

    } else {
        LOG(WARNING) << "DPP: unknown TCP frame type=0x" << std::hex << static_cast<int>(tcp_type)
                     << " — ignoring";
    }
}

void DppAgentTask::handle_presence_announcement(const uint8_t *frame, size_t frame_len)
{
    LOG(INFO) << "DPP: Presence Announcement received len=" << frame_len
              << " prefix=" << dpp_prefix_hex(frame, frame_len);

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
                LOG(INFO) << "DPP: found R_BOOTSTRAP_KEY_HASH "
                          << string_utils::bytes_to_hex_string(hash, HASH_LEN);
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
    LOG(INFO) << "DPP: Authentication Response received len=" << frame_len
              << " prefix=" << dpp_prefix_hex(frame, frame_len);
    send_proxied_encap_dpp(frame, frame_len, false /* Public Action */);
}

void DppAgentTask::handle_config_result(const uint8_t *frame, size_t frame_len)
{
    LOG(INFO) << "DPP: Configuration Result received len=" << frame_len;
    send_proxied_encap_dpp(frame, frame_len, false /* Public Action */);
}

void DppAgentTask::handle_gas_request(const uint8_t *frame, size_t frame_len, uint8_t gas_action)
{
    LOG(INFO) << "DPP: GAS Configuration Request received len=" << frame_len << " action=0x"
              << std::hex << static_cast<int>(gas_action) << std::dec
              << " prefix=" << dpp_prefix_hex(frame, frame_len);
    send_proxied_encap_dpp(frame, frame_len, true /* GAS */, gas_action);
}

void DppAgentTask::send_proxied_encap_dpp(const uint8_t *frame, size_t frame_len, bool is_gas,
                                          uint8_t gas_action)
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

    // EasyMesh tlv1905EncapDpp (!4390 / Table 102): eFrameType is the DPP
    // Public Action subtype (or DPP_GAS_FRAME); PA vs GAS is dpp_frame_indicator.
    if (is_gas) {
        encap_tlv->frame_type() = wfa_map::tlv1905EncapDpp::eFrameType::DPP_GAS_FRAME;
        encap_tlv->frame_flags().dpp_frame_indicator = true;
    } else {
        uint8_t pa_subtype = DPP_PA_AUTHENTICATION_RESP;
        if (frame_len > DPP_SUBTYPE_OFFSET) {
            pa_subtype = frame[DPP_SUBTYPE_OFFSET];
        }
        encap_tlv->frame_type() = static_cast<wfa_map::tlv1905EncapDpp::eFrameType>(pa_subtype);
        encap_tlv->frame_flags().dpp_frame_indicator = false;
    }

    if (m_last_enrollee_mac_valid) {
        encap_tlv->frame_flags().enrollee_mac_address_present = true;
        if (!encap_tlv->set_dest_sta_mac(m_last_enrollee_mac)) {
            LOG(WARNING) << "DPP: failed to set uplink Enrollee MAC; sending without STA MAC";
            encap_tlv->frame_flags().enrollee_mac_address_present = false;
        }
    } else {
        encap_tlv->frame_flags().enrollee_mac_address_present = false;
    }

    // Per EasyMesh / hostapd TCP: GAS body has Action stripped; restore it for 1905.
    // Use the real GAS Action (Initial 0x0A or Comeback 0x0C), not always Initial.
    const uint8_t tcp_prefix = is_gas ? gas_action : WLAN_PA_VENDOR_SPECIFIC;
    if (!encap_tlv->alloc_encapsulated_frame(1 + frame_len)) {
        LOG(ERROR) << "DPP: failed to allocate encapsulated frame in TLV";
        return;
    }
    encap_tlv->encapsulated_frame()[0] = tcp_prefix;
    std::copy(frame, frame + frame_len, encap_tlv->encapsulated_frame() + 1);
    encap_tlv->encapsulated_frame_length() = static_cast<uint16_t>(1 + frame_len);

    if (!m_btl_ctx.send_cmdu_to_controller("", m_cmdu_tx)) {
        LOG(ERROR) << "DPP: failed to send PROXIED_ENCAP_DPP_MESSAGE to controller";
        return;
    }

    LOG(INFO) << "DPP: sent PROXIED_ENCAP_DPP_MESSAGE to controller"
              << " (is_gas=" << is_gas
              << ", tlv_frame_type=" << static_cast<unsigned>(encap_tlv->frame_type())
              << ", encap_frame_len=" << (1 + frame_len)
              << ", prefix=" << dpp_prefix_hex(encap_tlv->encapsulated_frame(), 1 + frame_len)
              << ", enrollee_mac="
              << (m_last_enrollee_mac_valid ? tlvf::mac_to_string(m_last_enrollee_mac) : "<none>")
              << ", mid=0x" << std::hex << m_cmdu_tx.getMessageId() << std::dec << ")";
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

    const uint8_t *frame = encap_tlv->encapsulated_frame();
    size_t frame_len     = encap_tlv->encapsulated_frame_length();

    if (!frame || frame_len == 0) {
        LOG(ERROR) << "DPP: Proxied Encap DPP from controller (mid=" << mid
                   << ") has empty frame body";
        return;
    }

    uint8_t tcp_type = WLAN_PA_VENDOR_SPECIFIC;
    if (encap_tlv->frame_type() == wfa_map::tlv1905EncapDpp::eFrameType::DPP_GAS_FRAME ||
        encap_tlv->frame_flags().dpp_frame_indicator) {
        // Prefer Action from encap payload; Controller Phase 1 sends Initial (0x0B).
        if (frame[0] == WLAN_PA_GAS_INITIAL_RESP || frame[0] == WLAN_PA_GAS_COMEBACK_RESP) {
            tcp_type = frame[0];
        } else {
            tcp_type = WLAN_PA_GAS_INITIAL_RESP;
        }
    }

    std::string dest_sta = "<none>";
    if (encap_tlv->frame_flags().enrollee_mac_address_present) {
        auto dest = encap_tlv->dest_sta_mac();
        if (dest) {
            dest_sta                  = tlvf::mac_to_string(*dest);
            m_last_enrollee_mac       = *dest;
            m_last_enrollee_mac_valid = true;
        }
    }
    LOG(INFO) << "DPP: downlink PROXIED_ENCAP mid=0x" << std::hex << mid << std::dec
              << " tlv_frame_type=" << static_cast<unsigned>(encap_tlv->frame_type())
              << " gas=" << encap_tlv->frame_flags().dpp_frame_indicator
              << " hostapd_connected=" << m_hostapd_connected << " dest_sta=" << dest_sta
              << " rx_len=" << frame_len << " rx_prefix=" << dpp_prefix_hex(frame, frame_len);

    // hostapd TCP type 0x09 already means Public Action Vendor Specific. Payload
    // must start at DPP OUI 50:6F:9A. Controller Auth frames include 802.11
    // Category (0x04) + Action (0x09); EasyMesh "from the Action field" is 0x09
    // only. Strip either prefix. Leaving 0x04 in place makes hostapd close TCP
    // with no DPP-TX.
    const uint8_t *send_frame                   = frame;
    size_t send_frame_len                       = frame_len;
    static constexpr uint8_t WLAN_ACTION_PUBLIC = 0x04;
    if (tcp_type == WLAN_PA_VENDOR_SPECIFIC && send_frame_len >= 2 &&
        send_frame[0] == WLAN_ACTION_PUBLIC && send_frame[1] == WLAN_PA_VENDOR_SPECIFIC) {
        LOG(DEBUG) << "DPP: stripping Category+Action (0x04 0x09) from encapsulated frame"
                   << " before sending to hostapd";
        send_frame += 2;
        send_frame_len -= 2;
    } else if (tcp_type == WLAN_PA_VENDOR_SPECIFIC && send_frame_len > 1 &&
               send_frame[0] == WLAN_PA_VENDOR_SPECIFIC) {
        LOG(DEBUG) << "DPP: stripping leading Action byte (0x09) from encapsulated frame"
                   << " before sending to hostapd";
        send_frame += 1;
        send_frame_len -= 1;
    }

    if (tcp_type == WLAN_PA_VENDOR_SPECIFIC && send_frame_len > DPP_HDR_LEN) {
        uint8_t subtype = send_frame[DPP_SUBTYPE_OFFSET];

        LOG(INFO) << "DPP: sending " << dpp_pa_subtype_name(subtype) << " (subtype=0x" << std::hex
                  << static_cast<int>(subtype) << std::dec << ") to hostapd"
                  << ", mid=0x" << std::hex << mid << std::dec << ", tcp_type=0x"
                  << static_cast<int>(tcp_type) << std::dec << ", frame_len=" << send_frame_len
                  << ", send_prefix=" << dpp_prefix_hex(send_frame, send_frame_len);
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

    LOG(INFO) << "DPP: relayed " << send_frame_len << " bytes to hostapd"
              << " (type=0x" << std::hex << static_cast<int>(tcp_type) << std::dec
              << ", mid=" << mid << ")";
}

} // namespace beerocks
