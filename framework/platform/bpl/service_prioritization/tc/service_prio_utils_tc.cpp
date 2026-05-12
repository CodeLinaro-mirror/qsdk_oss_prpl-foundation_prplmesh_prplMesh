/* SPDX-License-Identifier: BSD-2-Clause-Patent
*
* SPDX-FileCopyrightText: 2024 the prplMesh contributors (see AUTHORS.md)
*
* This code is subject to the terms of the BSD+Patent license.
* See LICENSE file for more details.
*/

#include "service_prio_utils_tc.h"

#include <bcl/beerocks_string_utils.h>
#include <bcl/network/network_utils.h>

#include <easylogging++.h>

#include <sstream>
#include <vector>

namespace beerocks {
namespace bpl {
namespace {

constexpr const char *kTcBinary             = "tc";
constexpr const char *kEgressSelector       = "egress";
constexpr const char *kDscpIpTosMask        = "0xFC";
constexpr uint32_t kRulePrefBase            = 100;
constexpr uint32_t kSingleValueFallbackPref = 1000;

bool install_matchall_priority_rule(const std::string &iface_name, uint32_t pref, uint8_t pcp)
{
    std::ostringstream cmd;
    cmd << kTcBinary << " filter add dev " << iface_name << " " << kEgressSelector
        << " protocol all pref " << pref << " matchall action skbedit priority " << unsigned(pcp);

    return net::network_utils::tc_run_command(cmd.str());
}

bool install_vlan_priority_rule(const std::string &iface_name, uint32_t pref, uint16_t vlan_id,
                                uint8_t pcp)
{
    std::ostringstream cmd;
    cmd << kTcBinary << " filter add dev " << iface_name << " " << kEgressSelector
        << " protocol 802.1Q pref " << pref << " flower vlan_id " << vlan_id
        << " action skbedit priority " << unsigned(pcp) << " action vlan modify priority "
        << unsigned(pcp) << " id " << vlan_id;

    return net::network_utils::tc_run_command(cmd.str());
}

bool install_dscp_priority_rule(const std::string &iface_name, uint32_t pref, uint16_t dscp_value,
                                uint8_t pcp)
{
    std::ostringstream cmd;
    cmd << kTcBinary << " filter add dev " << iface_name << " " << kEgressSelector
        << " protocol ip pref " << pref << " flower ip_tos 0x"
        << string_utils::int_to_hex_string(dscp_value, 2) << "/" << kDscpIpTosMask
        << " action skbedit priority " << unsigned(pcp);

    return net::network_utils::tc_run_command(cmd.str());
}

bool install_vlan_dscp_priority_rule(const std::string &iface_name, uint32_t pref, uint16_t vlan_id,
                                     uint16_t dscp_value, uint8_t pcp)
{
    std::ostringstream cmd;
    cmd << kTcBinary << " filter add dev " << iface_name << " " << kEgressSelector
        << " protocol 802.1Q pref " << pref << " flower vlan_ethtype ip vlan_id " << vlan_id
        << " ip_tos 0x" << string_utils::int_to_hex_string(dscp_value, 2) << "/" << kDscpIpTosMask
        << " action skbedit priority " << unsigned(pcp) << " action vlan modify priority "
        << unsigned(pcp) << " id " << vlan_id;

    return net::network_utils::tc_run_command(cmd.str());
}

bool remove_filter_pref(const std::string &iface_name, uint32_t pref)
{
    std::ostringstream cmd;
    cmd << kTcBinary << " filter del dev " << iface_name << " " << kEgressSelector << " pref "
        << pref;

    return net::network_utils::tc_run_command(cmd.str(), true);
}

} // namespace

ServicePrioritizationUtils_tc::~ServicePrioritizationUtils_tc() { flush_rules(); }

bool ServicePrioritizationUtils_tc::flush_rules()
{
    std::vector<std::string> interfaces;
    interfaces.reserve(applied_filter_prefs.size());
    for (const auto &iface : applied_filter_prefs) {
        interfaces.push_back(iface.first);
    }

    bool retVal = true;

    for (const auto &iface_name : interfaces)
        retVal = remove_filters(iface_name) && retVal;

    LOG(DEBUG) << "Flushed QoS tc rules";
    return retVal;
}

bool ServicePrioritizationUtils_tc::apply_single_value_map(std::list<sInterfaceTagInfo> *iface_list,
                                                           uint8_t pcp)
{
    LOG(DEBUG) << "Applying single value map with PCP: " << std::to_string(pcp);

    if (!iface_list) {
        LOG(ERROR) << "iface_list does not exist";
        return false;
    }

    for (const auto &iface : *iface_list) {
        if (applied_filter_prefs.find(iface.iface_name) != applied_filter_prefs.end()) {
            LOG(WARNING) << "Rules for iface=" << iface.iface_name
                         << " were already applied before. Removing and installing new";
            if (!remove_filters(iface.iface_name)) {
                LOG(WARNING) << "Failed to remove existing QoS filters for iface="
                             << iface.iface_name;
            }
        }

        if (!ensure_qdisc(iface.iface_name)) {
            return false;
        }

        // Install VLAN-specific rules first so tagged traffic does not fall through
        // to the generic match all skb priority rule.
        uint32_t pref = kRulePrefBase;
        for (uint16_t vlan_id : iface.vlan_ids) {
            if (!remove_filter_pref(iface.iface_name, pref)) {
                return false;
            }
            if (!install_vlan_priority_rule(iface.iface_name, pref++, vlan_id, pcp)) {
                remove_filters(iface.iface_name);
                return false;
            }
            applied_filter_prefs[iface.iface_name].insert(pref - 1);
        }

        if (!remove_filter_pref(iface.iface_name, kSingleValueFallbackPref)) {
            return false;
        }
        if (!install_matchall_priority_rule(iface.iface_name, kSingleValueFallbackPref, pcp)) {
            remove_filters(iface.iface_name);
            return false;
        }
        applied_filter_prefs[iface.iface_name].insert(kSingleValueFallbackPref);

        LOG(DEBUG) << "Applied single value map for iface=" << iface.iface_name;
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
        LOG(WARNING) << "DSCP map does not exist. Applying single prio = " << default_pcp;
        return apply_single_value_map(iface_list, default_pcp);
    }

    for (const auto &iface : *iface_list) {
        if (applied_filter_prefs.find(iface.iface_name) != applied_filter_prefs.end()) {
            LOG(WARNING) << "Rules for iface=" << iface.iface_name
                         << " were already applied before. Removing and setting new";
            if (!remove_filters(iface.iface_name)) {
                LOG(WARNING) << "Failed to remove existing QoS filters for iface="
                             << iface.iface_name;
            }
        }

        if (!ensure_qdisc(iface.iface_name)) {
            return false;
        }

        uint32_t pref = kRulePrefBase;
        for (uint16_t i = 0; i < DSCP_MAP_LENGTH; ++i) {
            const uint16_t dscp_value = i << 2;
            const uint8_t prio_value  = map->dscp[i];

            if (!remove_filter_pref(iface.iface_name, pref)) {
                return false;
            }
            if (!install_dscp_priority_rule(iface.iface_name, pref++, dscp_value, prio_value)) {
                remove_filters(iface.iface_name);
                return false;
            }
            applied_filter_prefs[iface.iface_name].insert(pref - 1);

            for (uint16_t vlan_id : iface.vlan_ids) {
                if (!remove_filter_pref(iface.iface_name, pref)) {
                    return false;
                }
                if (!install_vlan_dscp_priority_rule(iface.iface_name, pref++, vlan_id, dscp_value,
                                                     prio_value)) {
                    remove_filters(iface.iface_name);
                    return false;
                }
                applied_filter_prefs[iface.iface_name].insert(pref - 1);
            }
        }

        LOG(DEBUG) << "Applied dscp map for iface=" << iface.iface_name;
    }

    return true;
}

bool ServicePrioritizationUtils_tc::apply_up_map(std::list<sInterfaceTagInfo> *iface_list,
                                                 uint8_t default_pcp)
{
    LOG(ERROR) << __func__ << ":not Supported for now";
    return false;
}

bool ServicePrioritizationUtils_tc::ensure_qdisc(const std::string &iface_name)
{
    if (!net::network_utils::tc_ensure_clsact_qdisc(iface_name)) {
        LOG(ERROR) << "Failed to install QoS qdisc for iface=" << iface_name;
        return false;
    }

    applied_filter_prefs.emplace(iface_name, std::set<uint32_t>{});
    return true;
}

bool ServicePrioritizationUtils_tc::remove_filters(const std::string &iface_name)
{
    auto iface_it = applied_filter_prefs.find(iface_name);
    if (iface_it == applied_filter_prefs.end()) {
        LOG(WARNING) << "QoS filters not initialized for iface=" << iface_name;
        return false;
    }

    // clsact is shared with traffic separation ingress filters; remove only our egress prefs.
    bool retVal = true;
    for (auto pref : iface_it->second) {
        std::ostringstream cmd;
        cmd << kTcBinary << " filter del dev " << iface_name << " " << kEgressSelector << " pref "
            << pref;
        retVal = net::network_utils::tc_run_command(cmd.str()) && retVal;
    }

    applied_filter_prefs.erase(iface_it);
    return retVal;
}

std::shared_ptr<ServicePrioritizationUtils> register_service_prio_utils()
{
    return std::make_shared<bpl::ServicePrioritizationUtils_tc>();
}

} // namespace bpl
} // namespace beerocks
