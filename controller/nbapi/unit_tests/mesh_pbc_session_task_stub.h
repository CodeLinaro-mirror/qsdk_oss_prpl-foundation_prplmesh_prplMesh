/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef MESH_PBC_SESSION_TASK_STUB_H
#define MESH_PBC_SESSION_TASK_STUB_H

#include <memory>

namespace son {
class PBCOnboardingTask;
} // namespace son

namespace prplmesh {
namespace controller {
namespace actions {

/**
 * The session engine that the stub of get_pbc_onboarding_task() returns. nullptr: no engine.
 */
extern std::shared_ptr<son::PBCOnboardingTask> g_stub_pbc_onboarding_task;

} // namespace actions
} // namespace controller
} // namespace prplmesh

#endif // MESH_PBC_SESSION_TASK_STUB_H
