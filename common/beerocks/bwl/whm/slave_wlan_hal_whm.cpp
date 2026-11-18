/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "slave_wlan_hal_whm.h"

#include <bcl/beerocks_string_utils.h>
#include <bcl/beerocks_utils.h>
#include <bcl/network/network_utils.h>
#include <easylogging++.h>
#include <tlvf/tlvftypes.h>

#include <cstring>
#include <utility>

using namespace beerocks;
using namespace beerocks::wbapi;

namespace bwl {
namespace whm {

namespace {

// Lookup keys for parse_event() results, built once. parsed_line_t is an
// std::unordered_map<std::string, std::string> and C++14 has no heterogeneous
// lookup, so a literal key would construct a temporary std::string per find().
const std::string k_event_opcode_key(EVENT_KEYLESS_PARAM_OPCODE);
const std::string k_chirp_src_key("src");
const std::string k_chirp_hash_key("hash");
const std::string k_dpp_rx_type_key("type");
// DPP Public Action frame type of a Presence Announcement (Easy Connect Table 31).
const std::string k_dpp_rx_presence_announcement("13");

} // namespace

// NOTE: Since *base_wlan_hal_whm* inherits *base_wlan_hal* virtually, we
//       need to explicitly call it from any deriving class.
slave_wlan_hal_whm::slave_wlan_hal_whm(const std::string &iface_name, hal_event_cb_t callback,
                                       const bwl::hal_conf_t &hal_conf)
    : base_wlan_hal(bwl::HALType::Slave, iface_name, IfaceType::Intel, callback, hal_conf),
      base_wlan_hal_whm(bwl::HALType::Slave, iface_name, std::move(callback), hal_conf)
{
}

slave_wlan_hal_whm::~slave_wlan_hal_whm() { stop_dpp_relay(); }

bool slave_wlan_hal_whm::init_dpp_relay()
{
    // One dedicated client per HAL instance. The Agent removes old FD handlers
    // and destroys the previous instance before attempting a fresh connection.
    if (m_ambiorix_cl.get_fd() < 0 &&
        !m_ambiorix_cl.connect_private(AMBIORIX_USP_BACKEND_PATH,
                                      AMBIORIX_PWHM_USP_BACKEND_URI)) {
        return false;
    }
    const auto bus_fd = m_ambiorix_cl.get_fd();
    const auto signal_fd = m_ambiorix_cl.get_signal_fd();
    if (bus_fd < 0 || signal_fd < 0) {
        return false;
    }
    m_fds_ext_events = {bus_fd, signal_fd};

    if (!m_ambiorix_cl.resolve_path(wbapi_utils::search_path_wifi() + "DPPRelay.", m_dpp_path)) {
        LOG(ERROR) << "DPP: failed to resolve WiFi.DPPRelay. object path";
        return false;
    }

    if (!subscribe_to_dpp_events()) {
        stop_dpp_relay();
        return false;
    }

    AmbiorixVariant enable_map(AMXC_VAR_ID_HTABLE);
    enable_map.add_child("RelayEnable", true);
    if (!m_ambiorix_cl.update_object(m_dpp_path, enable_map)) {
        LOG(ERROR) << "DPP: failed to enable relay listener at " << m_dpp_path;
        stop_dpp_relay();
        return false;
    }

    return refresh_dpp_relay();
}

void slave_wlan_hal_whm::stop_dpp_relay()
{

    if (m_dpp_event_handler && !m_dpp_path.empty()) {
        m_ambiorix_cl.unsubscribe_from_object_event(m_dpp_event_handler);
    }
    m_dpp_event_handler.reset();

    if (m_ap_wpa_ctrl_handler) {
        m_ambiorix_cl.unsubscribe_from_object_event(m_ap_wpa_ctrl_handler);
    }
    m_ap_wpa_ctrl_handler.reset();

    m_dpp_client_connected = false;

    if (!m_dpp_path.empty()) {
        AmbiorixVariant enable_map(AMXC_VAR_ID_HTABLE);
        enable_map.add_child("RelayEnable", false);
        if (!m_ambiorix_cl.update_object(m_dpp_path, enable_map)) {
            LOG(WARNING) << "DPP: failed to disable relay listener at " << m_dpp_path;
        }
    }
    m_dpp_path.clear();
}

bool slave_wlan_hal_whm::subscribe_to_dpp_events()
{
    auto callback = [this](AmbiorixVariant &event_data) { on_dpp_ambiorix_event(event_data); };

    // Ambiorix matches event_type exactly - need one handler per notification name.
    m_dpp_event_handler              = std::make_shared<sAmbiorixEventHandler>();
    m_dpp_event_handler->event_type  = "DppFrameReceived";
    m_dpp_event_handler->callback_fn = callback;
    if (!m_ambiorix_cl.subscribe_to_object_event(
            m_dpp_path, m_dpp_event_handler,
            "(path matches '" + m_dpp_path + "$') && (notification == 'DppFrameReceived')")) {
        m_dpp_event_handler.reset();
        return false;
    }

    // DPP-CHIRP-RX carries the 802.11 source. The TCP frame does not.
    const std::string wifi_ap_path = wbapi_utils::search_path_ap();
    auto wpa_callback = [this](AmbiorixVariant &event_data) { on_ap_wpa_ctrl_event(event_data); };
    m_ap_wpa_ctrl_handler              = std::make_shared<sAmbiorixEventHandler>();
    m_ap_wpa_ctrl_handler->event_type  = AMX_CL_WPA_CTRL_EVT;
    m_ap_wpa_ctrl_handler->callback_fn = wpa_callback;
    const std::string wpa_filter       = "(path matches '" + wifi_ap_path +
                                   "[0-9]+.$') && (notification == '" + AMX_CL_WPA_CTRL_EVT + "')";
    if (!m_ambiorix_cl.subscribe_to_object_event(wifi_ap_path, m_ap_wpa_ctrl_handler, wpa_filter)) {
        LOG(ERROR) << "DPP: failed to subscribe to AccessPoint " << AMX_CL_WPA_CTRL_EVT;
        m_ap_wpa_ctrl_handler.reset();
        return false;
    }
    return true;
}

bool slave_wlan_hal_whm::refresh_dpp_relay()
{
    if (m_dpp_path.empty() || !m_dpp_event_handler || !m_ap_wpa_ctrl_handler) {
        return false;
    }
    auto relay = m_ambiorix_cl.get_object(m_dpp_path);
    bool enabled = false, connected = false;
    if (!relay || !relay->read_child(enabled, "RelayEnable") || !enabled ||
        !relay->read_child(connected, "ClientConnected")) {
        return false;
    }
    if (connected != m_dpp_client_connected) {
        m_dpp_client_connected = connected;
        event_queue_push(connected ? Event::Dpp_Client_Connected : Event::Dpp_Client_Disconnected);
    }
    return true;
}

void slave_wlan_hal_whm::on_dpp_ambiorix_event(AmbiorixVariant &event_data)
{
    std::string state;
    if (event_data.read_child(state, "state")) {
        if (state == "connected") {
            m_dpp_client_connected = true;
            event_queue_push(Event::Dpp_Client_Connected);
        } else if (state == "disconnected") {
            m_dpp_client_connected = false;
            event_queue_push(Event::Dpp_Client_Disconnected);
        }
        return;
    }

    std::string frame_hex;
    uint8_t tcp_type = 0;
    if (!event_data.read_child(frame_hex, "frame") || !event_data.read_child(tcp_type, "tcpType")) {
        return;
    }

    auto frame = string_utils::hex_to_bytes<std::vector<uint8_t>>(frame_hex);
    event_queue_push(Event::Dpp_Frame_Received,
                     std::make_shared<sDppFrameEvent>(sDppFrameEvent{tcp_type, std::move(frame)}));
}

void slave_wlan_hal_whm::on_ap_wpa_ctrl_event(AmbiorixVariant &event_data)
{
    std::string event_str;
    if (!event_data.read_child(event_str, "eventData") || event_str.empty()) {
        return;
    }

    parsed_line_t parsed;
    parse_event(event_str, parsed);
    const auto opcode = parsed.find(k_event_opcode_key);
    if (opcode != parsed.end() && opcode->second == "DPP-RX") {
        on_ap_dpp_rx_event(event_data, parsed);
        return;
    }
    if (opcode == parsed.end() || opcode->second != "DPP-CHIRP-RX") {
        return;
    }

    const auto src_it  = parsed.find(k_chirp_src_key);
    const auto hash_it = parsed.find(k_chirp_hash_key);
    if (src_it == parsed.end() || hash_it == parsed.end()) {
        LOG(WARNING) << "DPP-CHIRP-RX missing src or hash: " << event_str;
        return;
    }

    sDppChirpEvent chirp{};
    chirp.enrollee_mac = tlvf::mac_from_string(src_it->second);
    if (std::memcmp(chirp.enrollee_mac.oct, beerocks::net::network_utils::ZERO_MAC.oct,
                    sizeof(chirp.enrollee_mac.oct)) == 0) {
        LOG(WARNING) << "DPP-CHIRP-RX has no usable src: " << src_it->second;
        return;
    }

    auto hash_bytes = string_utils::hex_to_bytes<std::vector<uint8_t>>(hash_it->second);
    if (hash_bytes.size() != sizeof(chirp.hash)) {
        LOG(WARNING) << "DPP-CHIRP-RX hash length " << hash_bytes.size() << " from "
                     << hash_it->second;
        return;
    }
    std::memcpy(chirp.hash, hash_bytes.data(), sizeof(chirp.hash));

    LOG(DEBUG) << "DPP-CHIRP-RX src=" << tlvf::mac_to_string(chirp.enrollee_mac)
               << " hash=" << hash_it->second;
    event_queue_push(Event::Dpp_Chirp_Received, std::make_shared<sDppChirpEvent>(chirp));
}

void slave_wlan_hal_whm::on_ap_dpp_rx_event(AmbiorixVariant &event_data,
                                            const parsed_line_t &parsed)
{
    // pWHM forwards AccessPoint DPP-RX only for type=13; keep the check for older pWHM.
    const auto type_it = parsed.find(k_dpp_rx_type_key);
    if (type_it == parsed.end() || type_it->second != k_dpp_rx_presence_announcement) {
        return;
    }
    const auto src_it = parsed.find(k_chirp_src_key);
    if (src_it == parsed.end()) {
        return;
    }
    sDppPresenceRxEvent presence{};
    presence.enrollee_mac = tlvf::mac_from_string(src_it->second);
    if (presence.enrollee_mac == beerocks::net::network_utils::ZERO_MAC) {
        return;
    }
    event_data.read_child(presence.ifname, "ifName");
    LOG(DEBUG) << "DPP-RX Presence Announcement src=" << tlvf::mac_to_string(presence.enrollee_mac)
               << " ifname=" << presence.ifname;
    event_queue_push(Event::Dpp_Presence_Rx, std::make_shared<sDppPresenceRxEvent>(presence));
}

bool slave_wlan_hal_whm::dpp_send_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    // Connection state is polled by the Agent every 2s; recovery is never done in here.
    if (m_dpp_path.empty() || !m_dpp_client_connected) {
        return false;
    }
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE), result;
    args.add_child("tcpType", tcp_type);
    args.add_child("frame", string_utils::bytes_to_hex_string(frame, frame_len));
    if (!m_ambiorix_cl.call(m_dpp_path, "SendDppFrame", args, result)) {
        m_dpp_client_connected = false;
        event_queue_push(Event::Dpp_Client_Disconnected);
        LOG(WARNING) << "DPP: SendDppFrame failed; awaiting relay/client recovery";
        return false;
    }
    return true;
}

bool slave_wlan_hal_whm::is_dpp_client_connected() const { return m_dpp_client_connected; }

} // namespace whm

std::shared_ptr<slave_wlan_hal> slave_wlan_hal_create(const std::string &iface_name,
                                                      base_wlan_hal::hal_event_cb_t callback,
                                                      const hal_conf_t &hal_conf)
{
    return std::make_shared<whm::slave_wlan_hal_whm>(iface_name, callback, hal_conf);
}

} // namespace bwl
