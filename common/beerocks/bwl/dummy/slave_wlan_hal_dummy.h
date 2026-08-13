/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _BWL_SLAVE_WLAN_HAL_DUMMY_H_
#define _BWL_SLAVE_WLAN_HAL_DUMMY_H_

#include "base_wlan_hal_dummy.h"
#include <bwl/slave_wlan_hal.h>

namespace bwl {
namespace dummy {

/*!
 * Hardware abstraction layer for Slave (DPP-over-TCP relay not supported).
 */
class slave_wlan_hal_dummy : public base_wlan_hal_dummy, public virtual slave_wlan_hal {

    // Public methods
public:
    slave_wlan_hal_dummy(const std::string &iface_name, hal_event_cb_t callback,
                         const bwl::hal_conf_t &hal_conf);
    virtual ~slave_wlan_hal_dummy();

protected:
    virtual bool process_dummy_data(parsed_obj_map_t &parsed_obj) override;
    virtual bool process_dummy_event(parsed_obj_map_t &parsed_obj) override;
};

} // namespace dummy
} // namespace bwl

#endif // _BWL_SLAVE_WLAN_HAL_DUMMY_H_
