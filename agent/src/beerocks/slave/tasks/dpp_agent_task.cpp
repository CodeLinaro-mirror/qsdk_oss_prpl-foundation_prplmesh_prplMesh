/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2024 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp_agent_task.h"
#include "../agent_db.h"
#include "../son_slave_thread.h"
#include "dpp_relay_frames.h"

#include <bcl/beerocks_logging.h>
#include <bcl/beerocks_string_utils.h>
#include <bcl/network/network_utils.h>
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>

#include <algorithm>
#include <cstring>
#include <functional>

namespace beerocks {

using namespace dpp_relay;

/** AccessPoint DPP-RX type=13 accepted as Enrollee MAC within this window of the chirp. */
static constexpr auto k_presence_mac_window = std::chrono::milliseconds(500);
static constexpr auto k_presence_rx_keep    = std::chrono::seconds(2);
static constexpr size_t k_presence_rx_max   = 16;

/** Identical uplink frames within this window are forwarded once. DPP retries are seconds
 *  apart, so 200 ms should only collapse duplicate events. */
static constexpr auto k_uplink_dedup_window = std::chrono::milliseconds(200);
static constexpr size_t k_uplink_seen_max   = 16;

/** Sent Chirp Notifications: first per hash at INFO, repeats summarised once per interval. */
static constexpr auto k_chirp_log_interval = std::chrono::seconds(60);
static constexpr auto k_chirp_log_forget   = std::chrono::minutes(10);
static constexpr size_t k_chirp_log_max    = 8;

/** Downlink frames held while hostapd is not connected to the relay. */
static constexpr size_t k_downlink_queue_max   = 4;
static constexpr auto k_downlink_queue_max_age = std::chrono::seconds(3);

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

        // Only the Controller sends downlink PROXIED_ENCAP_DPP_MESSAGE to a Proxy Agent.
        // On the Controller+Agent device this Agent shares the AL MAC with the Controller,
        // so it also receives the uplink PROXIED_ENCAP of other Proxy Agents (for example a
        // daisy-chained Agent relaying a second Enrollee). Those belong to the Controller.
        const sMacAddr controller_mac = AgentDB::get()->controller_info.bridge_mac;
        if (controller_mac != net::network_utils::ZERO_MAC && src_mac != controller_mac) {
            LOG(DEBUG) << "DPP: ignoring PROXIED_ENCAP_DPP_MESSAGE from Agent "
                       << tlvf::mac_to_string(src_mac) << ": not from the Controller "
                       << tlvf::mac_to_string(controller_mac) << " (mid=" << std::hex
                       << cmdu_rx.getMessageId() << std::dec << ")";
            return true;
        }

        // Second line of defence: drop frames that only travel Enrollee -> Controller.
        auto encap_tlv = cmdu_rx.getClass<wfa_map::tlv1905EncapDpp>();
        if (encap_tlv && is_uplink_only_frame(encap_tlv->encapsulated_frame(),
                                              encap_tlv->encapsulated_frame_length())) {
            LOG(WARNING) << "DPP: dropping uplink-only frame from the Controller (type=" << std::hex
                         << int(encap_tlv->encapsulated_frame()[0])
                         << ", mid=" << cmdu_rx.getMessageId() << std::dec << ")";
            return true;
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
        m_uplink_seen.clear();
        flush_pending_downlink();
    } else if (!client_connected && was_connected) {
        LOG(INFO) << "DPP: hostapd disconnected from relay";
        m_last_enrollee_mac_valid = false;
        m_uplink_seen.clear();
    }
}

void DppAgentTask::on_relay_stopped()
{
    m_relay_active            = false;
    m_hostapd_connected       = false;
    m_last_enrollee_mac_valid = false;
    m_chirp_macs.clear();
    m_presence_rx.clear();
    m_uplink_seen.clear();
    m_chirp_log.clear();
    m_pending_chirp = false;
    // m_pending_downlink is kept: recovery restarts the relay, and the age
    // bound discards the frames if hostapd does not reconnect in time.
    LOG(INFO) << "DPP: TCP relay stopped; relay can be re-armed";
}

