/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_pwhm.h"
#include <bcl/beerocks_version.h>
#include <bpl/bpl_cfg.h>
#include <mapf/common/utils.h>

using namespace mapf;

namespace beerocks {
namespace bpl {

bool get_string_value_dm(std::string attr, std::string &value)
{
    std::string dm       = "DeviceInfo.";
    std::string attr_val = "";

    auto det = m_ambiorix_cl.get_object(dm);
    if (!det) {
        LOG(ERROR) << "Failed to get the ambiorix object for path " << dm;
        return false;
    }

    det->read_child<>(attr_val, attr);
    value.assign(attr_val);

    return true;
}

bool get_serial_number(std::string &serial_number)
{
    std::string attr = "SerialNumber";

    if (!get_string_value_dm(attr, serial_number)) {
        serial_number.assign("prplmesh12345");
    }
    return true;
}

bool get_software_version(std::string &software_version)
{
    std::string attr = "SoftwareVersion";

    if (!get_string_value_dm(attr, software_version)) {
        std::string version_string = beerocks::version::get_module_version();
        software_version.assign(version_string);
    }
    return true;
}

bool get_ruid_chipset_vendor(const sMacAddr &ruid, std::string &chipset_vendor)
{
    chipset_vendor = "prplmesh";
    return true;
}

bool get_max_prioritization_rules(uint32_t &max_prioritization_rules)
{
    // On EasyMesh standard 9.1 it is said that a Multi-AP Agent that implements Profile-3, need to:
    // "Set Max Total Number Service Prioritization Rules to one".
    // This requirement will probably change on future version of the standard.
    max_prioritization_rules = 1;
    return true;
}

} // namespace bpl
} // namespace beerocks
