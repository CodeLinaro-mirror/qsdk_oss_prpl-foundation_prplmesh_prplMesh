/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "templates_actions_stub.h"

#include "on_action.h"
#include "templates_actions.h"

namespace prplmesh {
namespace controller {
namespace actions {

unsigned g_stub_templates_apply_count = 0;

bool is_templates_dm_initialized() { return true; }

bool templates_events_enabled(void)
{
    return g_database && g_database->config.use_dataelements_vap_configs;
}

void templates_request_apply(void) { ++g_stub_templates_apply_count; }

} // namespace actions
} // namespace controller
} // namespace prplmesh