void DppAgentTask::drop_stale_downlink()
{
    auto now = std::chrono::steady_clock::now();
    while (!m_pending_downlink.empty() &&
           now - m_pending_downlink.front().queued > k_downlink_queue_max_age) {
        LOG(WARNING) << "DPP: dropping stale queued downlink frame (mid=" << std::hex
                     << m_pending_downlink.front().mid << std::dec << ")";
        m_pending_downlink.pop_front();
    }
}

void DppAgentTask::queue_downlink_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len,
                                        uint16_t mid)
{
    if (!frame || frame_len == 0) {
        return;
    }
    drop_stale_downlink();
    if (m_pending_downlink.size() >= k_downlink_queue_max) {
        LOG(WARNING) << "DPP: downlink queue full; dropping oldest frame (mid=" << std::hex
                     << m_pending_downlink.front().mid << std::dec << ")";
        m_pending_downlink.pop_front();
    }
    m_pending_downlink.emplace_back();
    auto &entry    = m_pending_downlink.back();
    entry.tcp_type = tcp_type;
    entry.mid      = mid;
    entry.frame.assign(frame, frame + frame_len);
    entry.queued = std::chrono::steady_clock::now();
    LOG(INFO) << "DPP: queued downlink frame for hostapd (mid=" << std::hex << mid << std::dec
              << ", " << frame_len << " bytes, depth " << m_pending_downlink.size() << "/"
              << k_downlink_queue_max << ")";
}

void DppAgentTask::flush_pending_downlink()
{
    drop_stale_downlink();
    while (m_hostapd_connected && !m_pending_downlink.empty()) {
        const auto &entry = m_pending_downlink.front();
        if (!send_dpp_frame(entry.tcp_type, entry.frame.data(), entry.frame.size())) {
            LOG(WARNING) << "DPP: keeping " << m_pending_downlink.size()
                         << " queued frame(s) for next hostapd connect";
            return;
        }
        auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - entry.queued);
        LOG(INFO) << "DPP: relayed queued " << entry.frame.size()
                  << " bytes to hostapd (type=" << std::hex << static_cast<int>(entry.tcp_type)
                  << std::dec << ", mid=" << std::hex << entry.mid << std::dec
                  << ", waited=" << waited.count() << "ms)";
        m_pending_downlink.pop_front();
    }
}

void DppAgentTask::on_chirp_received(const sMacAddr &enrollee_mac, const uint8_t hash[32])
{
    const auto now = std::chrono::steady_clock::now();
    m_chirp_macs.erase(std::remove_if(m_chirp_macs.begin(), m_chirp_macs.end(),
                                      [&](const ChirpMac &entry) {
                                          return now - entry.seen > std::chrono::seconds(5) ||
                                                 std::memcmp(entry.hash, hash, 32) == 0;
                                      }),
                       m_chirp_macs.end());
    if (m_chirp_macs.size() >= 8) {
        m_chirp_macs.erase(m_chirp_macs.begin());
    }
    ChirpMac entry{};
    std::memcpy(entry.hash, hash, 32);
    entry.mac  = enrollee_mac;
    entry.seen = now;
    m_chirp_macs.push_back(entry);

    if (m_pending_chirp && std::memcmp(m_pending_hash, hash, 32) == 0) {
        m_pending_chirp = false;
        send_chirp_notification(hash, &enrollee_mac);
    }
}

void DppAgentTask::on_presence_rx(const sMacAddr &enrollee_mac, const std::string &ifname)
{
    const auto now = std::chrono::steady_clock::now();
    m_presence_rx.erase(std::remove_if(m_presence_rx.begin(), m_presence_rx.end(),
                                       [&](const PresenceRx &entry) {
                                           return now - entry.seen > k_presence_rx_keep;
                                       }),
                        m_presence_rx.end());
    if (m_presence_rx.size() >= k_presence_rx_max) {
        m_presence_rx.erase(m_presence_rx.begin());
    }
    m_presence_rx.push_back(PresenceRx{enrollee_mac, ifname, now});
}

