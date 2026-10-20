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

#include <functional>
#include <vector>

namespace prplmesh {
namespace agent {
namespace actions {

/**
 * @brief Callback type used by the WPS action handler.
 * Queues WPS PBC on the BackhaulManager thread (AP vs bSTA decided there).
 *
 * @return true when accepted, false if the worker is unavailable or its queue is full.
 */
using WpsAutoCb = std::function<bool()>;

/**
 * @brief Install the callback.
 * Must be set during agent startup (after BackhaulManager is constructed).
 */
void set_wps_callback(WpsAutoCb auto_cb);

/**
 * @brief Register the NBAPI functions exposed by the Agent.
 */
std::vector<beerocks::nbapi::sFunctions> get_func_list(void);

} // namespace actions
} // namespace agent
} // namespace prplmesh
#endif // ON_ACTION_H
