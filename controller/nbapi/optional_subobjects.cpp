/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "optional_subobjects.h"

namespace prplmesh {
namespace controller {
namespace actions {

std::vector<beerocks::nbapi::sOptionalSubobject> get_optional_subobject_list(void)
{
    constexpr char device[] = DATAELEMENTS_ROOT_DM ".Network.Device.{i}.";
    constexpr char radio[]  = DATAELEMENTS_ROOT_DM ".Network.Device.{i}.Radio.{i}.";
    constexpr char capabilities[] =
        DATAELEMENTS_ROOT_DM ".Network.Device.{i}.Radio.{i}.Capabilities.";
    constexpr char bss[] = DATAELEMENTS_ROOT_DM ".Network.Device.{i}.Radio.{i}.BSS.{i}.";

    return {
        {device, "bSTAMLD"},
        {radio, "SpatialReuse"},
        {radio, "X_PRPLWARE-COM_CACCompletion"},
        {capabilities, "WiFi6APRole"},
        {capabilities, "WiFi6bSTARole"},
        {capabilities, "WiFi7APRole"},
        {capabilities, "WiFi7bSTARole"},
        {bss, "SetQoSManagementInput"},
    };
}

} // namespace actions
} // namespace controller
} // namespace prplmesh