const sMacAddr *
DppAgentTask::find_unique_presence_mac(std::chrono::steady_clock::time_point ref) const
{
    // The relay TCP frame carries no radio, so every AccessPoint of this Agent counts.
    // The MAC is only used when one Enrollee chirped in the window.
    const sMacAddr *found = nullptr;
    for (const auto &entry : m_presence_rx) {
        const auto delta = (entry.seen > ref) ? (entry.seen - ref) : (ref - entry.seen);
        if (delta > k_presence_mac_window) {
            continue;
        }
        if (found && *found != entry.mac) {
            return nullptr;
        }
        found = &entry.mac;
    }
    return found;
}

bool DppAgentTask::is_duplicate_uplink(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    // GAS Comeback Requests repeat with an identical payload by design.
    if (tcp_type == WLAN_PA_GAS_COMEBACK_REQ || !frame) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    m_uplink_seen.erase(std::remove_if(m_uplink_seen.begin(), m_uplink_seen.end(),
                                       [&](const UplinkSeen &entry) {
                                           return now - entry.seen > k_uplink_dedup_window;
                                       }),
                        m_uplink_seen.end());

    uint16_t type = static_cast<uint16_t>(tcp_type) << 8;
    if (tcp_type == WLAN_PA_VENDOR_SPECIFIC && frame_len > DPP_SUBTYPE_OFFSET) {
        type |= frame[DPP_SUBTYPE_OFFSET];
    }
    const sMacAddr mac =
        m_last_enrollee_mac_valid ? m_last_enrollee_mac : net::network_utils::ZERO_MAC;
    const size_t payload_hash =
        std::hash<std::string>{}(std::string(reinterpret_cast<const char *>(frame), frame_len));

    for (const auto &entry : m_uplink_seen) {
        if (entry.type == type && entry.payload_hash == payload_hash && entry.mac == mac) {
            return true;
        }
    }
    if (m_uplink_seen.size() >= k_uplink_seen_max) {
        m_uplink_seen.erase(m_uplink_seen.begin());
    }
    m_uplink_seen.push_back(UplinkSeen{mac, type, payload_hash, now});
    return false;
}
void DppAgentTask::work()
{
    if (!m_pending_chirp) {
        return;
    }
    if (const auto *mac = find_chirp_mac(m_pending_hash)) {
        uint8_t hash[32];
        std::memcpy(hash, m_pending_hash, 32);
        m_pending_chirp = false;
        send_chirp_notification(hash, mac);
        return;
    }
    if (std::chrono::steady_clock::now() >= m_pending_deadline) {
        uint8_t hash[32];
        std::memcpy(hash, m_pending_hash, 32);
        m_pending_chirp = false;
        if (const auto *presence_mac = find_unique_presence_mac(m_pending_since)) {
            const sMacAddr mac = *presence_mac;
            LOG(DEBUG) << "DPP: Enrollee MAC " << tlvf::mac_to_string(mac)
                       << " taken from AccessPoint DPP-RX (no DPP-CHIRP-RX hash match)";
            send_chirp_notification(hash, &mac);
            return;
        }
        LOG(WARNING) << "DPP: Chirp Notification sent without Enrollee MAC; no DPP-CHIRP-RX hash "
                        "match and no unique AccessPoint DPP-RX source";
        send_chirp_notification(hash, nullptr);
    }
}

const sMacAddr *DppAgentTask::find_chirp_mac(const uint8_t hash[32]) const
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_chirp_macs.rbegin(); it != m_chirp_macs.rend(); ++it) {
        if (now - it->seen > std::chrono::seconds(5)) {
            continue;
        }
        if (std::memcmp(it->hash, hash, 32) == 0) {
            return &it->mac;
        }
    }
    return nullptr;
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
              << " (type=" << std::hex << static_cast<int>(tcp_type) << std::dec
              << ", prefix=" << dpp_prefix_hex(frame, frame_len) << ")";
    return true;
}

