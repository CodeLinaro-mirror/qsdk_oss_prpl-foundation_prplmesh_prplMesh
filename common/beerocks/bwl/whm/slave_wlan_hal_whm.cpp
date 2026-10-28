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

// NOTE: Since *base_wlan_hal_whm* inherits *base_wlan_hal* virtually, we
//       need to explicitly call it from any deriving class.
slave_wlan_hal_whm::slave_wlan_hal_whm(const std::string &iface_name, hal_event_cb_t callback,
                                       const bwl::hal_conf_t &hal_conf)
    : base_wlan_hal(bwl::HALType::Slave, iface_name, IfaceType::Intel, callback, hal_conf),
      base_wlan_hal_whm(bwl::HALType::Slave, iface_name, std::move(callback), hal_conf)
{
    int amx_fd = m_ambiorix_cl.get_fd();
    LOG_IF((amx_fd == -1), FATAL) << "Failed to get amx  fd";
    int amxp_fd = m_ambiorix_cl.get_signal_fd();
    LOG_IF((amxp_fd == -1), FATAL) << "Failed to get amx signal fd";

    m_fds_ext_events = {amx_fd, amxp_fd};
}

slave_wlan_hal_whm::~slave_wlan_hal_whm() { stop_dpp_relay(); }

bool slave_wlan_hal_whm::init_dpp_relay()
{
    // Drop stale subscriptions/path so a pWHM restart can re-arm cleanly.
    stop_dpp_relay();

    if (!m_ambiorix_cl.resolve_path(wbapi_utils::search_path_wifi() + "DPPRelay.", m_dpp_path)) {
        LOG(ERROR) << "DPP: failed to resolve WiFi.DPPRelay. object path";
        return false;
    }

    AmbiorixVariant enable_map(AMXC_VAR_ID_HTABLE);
    enable_map.add_child("RelayEnable", true);
    if (!m_ambiorix_cl.update_object(m_dpp_path, enable_map)) {
        LOG(ERROR) << "DPP: failed to enable relay listener at " << m_dpp_path;
        m_dpp_path.clear();
        return false;
    }

    subscribe_to_dpp_events();
    return true;
}

void slave_wlan_hal_whm::stop_dpp_relay()
{

    if (m_dpp_event_handler && !m_dpp_path.empty()) {
        m_ambiorix_cl.unsubscribe_from_object_event(m_dpp_event_handler);
    }
    m_dpp_event_handler.reset();

    if (m_dpp_state_event_handler && !m_dpp_path.empty()) {
        m_ambiorix_cl.unsubscribe_from_object_event(m_dpp_state_event_handler);
    }
    m_dpp_state_event_handler.reset();

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

void slave_wlan_hal_whm::subscribe_to_dpp_events()
{
    auto callback = [this](AmbiorixVariant &event_data) { on_dpp_ambiorix_event(event_data); };

    // Ambiorix matches event_type exactly — need one handler per notification name.
    m_dpp_event_handler              = std::make_shared<sAmbiorixEventHandler>();
    m_dpp_event_handler->event_type  = "DppFrameReceived";
    m_dpp_event_handler->callback_fn = callback;
    m_ambiorix_cl.subscribe_to_object_event(m_dpp_path, m_dpp_event_handler,
                                            "(path matches '" + m_dpp_path +
                                                "$') && (notification == 'DppFrameReceived')");

    m_dpp_state_event_handler              = std::make_shared<sAmbiorixEventHandler>();
    m_dpp_state_event_handler->event_type  = "DppRelayClientStateChanged";
    m_dpp_state_event_handler->callback_fn = callback;
    m_ambiorix_cl.subscribe_to_object_event(
        m_dpp_path, m_dpp_state_event_handler,
        "(path matches '" + m_dpp_path + "$') && (notification == 'DppRelayClientStateChanged')");

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
    }
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
    const auto opcode = parsed.find(EVENT_KEYLESS_PARAM_OPCODE);
    if (opcode == parsed.end() || opcode->second != "DPP-CHIRP-RX") {
        return;
    }

    const auto src  = parsed.find("src");
    const auto hash = parsed.find("hash");
    if (src == parsed.end() || hash == parsed.end()) {
        LOG(WARNING) << "DPP-CHIRP-RX missing src or hash: " << event_str;
        return;
    }

    sDppChirpEvent chirp{};
    chirp.enrollee_mac = tlvf::mac_from_string(src->second);
    if (std::memcmp(chirp.enrollee_mac.oct, beerocks::net::network_utils::ZERO_MAC.oct,
                    sizeof(chirp.enrollee_mac.oct)) == 0) {
        LOG(WARNING) << "DPP-CHIRP-RX has no usable src: " << src->second;
        return;
    }

    auto hash_bytes = string_utils::hex_to_bytes<std::vector<uint8_t>>(hash->second);
    if (hash_bytes.size() != sizeof(chirp.hash)) {
        LOG(WARNING) << "DPP-CHIRP-RX hash length " << hash_bytes.size() << " from "
                     << hash->second;
        return;
    }
    std::memcpy(chirp.hash, hash_bytes.data(), sizeof(chirp.hash));

    LOG(INFO) << "DPP-CHIRP-RX src=" << tlvf::mac_to_string(chirp.enrollee_mac)
              << " hash=" << hash->second;
    event_queue_push(Event::Dpp_Chirp_Received, std::make_shared<sDppChirpEvent>(chirp));
}

bool slave_wlan_hal_whm::dpp_send_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    auto try_send = [&]() -> bool {
        if (m_dpp_path.empty()) {
            return false;
        }
        AmbiorixVariant args(AMXC_VAR_ID_HTABLE), result;
        args.add_child("tcpType", tcp_type);
        args.add_child("frame", string_utils::bytes_to_hex_string(frame, frame_len));
        return m_ambiorix_cl.call(m_dpp_path, "SendDppFrame", args, result);
    };

    if (try_send()) {
        return true;
    }

    // pWHM may have restarted; refresh path/subscriptions once and retry.
    LOG(WARNING) << "DPP: SendDppFrame failed; re-arming relay and retrying";
    if (!init_dpp_relay()) {
        LOG(ERROR) << "DPP: SendDppFrame failed - re-arm unsuccessful, path='" << m_dpp_path << "'";
        return false;
    }
    if (!try_send()) {
        LOG(ERROR) << "DPP: SendDppFrame failed after re-arm - amxb_fd=" << m_ambiorix_cl.get_fd()
                   << " amxp_fd=" << m_ambiorix_cl.get_signal_fd() << " path=" << m_dpp_path;
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
