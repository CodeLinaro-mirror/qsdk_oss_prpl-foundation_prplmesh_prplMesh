/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_service_helper.h"

#include <bcl/beerocks_version.h>
#include <bpl/bpl_cfg.h>
#include <mapf/common/logger.h>

namespace beerocks {
namespace bpl {

namespace {

constexpr const char *DEVICE_INFO_PATH = "DeviceInfo.";

template <typename T> bool read_device_info_param(const std::string &name, T &value)
{
    return read_param_via_common_socket(DEVICE_INFO_PATH, name, value);
}

} // namespace

static bool get_string_value_dm(const std::string &attr, std::string &value)
{
    value = "invalid";

    if (!read_device_info_param(attr, value)) {
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

} // namespace bpl
} // namespace beerocks