// ---------------------------------------------------------------------------
// Private: Uplink frame dispatch (hostapd -> Controller)
// ---------------------------------------------------------------------------

void DppAgentTask::dispatch_dpp_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    // A chirping Enrollee sends a Presence Announcement every few seconds; those are traced
    // at debug level by handle_presence_announcement() (same len and prefix).
    const bool is_presence = tcp_type == WLAN_PA_VENDOR_SPECIFIC &&
                             frame_len > DPP_SUBTYPE_OFFSET &&
                             frame[DPP_SUBTYPE_OFFSET] == DPP_PA_PRESENCE_ANNOUNCEMENT;
    LOG_IF(!is_presence, INFO) << "DPP: received TCP frame type=" << std::hex
                               << static_cast<int>(tcp_type) << std::dec << " len=" << frame_len
                               << " prefix=" << dpp_prefix_hex(frame, frame_len);

    if (is_duplicate_uplink(tcp_type, frame, frame_len)) {
        LOG(INFO)
            << "DPP: duplicate uplink frame within "
            << std::chrono::duration_cast<std::chrono::milliseconds>(k_uplink_dedup_window).count()
            << " ms - not forwarded again";
        return;
    }

    if (tcp_type == WLAN_PA_VENDOR_SPECIFIC) {
        // DPP Public Action frame
        // Minimum: OUI(3) + OUIType(1) + CryptoSuite(1) + FrameType(1) = 6 bytes
        if (frame_len < DPP_HDR_LEN) {
            LOG(WARNING) << "DPP: Public Action frame too short (" << frame_len << " bytes)";
            return;
        }

        // Validate DPP OUI header: OUI[0-2]=50:6F:9A, OUI_Type[3]=0x1A, CryptoSuite[4]=0x01
        // Per Wi-Fi Easy Connect section 8.2.1 / EasyMesh section 5.3.4
        static constexpr uint8_t DPP_OUI_EXPECTED[] = {0x50, 0x6F, 0x9A, 0x1A, 0x01};
        if (memcmp(frame, DPP_OUI_EXPECTED, sizeof(DPP_OUI_EXPECTED)) != 0) {
            LOG(WARNING) << "DPP: Public Action frame has invalid OUI header"
                         << " (expected 50:6F:9A 0x1A 0x01)"
                         << " - discarding";
            return;
        }

        uint8_t dpp_subtype = frame[DPP_SUBTYPE_OFFSET];
        LOG_IF(!is_presence, INFO)
            << "DPP: Public Action " << dpp_pa_subtype_name(dpp_subtype) << " subtype=" << std::hex
            << static_cast<int>(dpp_subtype) << std::dec;

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
            LOG(DEBUG) << "DPP: unhandled Public Action subtype=" << std::hex
                       << static_cast<int>(dpp_subtype) << std::dec << " - forwarding as generic";
            send_proxied_encap_dpp(frame, frame_len, false);
            break;
        }

    } else if (tcp_type == WLAN_PA_GAS_INITIAL_REQ || tcp_type == WLAN_PA_GAS_COMEBACK_REQ) {
        // GAS frame (DPP Configuration Request) - keep Action byte for EasyMesh encap.
        handle_gas_request(frame, frame_len, tcp_type);

    } else {
        LOG(WARNING) << "DPP: unknown TCP frame type=" << std::hex << static_cast<int>(tcp_type)
                     << std::dec << " - ignoring";
    }
}

