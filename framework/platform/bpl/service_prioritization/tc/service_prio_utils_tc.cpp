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
    for (const auto &iface_name : applied_interfaces) {
        // remove qdisc
        std::string cmd = "tc qdisc del dev " + iface_name + " handle ffff: clsact";
        beerocks::os_utils::system_call(cmd);
    }
    applied_interfaces.clear();
    LOG(DEBUG) << "Flushed qos ip egress map and tc rules";
    return true;
}

bool ServicePrioritizationUtils_tc::apply_single_value_map(std::list<sInterfaceTagInfo> *iface_list,
                                                           uint8_t pcp)
{
    LOG(DEBUG) << "Applying single value map with PCP: " << std::to_string(pcp);
    apply_rules(*iface_list, pcp);
    return true;
}

bool ServicePrioritizationUtils_tc::apply_dscp_map(std::list<sInterfaceTagInfo> *iface_list,
                                                   struct sDscpMap *map, uint8_t pcp)
{
    LOG(DEBUG) << "Applying DSCP map with default PCP: " << std::to_string(pcp);
    apply_rules(*iface_list, pcp);
    return true;
}

bool ServicePrioritizationUtils_tc::apply_up_map(std::list<sInterfaceTagInfo> *iface_list,
                                                 uint8_t default_pcp)
{
    LOG(DEBUG) << "Applying UP map with default PCP: " << std::to_string(default_pcp);
    apply_rules(*iface_list, default_pcp);
    return true;
}

void ServicePrioritizationUtils_tc::apply_rules(const std::list<sInterfaceTagInfo> &iface_list,
                                                uint8_t pcp)
{
    for (const auto &iface : iface_list) {
        if (applied_interfaces.find(iface.iface_name) != applied_interfaces.end()) {
            LOG(DEBUG) << "Rules for iface=" << iface.iface_name << " were already applied before";
            continue;
        }

        std::string cmd;
        switch (iface.tag_info) {
        case TAGGED_PORT_PRIMARY_TAGGED:   // fall down
        case TAGGED_PORT_PRIMARY_UNTAGGED: // fall down
        case UNTAGGED_PORT:
            cmd = "tc qdisc add dev " + iface.iface_name + " handle ffff: clsact";
            beerocks::os_utils::system_call(cmd);
            cmd = "tc filter add dev " + iface.iface_name +
                  " egress protocol ip u32 match ip protocol 1 0xff action skbedit priority " +
                  std::to_string(pcp);
            beerocks::os_utils::system_call(cmd);
            break;
        default:
            LOG(ERROR) << "Unknown port mode for interface: " << iface.iface_name;
            break;
        }

        applied_interfaces.insert(iface.iface_name);
    }
}

std::shared_ptr<ServicePrioritizationUtils> register_service_prio_utils()
{
    return std::make_shared<bpl::ServicePrioritizationUtils_tc>();
}

} // namespace bpl
} // namespace beerocks
