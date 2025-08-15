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
    std::string dm                 = "DeviceInfo";
    std::string dev_string         = "0.'DeviceInfo.'";
    constexpr int amxb_get_timeout = 30;
    value                          = std::string("invalid");

    amxc_var_t data;
    amxc_var_init(&data);

    amxb_bus_ctx_t *ctx = amxb_be_who_has(dm.c_str());
    if (ctx == NULL) {
        LOG(WARNING) << "Failed to get the bus context";
        return false;
    }

    dm += "." + attr;
    int ret = amxb_get(ctx, dm.c_str(), 0, &data, amxb_get_timeout);
    if (ret != AMXB_STATUS_OK) {
        LOG(WARNING) << "amxb_get timedout";
        return false;
    }

    if (amxc_var_is_null(&data)) {
        return false;
    }

    dev_string += "." + attr;
    const char *p = GETP_CHAR(&data, dev_string.c_str());
    if (p == NULL) {
        amxc_var_clean(&data);
        return false;
    }

    value.assign(p);
    amxc_var_clean(&data);
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

bool get_manufacturer(std::string &manufacturer)
{
    std::string attr = "Manufacturer";

    get_string_value_dm(attr, manufacturer);

    return true;
}

bool get_model_name(std::string &model_name)
{
    std::string attr = "ModelName";

    get_string_value_dm(attr, model_name);

    return true;
}

bool get_model_number(std::string &model_number)
{
    std::string attr = "ModelNumber";
    return get_string_value_dm(attr, model_number);
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
