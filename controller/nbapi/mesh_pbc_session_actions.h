/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef MESH_PBC_SESSION_ACTIONS_H
#define MESH_PBC_SESSION_ACTIONS_H

#include "ambiorix_impl.h"

#include <memory>
#include <vector>

namespace son {
class PBCOnboardingTask;
} // namespace son

namespace prplmesh {
namespace controller {
namespace actions {

/**
 * @brief Get the push-button session methods (InitiatePBC() and TerminatePBC()) for
 * get_func_list().
 *
 * An InitiatePBC() call that starts a session gets its reply when the session ends (deferred
 * RPC). A call that the session engine rejects gets its reply at once. Both give the result in
 * the out arguments Status and FailureReason (plus EnrolleeMACAddress and BSSID on Success, and
 * PBCSessionID when a session was created), with amxd status OK. TerminatePBC() returns
 * amxd_status_invalid_value when it fails.
 */
std::vector<beerocks::nbapi::sFunctions> get_mesh_pbc_session_func_list(void);

/**
 * @brief Remove the open InitiatePBC() call, if any. Call it at the Controller stop, before main()
 * returns, so that no deferred call refers to the state of this file after that.
 */
void mesh_pbc_session_actions_cleanup(void);

/**
 * @brief End the active push-button session, if any, because a Templates.Network change event
 * has Enable false. Nothing happens if no session runs. The open InitiatePBC() call returns
 * Error_Other.
 */
void mesh_pbc_templates_network_disabled(void);

/**
 * @brief Get the push-button session engine of the Controller.
 *
 * @return The PBCOnboardingTask of the Controller, or nullptr if no Controller runs it.
 */
std::shared_ptr<son::PBCOnboardingTask> get_pbc_onboarding_task(void);

} // namespace actions
} // namespace controller
} // namespace prplmesh

#endif // MESH_PBC_SESSION_ACTIONS_H
