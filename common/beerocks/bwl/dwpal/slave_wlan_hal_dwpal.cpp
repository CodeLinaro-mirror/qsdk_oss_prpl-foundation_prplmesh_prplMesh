/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "slave_wlan_hal_dwpal.h"

#include <bcl/network/network_utils.h>

namespace bwl {
namespace dwpal {

slave_wlan_hal_dwpal::slave_wlan_hal_dwpal(const std::string &iface_name, hal_event_cb_t callback,
                                           const bwl::hal_conf_t &hal_conf)
    : base_wlan_hal(bwl::HALType::Slave, iface_name, IfaceType::Intel, callback, hal_conf),
      base_wlan_hal_dwpal(bwl::HALType::Slave, iface_name, callback, hal_conf)
{
}

sMacAddr slave_wlan_hal_dwpal::get_bsta_mld_mac() { return beerocks::net::network_utils::ZERO_MAC; }

sMacAddr slave_wlan_hal_dwpal::get_ap_mld_mac() { return beerocks::net::network_utils::ZERO_MAC; }

bool slave_wlan_hal_dwpal::process_dwpal_event(char *buffer, int bufLen, const std::string &opcode)
{
    return true;
}

bool slave_wlan_hal_dwpal::process_dwpal_nl_event(struct nl_msg *msg, void *arg) { return true; }

} // namespace dwpal

std::shared_ptr<slave_wlan_hal> slave_wlan_hal_create(const std::string &iface_name,
                                                      base_wlan_hal::hal_event_cb_t callback,
                                                      const hal_conf_t &hal_conf)
{
    return std::make_shared<dwpal::slave_wlan_hal_dwpal>(iface_name, callback, hal_conf);
}

} // namespace bwl
