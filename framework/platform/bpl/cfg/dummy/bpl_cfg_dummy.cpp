/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <amxc/amxc.h>
#include <amxp/amxp.h>

#include <amxd/amxd_action.h>
#include <amxd/amxd_dm.h>
#include <amxd/amxd_object.h>
#include <amxd/amxd_object_event.h>
#include <amxd/amxd_transaction.h>

#include <amxb/amxb.h>
#include <amxb/amxb_register.h>

#include <amxo/amxo.h>
#include <amxo/amxo_save.h>

#include <bcl/beerocks_version.h>
#include <bpl/bpl_cfg.h>
#include <mapf/common/utils.h>

namespace beerocks {
namespace bpl {

/*
 * This function is to fetch data from DeviceInfo. 
 * data model. This file is used for legacy platforms.
 */
bool get_string_value_dm(std::string &attr, std::string &value)
{
    std::string dm                 = "DeviceInfo";
    std::string dev_string         = "0.'DeviceInfo.'";
    constexpr int amxb_get_timeout = 3;
    value                          = std::string("invalid");

    amxc_var_t data;
    amxc_var_init(&data);

    amxb_bus_ctx_t *ctx = amxb_be_who_has(dm.c_str());
    if (ctx == NULL) {
        LOG(WARNING) << "Failed to get the bus context";
        return false;
    }

    dm += attr;
    if (amxb_get(ctx, dm.c_str(), 0, &data, amxb_get_timeout) != AMXB_STATUS_OK) {
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

/*
 * Function to get the Serial Number from data model
 * On failure, assign the default value.
 */
bool get_serial_number(std::string &serial_number)
{
    std::string attr = "SerialNumber";

    if (!get_string_value_dm(attr, serial_number)) {
        serial_number.assign("prplmesh12345");
    }
    return true;
}

/*
 * Function to get the Software Version from data model
 * On failure, assign the default value.
 */
bool get_software_version(std::string &software_version)
{
    std::string attr = "SoftwareVersion";

    if (!get_string_value_dm(attr, software_version)) {
        std::string version_string = beerocks::version::get_module_version();
        software_version.assign(version_string);
    }
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
