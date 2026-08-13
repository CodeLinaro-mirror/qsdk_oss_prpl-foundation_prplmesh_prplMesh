/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "slave_wlan_hal_dummy.h"

namespace bwl {
namespace dummy {

slave_wlan_hal_dummy::slave_wlan_hal_dummy(const std::string &iface_name, hal_event_cb_t callback,
                                           const bwl::hal_conf_t &hal_conf)
    : base_wlan_hal(bwl::HALType::Slave, iface_name, IfaceType::Intel, callback, hal_conf),
      base_wlan_hal_dummy(bwl::HALType::Slave, iface_name, callback, hal_conf)
{
}

slave_wlan_hal_dummy::~slave_wlan_hal_dummy() = default;

bool slave_wlan_hal_dummy::process_dummy_data(parsed_obj_map_t &parsed_obj) { return true; }

bool slave_wlan_hal_dummy::process_dummy_event(parsed_obj_map_t &parsed_obj) { return true; }

} // namespace dummy

std::shared_ptr<slave_wlan_hal> slave_wlan_hal_create(const std::string &iface_name,
                                                      base_wlan_hal::hal_event_cb_t callback,
                                                      const hal_conf_t &hal_conf)
{
    return std::make_shared<dummy::slave_wlan_hal_dummy>(iface_name, callback, hal_conf);
}

} // namespace bwl
