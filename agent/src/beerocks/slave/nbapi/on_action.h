/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef ON_ACTION_H
#define ON_ACTION_H

#include "agent_db.h"

namespace prplmesh {
namespace agent {
namespace actions {

/**
 * @brief Callback type used by the WPS action handler.
 * Should run WPS PBC automatically (AP vs bSTA decided internally).
 *
 * @return true on success, false on failure.
 */
using WpsAutoCb = std::function<bool()>;

/**
 * @brief Callback type used by the Configuration handler. Should be run when
 * Configuration changes to prompt the BH manager to (re)connect to the new BH
 * AP.
 *
 * @return true on success, false on failure.
 */
using ConnectCb = std::function<bool()>;

/**
 * @brief Install the callback.
 * Must be set during agent startup (after BackhaulManager is constructed).
 */
void set_wps_callback(WpsAutoCb auto_cb);

/**
 * @brief Install the callback.
 * Must be set during agent startup (after BackhaulManager is constructed).
 */
void set_connect_callback(WpsAutoCb cb);

/**
 * @brief Register the NBAPI events exposed by the Agent.
 */
std::vector<beerocks::nbapi::sEvents> get_events_list(void);

/**
 * @brief Register the NBAPI functions exposed by the Agent.
 */
std::vector<beerocks::nbapi::sFunctions> get_func_list(void);

} // namespace actions
} // namespace agent
} // namespace prplmesh
#endif // ON_ACTION_H
