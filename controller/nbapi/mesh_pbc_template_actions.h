/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef MESH_PBC_TEMPLATE_ACTIONS_H
#define MESH_PBC_TEMPLATE_ACTIONS_H

#include "ambiorix_impl.h"

#include <string>
#include <vector>

namespace prplmesh {
namespace controller {
namespace actions {

std::vector<beerocks::nbapi::sActionsCallback> get_mesh_pbc_template_actions_list(void);

} // namespace actions
} // namespace controller
} // namespace prplmesh

#endif // MESH_PBC_TEMPLATE_ACTIONS_H