void DppAgentTask::handle_presence_announcement(const uint8_t *frame, size_t frame_len)
{
    LOG(DEBUG) << "DPP: Presence Announcement received len=" << frame_len
               << " prefix=" << dpp_prefix_hex(frame, frame_len);

    uint8_t hash[DPP_BOOTSTRAP_KEY_HASH_LEN] = {};
    if (frame_len <= DPP_HDR_LEN ||
        !find_bootstrap_key_hash(frame + DPP_HDR_LEN, frame_len - DPP_HDR_LEN, hash)) {
        LOG(WARNING) << "DPP: Presence Announcement missing or malformed R_BOOTSTRAP_KEY_HASH";
        return;
    }
    LOG(DEBUG) << "DPP: found R_BOOTSTRAP_KEY_HASH "
               << string_utils::bytes_to_hex_string(hash, sizeof(hash));
    if (const auto *mac = find_chirp_mac(hash)) {
        m_pending_chirp = false;
        send_chirp_notification(hash, mac);
        return;
    }

    std::memcpy(m_pending_hash, hash, sizeof(hash));
    m_pending_chirp    = true;
    m_pending_since    = std::chrono::steady_clock::now();
    m_pending_deadline = m_pending_since + std::chrono::seconds(1);
    LOG(DEBUG) << "DPP: holding Chirp Notification until DPP-CHIRP-RX supplies the Enrollee MAC";
}

void DppAgentTask::send_chirp_notification(const uint8_t hash[32], const sMacAddr *enrollee_mac)
{
    constexpr size_t HASH_LEN = DPP_BOOTSTRAP_KEY_HASH_LEN;

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

    if (enrollee_mac) {
        chirp_tlv->flags().enrollee_mac_address_present = true;
        if (!chirp_tlv->set_dest_sta_mac(*enrollee_mac)) {
            LOG(WARNING) << "DPP: failed to set Chirp Notification Enrollee MAC";
            chirp_tlv->flags().enrollee_mac_address_present = false;
        }
    }

    if (!m_btl_ctx.send_cmdu_to_controller("", m_cmdu_tx)) {
        LOG(ERROR) << "DPP: failed to send CHIRP_NOTIFICATION_MESSAGE to controller";
        return;
    }

    log_chirp_notification_sent(
        hash,
        (enrollee_mac && chirp_tlv->flags().enrollee_mac_address_present) ? enrollee_mac : nullptr);
}

void DppAgentTask::log_chirp_notification_sent(const uint8_t hash[32], const sMacAddr *enrollee_mac)
{
    const auto now            = std::chrono::steady_clock::now();
    const std::string mac_str = enrollee_mac ? tlvf::mac_to_string(*enrollee_mac) : "<none>";

    auto it = std::find_if(m_chirp_log.begin(), m_chirp_log.end(), [&](const ChirpLog &e) {
        return std::memcmp(e.hash, hash, sizeof(e.hash)) == 0;
    });
    if (it != m_chirp_log.end() && now - it->last_seen > k_chirp_log_forget) {
        m_chirp_log.erase(it);
        it = m_chirp_log.end();
    }

    if (it == m_chirp_log.end()) {
        if (m_chirp_log.size() >= k_chirp_log_max) {
            m_chirp_log.erase(std::min_element(
                m_chirp_log.begin(), m_chirp_log.end(),
                [](const ChirpLog &a, const ChirpLog &b) { return a.last_seen < b.last_seen; }));
        }
        ChirpLog entry;
        std::memcpy(entry.hash, hash, sizeof(entry.hash));
        entry.last_report = now;
        entry.last_seen   = now;
        m_chirp_log.push_back(entry);
        LOG(INFO) << "DPP: sent CHIRP_NOTIFICATION_MESSAGE to controller enrollee_mac=" << mac_str
                  << " hash=" << string_utils::bytes_to_hex_string(hash, sizeof(entry.hash))
                  << " (repeats summarised every "
                  << std::chrono::duration_cast<std::chrono::seconds>(k_chirp_log_interval).count()
                  << " s)";
        return;
    }

    it->last_seen = now;
    ++it->repeats;
    if (now - it->last_report < k_chirp_log_interval) {
        LOG(DEBUG) << "DPP: sent CHIRP_NOTIFICATION_MESSAGE to controller enrollee_mac=" << mac_str
                   << " (repeat " << it->repeats << ")";
        return;
    }

    LOG(INFO) << "DPP: sent " << it->repeats << " CHIRP_NOTIFICATION_MESSAGE(s) in "
              << std::chrono::duration_cast<std::chrono::seconds>(now - it->last_report).count()
              << " s for hash " << string_utils::bytes_to_hex_string(hash, sizeof(it->hash))
              << ", last enrollee_mac=" << mac_str;
    it->repeats     = 0;
    it->last_report = now;
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
    LOG(INFO) << "DPP: GAS Configuration Request received len=" << frame_len
              << " action=" << std::hex << static_cast<int>(gas_action) << std::dec
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
              << ", mid=" << std::hex << m_cmdu_tx.getMessageId() << std::dec << ")";
}

