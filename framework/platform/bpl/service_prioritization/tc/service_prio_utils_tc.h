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

class ServicePrioritizationUtils_tc : public ServicePrioritizationUtils {
public:
    ~ServicePrioritizationUtils_tc(); // clear all rules when we are done

    bool flush_rules() override;
    bool apply_single_value_map(std::list<sInterfaceTagInfo> *iface_list, uint8_t pcp) override;
    bool apply_dscp_map(std::list<sInterfaceTagInfo> *iface_list, sDscpMap *map,
                        uint8_t default_pcp = 0) override;
    bool apply_up_map(std::list<sInterfaceTagInfo> *iface_list, uint8_t default_pcp = 0) override;

private:
    bool ensure_qdisc(const std::string &iface_name);
    bool remove_filters(const std::string &iface_name);

private:
    std::map<std::string, std::set<uint32_t>> applied_filter_prefs;
};

} // namespace bpl
} // namespace beerocks
