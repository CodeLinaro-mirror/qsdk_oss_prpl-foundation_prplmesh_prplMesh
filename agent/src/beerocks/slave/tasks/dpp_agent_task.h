/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2024 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_AGENT_TASK_H_
#define _DPP_AGENT_TASK_H_

#include "task.h"

#include <tlvf/CmduMessageTx.h>
#include <tlvf/common/sMacAddr.h>

#include <cstdint>

namespace beerocks {

// Forward declaration
class slave_thread;

/**
 * @brief DPP Agent Task
 *
 * Implements the DPP Relay Proxy role for the prplMesh Agent.
 *
 * The raw TCP relay socket to hostapd is owned in-process by slave_thread's
 * bwl::slave_wlan_hal (common/beerocks/bwl/include/bwl/slave_wlan_hal.h). This task
 * is notified of received frames via on_dpp_frame_received() and sends frames back
 * to hostapd via slave_wlan_hal::dpp_send_frame() — no cross-process/CMDU hop is
 * involved.
 *
 *   Uplink:   Receives DPP frame notifications from the relay HAL, encapsulates them
 *             into IEEE 1905.1 Proxied Encap DPP / Chirp Notification CMDUs, and
 *             forwards to the Controller.
 *   Downlink: Receives Proxied Encap DPP CMDUs from the Controller, extracts the
 *             raw DPP frame, and sends it to hostapd via the relay HAL.
 */
class DppAgentTask : public Task {
public:
    /**
     * @brief Constructor.
     *
     * @param btl_ctx Reference to the slave thread context.
     * @param cmdu_tx CMDU transmit buffer.
     */
    DppAgentTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);

    ~DppAgentTask() = default;

    /**
     * @brief Handle incoming CMDU messages (downlink from Controller).
     *
     * Handles PROXIED_ENCAP_DPP_MESSAGE from the Controller and forwards the
     * encapsulated DPP frame to hostapd via the relay HAL.
     */
    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;

    /**
     * @brief Notify the task that a DPP frame was received from hostapd via the relay HAL.
     *
     * @param tcp_type  TCP type byte (first byte of payload after length header).
     * @param frame     Pointer to the frame body (after TCP header strip).
     * @param frame_len Length of the frame body in bytes.
     */
    void on_dpp_frame_received(uint8_t tcp_type, const uint8_t *frame, size_t frame_len);

    /**
     * @brief Notify the task that hostapd's connection state to the relay changed, as
     * reported by slave_thread's bwl::slave_wlan_hal Dpp_Client_Connected/
     * Dpp_Client_Disconnected events (routed through hal_event_handler()). The listener itself
     * is always active once slave_thread has started the relay via init_dpp_relay(), so there
     * is no separate "server active" signal.
     *
     * @param client_connected true if hostapd is currently connected to the relay.
     */
    void on_relay_status(bool client_connected);

    /**
     * @brief Returns true if slave_thread has started the relay HAL's TCP listener.
     * Used by slave_thread to decide whether to suppress the ap_manager's
     * CHIRP_NOTIFICATION_MESSAGE (which would be a duplicate), and as the
     * idempotency guard before starting the listener.
     */
    bool is_relay_active() const { return m_relay_active; }

private:
    /**
     * @brief Send a raw DPP frame to hostapd via the relay HAL.
     *
     * @param tcp_type  802.11 Public Action type byte (e.g. 0x09 or 0x0B).
     * @param frame     Raw DPP frame body.
     * @param frame_len Length of the raw DPP frame in bytes.
     * @return true on success, false if no active hostapd connection or send failed.
     */
    bool send_dpp_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len);

    // -------------------------------------------------------------------------
    // Uplink frame dispatch (hostapd → Controller)
    // -------------------------------------------------------------------------

    /**
     * @brief Dispatch a complete TCP frame received from hostapd.
     *
     * @param tcp_type  TCP type byte (first byte of payload after length header).
     * @param frame     Pointer to the frame body (after TCP header strip).
     * @param frame_len Length of the frame body in bytes.
     */
    void dispatch_dpp_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len);

    /**
     * @brief Handle a DPP Presence Announcement frame (subtype 0x0D).
     * Extracts the bootstrapping key hash and sends a Chirp Notification CMDU.
     */
    void handle_presence_announcement(const uint8_t *frame, size_t frame_len);

    /**
     * @brief Handle a DPP Authentication Response frame (subtype 0x01).
     * Sends a Proxied Encap DPP CMDU (Public Action) to the Controller.
     */
    void handle_auth_response(const uint8_t *frame, size_t frame_len);

    /**
     * @brief Handle a DPP Configuration Result frame (subtype 0x0B).
     * Sends a Proxied Encap DPP CMDU (Public Action) to the Controller.
     */
    void handle_config_result(const uint8_t *frame, size_t frame_len);

    /**
     * @brief Handle a GAS Initial/Comeback Request frame.
     * Sends a Proxied Encap DPP CMDU (GAS frame) to the Controller.
     */
    void handle_gas_request(const uint8_t *frame, size_t frame_len);

    /**
     * @brief Send a Proxied Encap DPP CMDU to the Controller.
     *
     * @param frame      Raw DPP frame body.
     * @param frame_len  Length of the frame body.
     * @param is_gas     true for GAS frames, false for Public Action frames.
     */
    void send_proxied_encap_dpp(const uint8_t *frame, size_t frame_len, bool is_gas);

    // -------------------------------------------------------------------------
    // Downlink frame dispatch (Controller → hostapd)
    // -------------------------------------------------------------------------

    /**
     * @brief Handle a Proxied Encap DPP message received from the Controller.
     * Extracts the encapsulated DPP frame and sends it to hostapd via TCP.
     */
    void handle_proxied_encap_dpp_from_controller(ieee1905_1::CmduMessageRx &cmdu_rx);

    // -------------------------------------------------------------------------
    // Member variables
    // -------------------------------------------------------------------------

    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;

    /** True if slave_thread's slave_wlan_hal DPP relay listener is active. */
    bool m_relay_active = false;

    /** True if hostapd is currently connected to the relay. */
    bool m_hostapd_connected = false;
};

} // namespace beerocks

#endif // _DPP_AGENT_TASK_H_