// ---------------------------------------------------------------------------
// Private: Downlink frame dispatch (Controller -> hostapd)
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
        // Prefer Action from encap payload; the Controller sends Initial (0x0B).
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
    LOG(INFO) << "DPP: downlink PROXIED_ENCAP mid=" << std::hex << mid << std::dec
              << " tlv_frame_type=" << static_cast<unsigned>(encap_tlv->frame_type())
              << " gas=" << encap_tlv->frame_flags().dpp_frame_indicator
              << " hostapd_connected=" << m_hostapd_connected << " dest_sta=" << dest_sta
              << " rx_len=" << frame_len << " rx_prefix=" << dpp_prefix_hex(frame, frame_len);

    // hostapd TCP type 0x09 already means Public Action Vendor Specific. Payload
    // must start at DPP OUI 50:6F:9A. Controller Auth frames include 802.11
    // Category (0x04) + Action (0x09); EasyMesh "from the Action field" is 0x09
    // only. Strip either prefix. Leaving 0x04 in place makes hostapd close TCP
    // with no DPP-TX.
    const uint8_t *send_frame = frame;
    size_t send_frame_len     = frame_len;
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

        LOG(INFO) << "DPP: sending " << dpp_pa_subtype_name(subtype) << " (subtype=" << std::hex
                  << static_cast<int>(subtype) << std::dec << ") to hostapd"
                  << ", mid=" << std::hex << mid << std::dec << ", tcp_type=" << std::hex
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
            LOG(INFO) << "DPP: sending GAS Response (type=" << std::hex
                      << static_cast<int>(tcp_type) << std::dec << ") to hostapd"
                      << " (stripped Action byte)"
                      << ", mid=" << std::hex << mid << std::dec
                      << ", frame_len=" << send_frame_len;
        } else {
            LOG(WARNING) << "DPP: GAS Response frame doesn't start with expected Action byte"
                         << " (expected " << std::hex << static_cast<int>(tcp_type) << std::dec
                         << ", got " << std::hex << static_cast<int>(frame[0]) << std::dec << ")";
            send_frame     = frame;
            send_frame_len = frame_len;
        }
    }

    if (!m_hostapd_connected || !m_pending_downlink.empty()) {
        // hostapd not on the relay yet, or a backlog exists: queue to keep order.
        queue_downlink_frame(tcp_type, send_frame, send_frame_len, mid);
        flush_pending_downlink();
        return;
    }

    if (!send_dpp_frame(tcp_type, send_frame, send_frame_len)) {
        LOG(ERROR) << "DPP: failed to relay frame to hostapd (mid=" << std::hex << mid << std::dec
                   << "); queued for relay recovery";
        queue_downlink_frame(tcp_type, send_frame, send_frame_len, mid);
        return;
    }

    LOG(INFO) << "DPP: relayed " << send_frame_len << " bytes to hostapd"
              << " (type=" << std::hex << static_cast<int>(tcp_type) << std::dec
              << ", mid=" << std::hex << mid << std::dec << ")";
}

} // namespace beerocks
