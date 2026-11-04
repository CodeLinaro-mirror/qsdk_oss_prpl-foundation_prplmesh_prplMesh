/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_session_task_stub.h"

#include "mesh_pbc_session_actions.h"

namespace prplmesh {
namespace controller {
namespace actions {

std::shared_ptr<son::PBCOnboardingTask> g_stub_pbc_onboarding_task;

std::shared_ptr<son::PBCOnboardingTask> get_pbc_onboarding_task(void)
{
    return g_stub_pbc_onboarding_task;
}

} // namespace actions
} // namespace controller
} // namespace prplmesh
