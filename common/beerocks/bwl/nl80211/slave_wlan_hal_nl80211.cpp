/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "slave_wlan_hal_nl80211.h"

namespace bwl {
namespace nl80211 {

namespace {
constexpr int k_slave_wpa_ctrl_buffer_size = 4096;
} // namespace

slave_wlan_hal_nl80211::slave_wlan_hal_nl80211(const std::string &iface_name,
                                               hal_event_cb_t callback,
                                               const bwl::hal_conf_t &hal_conf)
    : base_wlan_hal(bwl::HALType::Slave, iface_name, IfaceType::Intel, callback, hal_conf),
      base_wlan_hal_nl80211(bwl::HALType::Slave, iface_name, callback, k_slave_wpa_ctrl_buffer_size,
                            hal_conf)
{
}

bool slave_wlan_hal_nl80211::process_nl80211_event(parsed_obj_map_t &event) { return true; }

} // namespace nl80211

std::shared_ptr<slave_wlan_hal> slave_wlan_hal_create(const std::string &iface_name,
                                                      base_wlan_hal::hal_event_cb_t callback,
                                                      const hal_conf_t &hal_conf)
{
    return std::make_shared<nl80211::slave_wlan_hal_nl80211>(iface_name, callback, hal_conf);
}

} // namespace bwl
