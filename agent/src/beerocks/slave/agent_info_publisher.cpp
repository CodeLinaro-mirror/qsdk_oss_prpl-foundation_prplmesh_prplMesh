/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "agent_info_publisher.h"

#include "agent_dm.h"

#include <algorithm>
#include <string>

using namespace beerocks;

auto AgentInfoPublisher::operator=(eSlaveState state) -> eSlaveState
{
    cur     = state;
    max     = std::min(STATE_OPERATIONAL, std::max(max, cur));
    changed = true;

    return state;
}

void AgentInfoPublisher::publish_state()
{
    if (!changed) {
        return;
    }
    auto to_string = [](eSlaveState e) {
        const char *arr[] = {
#define DEFINE_eSlaveState_to_string(x) #x " (",
            EACH_SLAVESTATE(DEFINE_eSlaveState_to_string)
#undef DEFINE_eSlaveState_to_string
        };

        return arr[e] + (sizeof("STATE_") - 1) + std::to_string(e) + ')';
    };

    changed = !AgentDM::get()->dm_set_agent_state(to_string(cur), to_string(max));
}

void AgentInfoPublisher::publish(bool desired_controller_connected)
{
    auto dm = AgentDM::get();
    publish_state();
    if (m_published_controller_connected != int(desired_controller_connected) &&
        dm->dm_set_controller_connected(desired_controller_connected)) {
        m_published_controller_connected = desired_controller_connected;
    }
}
