/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BWL_SLAVE_WLAN_HAL_WHM_H_
#define _BWL_SLAVE_WLAN_HAL_WHM_H_

#include "base_wlan_hal_whm.h"
#include <bwl/slave_wlan_hal.h>
#include <utility>
#include <vector>

namespace bwl {
namespace whm {

class slave_wlan_hal_whm : public base_wlan_hal_whm, public virtual slave_wlan_hal {
public:
    explicit slave_wlan_hal_whm(const std::string &iface_name,
                                base_wlan_hal::hal_event_cb_t callback, const hal_conf_t &hal_conf);
    virtual ~slave_wlan_hal_whm();

    // DPP-over-TCP relay
    bool init_dpp_relay() override;
    void stop_dpp_relay() override;
    bool dpp_send_frame(uint8_t tcp_type, const uint8_t *frame, size_t frame_len) override;
    bool is_dpp_client_connected() const override;

protected:
    bool event_queue_push(slave_wlan_hal::Event event, std::shared_ptr<void> data = {})
    {
        return base_wlan_hal::event_queue_push(int(event), std::move(data));
    }

private:
    void subscribe_to_dpp_events();
    void on_dpp_ambiorix_event(beerocks::wbapi::AmbiorixVariant &event_data);

    std::shared_ptr<beerocks::wbapi::sAmbiorixEventHandler> m_dpp_event_handler;
    std::shared_ptr<beerocks::wbapi::sAmbiorixEventHandler> m_dpp_state_event_handler;
    std::string m_dpp_path;
    bool m_dpp_client_connected = false;
};

} // namespace whm
} // namespace bwl

#endif // _BWL_SLAVE_WLAN_HAL_WHM_H_
