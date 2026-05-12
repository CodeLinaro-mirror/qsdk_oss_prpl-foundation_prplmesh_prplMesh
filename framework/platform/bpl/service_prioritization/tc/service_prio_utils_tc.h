/* SPDX-License-Identifier: BSD-2-Clause-Patent
*
* SPDX-FileCopyrightText: 2024 the prplMesh contributors (see AUTHORS.md)
*
* This code is subject to the terms of the BSD+Patent license.
* See LICENSE file for more details.
*/

#include <algorithm>
#include <bpl/bpl_service_prio_utils.h>
#include <iterator>
#include <map>
#include <set>
#include <string>

namespace beerocks {
namespace bpl {

/**
 * @brief TC-based implementation of service-prioritization rule handling.
 *
 * Installs clsact egress filters for PCP, DSCP and VLAN-tagged traffic and
 * tracks installed filter preferences for scoped cleanup.
 */
class ServicePrioritizationUtils_tc : public ServicePrioritizationUtils {
public:
    /**
     * @brief Clear all installed service-prioritization rules.
     */
    ~ServicePrioritizationUtils_tc();

    /**
     * @brief Remove all service-prioritization TC filters installed by this object.
     *
     * @return true if all installed filters were removed successfully.
     */
    bool flush_rules() override;

    /**
     * @brief Apply the same PCP value on all listed interfaces.
     *
     * @param iface_list Interfaces and VLAN IDs to configure.
     * @param pcp PCP value to apply.
     * @return true on success, false on error.
     */
    bool apply_single_value_map(std::list<sInterfaceTagInfo> *iface_list, uint8_t pcp) override;

    /**
     * @brief Apply DSCP-to-PCP mapping on all listed interfaces.
     *
     * @param iface_list Interfaces and VLAN IDs to configure.
     * @param map DSCP-to-PCP mapping table.
     * @param default_pcp PCP value to use when @a map is not provided.
     * @return true on success, false on error.
     */
    bool apply_dscp_map(std::list<sInterfaceTagInfo> *iface_list, sDscpMap *map,
                        uint8_t default_pcp = 0) override;

    /**
     * @brief Apply UP-to-PCP mapping on all listed interfaces.
     *
     * @param iface_list Interfaces and VLAN IDs to configure.
     * @param default_pcp Default PCP value.
     * @return true on success, false on error.
     */
    bool apply_up_map(std::list<sInterfaceTagInfo> *iface_list, uint8_t default_pcp = 0) override;

private:
    /**
     * @brief Prepare the TC qdisc used by service-prioritization filters.
     *
     * @param iface_name Interface name to configure.
     * @return true on success, false on error.
     */
    bool ensure_qdisc(const std::string &iface_name);

    /**
     * @brief Remove service-prioritization filters from an interface.
     *
     * @param iface_name Interface name to clean.
     * @return true on success, false on error.
     */
    bool remove_filters(const std::string &iface_name);

private:
    /**
     * @brief Map of interface names to installed egress filter preferences.
     */
    std::map<std::string, std::set<uint32_t>> applied_filter_prefs;
};

} // namespace bpl
} // namespace beerocks
