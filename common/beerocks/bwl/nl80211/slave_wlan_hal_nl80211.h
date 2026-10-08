/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BWL_SLAVE_WLAN_HAL_NL80211_H_
#define _BWL_SLAVE_WLAN_HAL_NL80211_H_

#include "base_wlan_hal_nl80211.h"
#include <bwl/slave_wlan_hal.h>

namespace bwl {
namespace nl80211 {

/*!
 * Slave HAL stub for NL80211. DPP-over-TCP relay is WHM-only; defaults no-op.
 */
class slave_wlan_hal_nl80211 : public base_wlan_hal_nl80211, public virtual slave_wlan_hal {
public:
    slave_wlan_hal_nl80211(const std::string &iface_name, hal_event_cb_t callback,
                           const bwl::hal_conf_t &hal_conf);
    virtual ~slave_wlan_hal_nl80211() = default;

protected:
    virtual bool process_nl80211_event(parsed_obj_map_t &event) override;
};

} // namespace nl80211
} // namespace bwl

#endif // _BWL_SLAVE_WLAN_HAL_NL80211_H_
