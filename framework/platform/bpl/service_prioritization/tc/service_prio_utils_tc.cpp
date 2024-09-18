/* SPDX-License-Identifier: BSD-2-Clause-Patent
*
* SPDX-FileCopyrightText: 2024 the prplMesh contributors (see AUTHORS.md)
*
* This code is subject to the terms of the BSD+Patent license.
* See LICENSE file for more details.
*/

#include "service_prio_utils_tc.h"

#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/beerocks_os_utils.h>
#include <bcl/beerocks_string_utils.h>
#include <bcl/beerocks_utils.h>
#include <bcl/network/network_utils.h>

#include <easylogging++.h>

namespace beerocks {
namespace bpl {

ServicePrioritizationUtils_tc::~ServicePrioritizationUtils_tc() { flush_rules(); }

bool ServicePrioritizationUtils_tc::flush_rules()
{
    for (const auto &iface_name : applied_interfaces)
        remove_qdisc(iface_name);

    LOG(DEBUG) << "Flushed QoS tc rules";
    return true;
}

bool ServicePrioritizationUtils_tc::apply_single_value_map(std::list<sInterfaceTagInfo> *iface_list,
                                                           uint8_t pcp)
{
    LOG(DEBUG) << "Applying single value map with PCP: " << std::to_string(pcp);

    if (!iface_list) {
        LOG(ERROR) << "iface_list does not exist";
        return false;
    }

    // Apply filter for
    for (const auto &iface : *iface_list) {
        if (applied_interfaces.find(iface.iface_name) != applied_interfaces.end()) {
            LOG(WARNING) << "Rules for iface=" << iface.iface_name
                         << " were already applied before. Removing and installing new";
            remove_qdisc(iface.iface_name);
        }

        // Add the root prio qdisc
        add_qdisc(iface.iface_name);

        // --- Filter for WiFi QoS Control priority ---
        std::string cmd = "tc filter add dev " + iface.iface_name +
                          " protocol ip matchall action skbedit priority " + std::to_string(pcp);
        beerocks::os_utils::system_call(cmd);

        // --- Filter for VLAN tag priority ---
        for (uint16_t vlan_id : iface.vlan_ids) {
            cmd = "tc filter add dev " + iface.iface_name +
                  " parent ffff: protocol 802.1Q flower vlan_id " + std::to_string(vlan_id) +
                  " action goto chain " +
                  std::to_string(vlan_id); // using vlan_id as a chain id for simplicity
            beerocks::os_utils::system_call(cmd);

            cmd = "tc filter add dev " + iface.iface_name + " chain " +
                  std::to_string(vlan_id) + // using vlan_id as a chain id for simplicity
                  " parent ffff: protocol 802.1Q matchall action vlan "
                  "modify priority " +
                  std::to_string(pcp) + " id " + std::to_string(vlan_id);
            beerocks::os_utils::system_call(cmd);
        }
    }

    return true;
}

bool ServicePrioritizationUtils_tc::apply_dscp_map(std::list<sInterfaceTagInfo> *iface_list,
                                                   sDscpMap *map, uint8_t default_pcp)
{
    LOG(DEBUG) << "Applying DSCP map";

    if (!iface_list) {
        LOG(ERROR) << "iface_list does not exist";
        return false;
    }

    if (!map) {
        LOG(ERROR) << "DSCP map does not exist. Applying single prio = " << default_pcp;
        apply_single_value_map(iface_list, default_pcp);
        return false;
    }

    for (const auto &iface : *iface_list) {
        if (applied_interfaces.find(iface.iface_name) != applied_interfaces.end()) {
            LOG(WARNING) << "Rules for iface=" << iface.iface_name
                         << " were already applied before. Removing and setting new";
            remove_qdisc(iface.iface_name);
        }

        // Add the root prio qdisc
        add_qdisc(iface.iface_name);

        // Iterate over each DSCP value in the map and apply a filter
        for (uint16_t i = 0; i < DSCP_MAP_LENGTH; ++i) {
            // we need to << 2 bits because of prio position in DSCP filed (two rightmost bits are used for different purpose)
            const uint16_t dscp_value = i << 2;
            const uint8_t prio_value  = map->dscp[i];

            // --- Filter for WiFi QoS Control priority ---
            std::string cmd = "tc filter add dev " + iface.iface_name +
                              " protocol ip u32 match ip dsfield 0x" +
                              string_utils::int_to_hex_string(dscp_value, 2) +
                              " 0xFC action skbedit priority " + std::to_string(prio_value);
            beerocks::os_utils::system_call(cmd);

            // --- Filter for VLAN tag priority ---
            for (uint16_t vlan_id : iface.vlan_ids) {
                // Apply the filter to match VLAN
                cmd = "tc filter add dev " + iface.iface_name +
                      " parent ffff: protocol 802.1Q flower vlan_id " + std::to_string(vlan_id) +
                      " action goto chain " +
                      std::to_string(vlan_id); // using vlan_id as a chain id for simplicity
                beerocks::os_utils::system_call(cmd);

                // Apply filter for modifying VLAN tag priority
                cmd = "tc filter add dev " + iface.iface_name + " chain " +
                      std::to_string(vlan_id) + // using vlan_id as a chain id for simplicity
                      " parent ffff: protocol 802.1Q u32 match ip dsfield 0x" +
                      string_utils::int_to_hex_string(dscp_value, 2) +
                      " 0xFC action vlan modify priority " + std::to_string(prio_value) + " id " +
                      std::to_string(vlan_id);
                beerocks::os_utils::system_call(cmd);
            }
        }
        LOG(DEBUG) << "Applied dscp map for UP and PCP successfully";
    }

    return true;
}

bool ServicePrioritizationUtils_tc::apply_up_map(std::list<sInterfaceTagInfo> *iface_list,
                                                 uint8_t default_pcp)
{
    LOG(ERROR) << __func__ << ":not Supported for now";
    return false;
}

bool ServicePrioritizationUtils_tc::add_qdisc(const std::string &iface_name)
{
    if (applied_interfaces.find(iface_name) != applied_interfaces.end()) {
        LOG(WARNING) << "Qdisc already initialized for iface=" << iface_name;
        return false;
    }

    // Add the root prio qdisc
    std::string cmd = "tc qdisc add dev " + iface_name + " handle ffff: root prio";
    beerocks::os_utils::system_call(cmd);

    applied_interfaces.insert(iface_name);
    return true;
}

bool ServicePrioritizationUtils_tc::remove_qdisc(const std::string &iface_name)
{
    if (applied_interfaces.find(iface_name) == applied_interfaces.end()) {
        LOG(WARNING) << "Qdisc not initialized for iface=" << iface_name;
        return false;
    }

    // remove qdisc
    std::string cmd = "tc qdisc del dev " + iface_name + " handle ffff: root prio";
    beerocks::os_utils::system_call(cmd);

    applied_interfaces.erase(iface_name);
    return true;
}

std::shared_ptr<ServicePrioritizationUtils> register_service_prio_utils()
{
    return std::make_shared<bpl::ServicePrioritizationUtils_tc>();
}

} // namespace bpl
} // namespace beerocks
