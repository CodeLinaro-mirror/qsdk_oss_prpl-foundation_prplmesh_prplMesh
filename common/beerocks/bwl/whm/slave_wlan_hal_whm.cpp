/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "slave_wlan_hal_whm.h"

#include <bcl/beerocks_string_utils.h>
#include <bcl/beerocks_utils.h>
#include <bcl/network/network_utils.h>
#include <easylogging++.h>

using namespace beerocks;
using namespace beerocks::wbapi;

namespace bwl {
namespace whm {

// NOTE: Since *base_wlan_hal_whm* inherits *base_wlan_hal* virtually, we
//       need to explicitly call it from any deriving class.
slave_wlan_hal_whm::slave_wlan_hal_whm(const std::string &iface_name, hal_event_cb_t callback,
                                       const bwl::hal_conf_t &hal_conf)
    : base_wlan_hal(bwl::HALType::Slave, iface_name, IfaceType::Intel, callback, hal_conf),
      base_wlan_hal_whm(bwl::HALType::Slave, iface_name, callback, hal_conf)
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
    if (m_dpp_path.empty() &&
        !m_ambiorix_cl.resolve_path(wbapi_utils::search_path_wifi() + "DPPRelay.", m_dpp_path)) {
        LOG(ERROR) << "DPP: failed to resolve WiFi.DPPRelay. object path";
        return false;
    }

    AmbiorixVariant enable_map(AMXC_VAR_ID_HTABLE);
    enable_map.add_child("RelayEnable", true);
    if (!m_ambiorix_cl.update_object(m_dpp_path, enable_map)) {
        LOG(ERROR) << "DPP: failed to enable relay listener at " << m_dpp_path;
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
    m_dpp_client_connected = false;

    if (!m_dpp_path.empty()) {
        AmbiorixVariant enable_map(AMXC_VAR_ID_HTABLE);
        enable_map.add_child("RelayEnable", false);
        if (!m_ambiorix_cl.update_object(m_dpp_path, enable_map)) {
            LOG(WARNING) << "DPP: failed to disable relay listener at " << m_dpp_path;
        }
    }
}

void slave_wlan_hal_whm::subscribe_to_dpp_events()
{
    m_dpp_event_handler              = std::make_shared<sAmbiorixEventHandler>();
    m_dpp_event_handler->event_type  = "DppFrameReceived";
    m_dpp_event_handler->callback_fn = [this](AmbiorixVariant &event_data) {
        on_dpp_ambiorix_event(event_data);
    };
    std::string filter =
        "(path matches '" + m_dpp_path + "$') && (notification == 'DppFrameReceived')";
    m_ambiorix_cl.subscribe_to_object_event(m_dpp_path, m_dpp_event_handler, filter);
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
                     std::make_shared<sDppFrameEvent>(sDppFrameEvent{tcp_type, frame}));
}

bool slave_wlan_hal_whm::dpp_send_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
{
    LOG(DEBUG) << "DPP: dpp_send_frame diag: m_dpp_path='" << m_dpp_path
               << "' amxb_fd=" << m_ambiorix_cl.get_fd()
               << " amxp_fd=" << m_ambiorix_cl.get_signal_fd();

    if (m_dpp_path.empty()) {
        LOG(ERROR) << "DPP: SendDppFrame failed - m_dpp_path is empty";
        return false;
    }

    AmbiorixVariant args(AMXC_VAR_ID_HTABLE), result;
    args.add_child("tcpType", tcp_type);
    args.add_child("frame", string_utils::bytes_to_hex_string(frame, frame_len));

    if (!m_ambiorix_cl.call(m_dpp_path, "SendDppFrame", args, result)) {
        LOG(ERROR) << "DPP: SendDppFrame failed - amxb_fd=" << m_ambiorix_cl.get_fd()
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
