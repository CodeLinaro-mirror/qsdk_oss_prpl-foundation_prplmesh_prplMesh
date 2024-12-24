/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#if USE_PRPLMESH_WHM
#include "../prplMesh_WHM/bpl_cfg_pwhm.h"
#endif

#ifndef USE_PRPLMESH_WHM
#include <amxc/amxc.h>
#endif

#include <bcl/beerocks_version.h>
#include <bpl/bpl_cfg.h>
#include <mapf/common/utils.h>

using namespace mapf;

namespace beerocks {
namespace bpl {

bool get_string_value_dm(std::string attr, std::string dev_string, std::string &value)
{
    std::string dm                 = "DeviceInfo";
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

#if USE_PRPLMESH_WHM
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
#endif

bool get_serial_number(std::string &serial_number)
{
    std::string attr       = "SerialNumber";
    std::string dev_string = "0.'DeviceInfo.'";

#if USE_PRPLMESH_WHM
    if (!get_string_value_dm(attr, serial_number)) {
        serial_number.assign("prplmesh12345");
    }
#else
    if (!get_string_value_dm(attr, dev_string, serial_number)) {
        serial_number.assign("prplmesh12345");
    }
#endif
    return true;
}

bool get_software_version(std::string &software_version)
{
    std::string attr       = "SoftwareVersion";
    std::string dev_string = "0.'DeviceInfo.'";

#if USE_PRPLMESH_WHM
    if (!get_string_value_dm(attr, software_version)) {
        std::string version_string = beerocks::version::get_module_version();
        software_version.assign(version_string);
    }
#else
    if (!get_string_value_dm(attr, dev_string, software_version)) {
        std::string version_string = beerocks::version::get_module_version();
        software_version.assign(version_string);
    }
#endif
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
