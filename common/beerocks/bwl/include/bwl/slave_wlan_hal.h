/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _BWL_SLAVE_WLAN_HAL_H_
#define _BWL_SLAVE_WLAN_HAL_H_

#include "base_wlan_hal.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bwl {

/*!
 * Hardware abstraction layer for Slave
 * Read more about virtual inheritance: https://en.wikipedia.org/wiki/Virtual_inheritance
 */
class slave_wlan_hal : public virtual base_wlan_hal {

    // Public definitions
public:
    // Slave Events
    enum class Event {
        Invalid = 0,

        // DPP-over-TCP relay events
        Dpp_Client_Connected,
        Dpp_Client_Disconnected,
        Dpp_Frame_Received,
    };

    /** Payload pushed via event_queue_push() for Event::Dpp_Frame_Received. */
    struct sDppFrameEvent {
        uint8_t tcp_type;
        std::vector<uint8_t> frame;
    };

    // Public methods:
public:
    virtual ~slave_wlan_hal() = default;

    /**
     * @brief Start the DPP-over-TCP relay listener, if supported by this backend. No-op
     * (returns false) on backends that don't relay DPP over TCP.
     */
    virtual bool init_dpp_relay() { return false; }

    /**
     * @brief Stop the DPP-over-TCP relay listener, if it was started.
     */
    virtual void stop_dpp_relay() {}

    /**
     * @brief Send a raw DPP frame to hostapd on the active relay connection.
     */
    virtual bool dpp_send_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len)
    {
        return false;
    }

    /**
     * @brief Returns true if hostapd is currently connected to the DPP relay.
     */
    virtual bool is_dpp_client_connected() const { return false; }
};

// Slave HAL factory types
std::shared_ptr<slave_wlan_hal> slave_wlan_hal_create(const std::string &iface_name,
                                                      base_wlan_hal::hal_event_cb_t cb,
                                                      const hal_conf_t &hal_conf);

} // namespace bwl

#endif // _BWL_SLAVE_WLAN_HAL_H_
