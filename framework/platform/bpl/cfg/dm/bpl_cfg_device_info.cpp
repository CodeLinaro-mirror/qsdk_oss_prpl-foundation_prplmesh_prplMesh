/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_pwhm.h"

#include <bcl/beerocks_version.h>
#include <bpl/bpl_cfg.h>

namespace beerocks {
namespace bpl {

static bool get_string_value_dm(const std::string &attr, std::string &value)
{
    value = "invalid";

    auto obj = m_ambiorix_cl_ubus.get_object("DeviceInfo.");
    if (!obj) {
        LOG(WARNING) << "Failed to get DeviceInfo object";
        return false;
    }

    if (!obj->read_child(value, attr)) {
        LOG(WARNING) << "Failed to read DeviceInfo parameter " << attr;
        return false;
    }

    return true;
}

bool get_serial_number(std::string &serial_number)
{
    if (!get_string_value_dm("SerialNumber", serial_number) || serial_number.empty()) {
        serial_number.assign("prplmesh12345");
    }
    return true;
}

bool get_software_version(std::string &software_version)
{
    if (!get_string_value_dm("SoftwareVersion", software_version) || software_version.empty()) {
        software_version.assign(beerocks::version::get_module_version());
    }
    return true;
}

bool get_manufacturer(std::string &manufacturer)
{
    if (!get_string_value_dm("Manufacturer", manufacturer) || manufacturer.empty()) {
        manufacturer = "prplMesh";
    }

    return true;
}

bool get_model_name(std::string &model_name)
{
    if (!get_string_value_dm("ModelName", model_name) || model_name.empty()) {
        model_name = "Ubuntu";
    }

    return true;
}

bool get_model_number(std::string &model_number)
{
    if (!get_string_value_dm("ModelNumber", model_number) || model_number.empty()) {
        model_number = "18.04";
    }
    return true;
}

bool get_ruid_chipset_vendor(const sMacAddr &ruid, std::string &chipset_vendor)
{
    (void)ruid;
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
