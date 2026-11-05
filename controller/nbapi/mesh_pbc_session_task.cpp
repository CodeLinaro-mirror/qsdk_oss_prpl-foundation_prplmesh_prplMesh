/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_session_actions.h"
#include "on_action.h"

namespace prplmesh {
namespace controller {
namespace actions {

std::shared_ptr<son::PBCOnboardingTask> get_pbc_onboarding_task(void)
{
    if (!g_database) {
        return nullptr;
    }
    auto controller_ctx = g_database->get_controller_ctx();
    if (!controller_ctx) {
        return nullptr;
    }
    return controller_ctx->get_pbc_onboarding_task();
}

} // namespace actions
} // namespace controller
} // namespace prplmesh
