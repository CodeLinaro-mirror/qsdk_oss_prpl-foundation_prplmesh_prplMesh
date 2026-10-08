/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BWL_SLAVE_WLAN_HAL_DWPAL_H_
#define _BWL_SLAVE_WLAN_HAL_DWPAL_H_

#include "base_wlan_hal_dwpal.h"
#include <bwl/slave_wlan_hal.h>

namespace bwl {
namespace dwpal {

/*!
 * Slave HAL stub for DWPAL. DPP-over-TCP relay is WHM-only; defaults no-op.
 */
class slave_wlan_hal_dwpal : public base_wlan_hal_dwpal, public virtual slave_wlan_hal {
public:
    slave_wlan_hal_dwpal(const std::string &iface_name, hal_event_cb_t callback,
                         const bwl::hal_conf_t &hal_conf);
    virtual ~slave_wlan_hal_dwpal() = default;

    sMacAddr get_bsta_mld_mac() override;
    sMacAddr get_ap_mld_mac() override;

protected:
    virtual bool process_dwpal_event(char *buffer, int bufLen, const std::string &opcode) override;
    virtual bool process_dwpal_nl_event(struct nl_msg *msg, void *arg = nullptr) override;
};

} // namespace dwpal
} // namespace bwl

#endif // _BWL_SLAVE_WLAN_HAL_DWPAL_H_
