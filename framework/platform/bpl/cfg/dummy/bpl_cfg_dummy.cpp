/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_version.h>
#include <bpl/bpl_cfg.h>
#include <mapf/common/utils.h>

namespace beerocks {
namespace bpl {

bool get_friendly_name(std::string &friendly_name)
{
    // The dummy backend has no independent DeviceInfo bus client.
    friendly_name.clear();
    return false;
}

/*
 * Function to get the Serial Number from data model
 * On failure, assign the default value.
 */
bool get_serial_number(std::string &serial_number)
{
    serial_number = "prplmesh12345";
    return true;
}

/*
 * Function to get the Software Version from data model
 * On failure, assign the default value.
 */
bool get_software_version(std::string &software_version)
{
    software_version = beerocks::version::get_module_version();
    return true;
}

bool get_manufacturer(std::string &manufacturer)
{
    manufacturer = "prplMesh";
    return true;
}

bool get_model_name(std::string &model_name)
{
    model_name = "Ubuntu";
    return true;
}

bool get_model_number(std::string &model_number)
{
    model_number = "modelnumber12345";
    return true;
}

bool get_max_prioritization_rules(uint32_t &max_prioritization_rules)
{
    LOG(ERROR) << __func__ << ":not Supported in Dummy";
    return false;
}

bool get_ruid_chipset_vendor(const sMacAddr &ruid, std::string &chipset_vendor)
{
    LOG(ERROR) << __func__ << ":not Supported in Dummy";
    return false;
}

} // namespace bpl
} // namespace beerocks
