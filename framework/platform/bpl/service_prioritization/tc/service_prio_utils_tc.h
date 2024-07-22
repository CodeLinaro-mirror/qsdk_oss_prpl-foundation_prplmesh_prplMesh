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
#include <set>
#include <string>

namespace beerocks {
namespace bpl {

class ServicePrioritizationUtils_tc : public ServicePrioritizationUtils {
public:
    ~ServicePrioritizationUtils_tc(); // clear all rules when we are done

    bool flush_rules() override;
    bool apply_single_value_map(std::list<struct sInterfaceTagInfo> *iface_list,
                                uint8_t pcp) override;
    bool apply_dscp_map(std::list<sInterfaceTagInfo> *iface_list, struct sDscpMap *map,
                        uint8_t default_pcp = 0) override;
    bool apply_up_map(std::list<sInterfaceTagInfo> *iface_list, uint8_t default_pcp = 0) override;

private:
    void apply_rules(const std::list<sInterfaceTagInfo> &iface_list, uint8_t pcp);

private:
    std::set<std::string> applied_interfaces;
};

} // namespace bpl
} // namespace beerocks
