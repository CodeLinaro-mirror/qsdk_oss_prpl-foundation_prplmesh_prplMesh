/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef ON_ACTION_H
#define ON_ACTION_H

#include "ambiorix_impl.h"

#include "../src/beerocks/master/db/db.h"
#include "../src/beerocks/master/son_actions.h"

namespace prplmesh {
namespace controller {
namespace actions {

std::vector<beerocks::nbapi::sActionsCallback> get_actions_callback_list(void);
std::vector<beerocks::nbapi::sEvents> get_events_list(void);
std::vector<beerocks::nbapi::sFunctions> get_func_list(void);
beerocks::nbapi::ambiorix_func_ptr get_access_point_commit(void);

void templates_commit_request(void);

void templates_commit_apply_pending(void);

void templates_schedule_commit_apply(void);

void templates_restage_only(void);

bool is_templates_dm_initialized();

void set_templates_dm_initialized(bool val);

extern son::db *g_database;

/**
* dwell time (40 milliseconds)
*/
constexpr int PREFERRED_DWELLTIME_MS = 40;

} // namespace actions
} // namespace controller
} // namespace prplmesh
#endif // ON_ACTION_H
