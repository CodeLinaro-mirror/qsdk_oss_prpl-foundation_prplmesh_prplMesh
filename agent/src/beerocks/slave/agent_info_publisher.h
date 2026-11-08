/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _AGENT_INFO_PUBLISHER_H_
#define _AGENT_INFO_PUBLISHER_H_

namespace beerocks {

#define EACH_SLAVESTATE(DO)                                                                        \
    DO(STATE_WAIT_BEFORE_INIT)                                                                     \
    DO(STATE_INIT)                                                                                 \
    DO(STATE_LOAD_PLATFORM_CONFIGURATION)                                                          \
    DO(STATE_CONNECT_TO_PLATFORM_MANAGER)                                                          \
    DO(STATE_WAIT_FOR_PLATFORM_MANAGER_REGISTER_RESPONSE)                                          \
    DO(STATE_CONNECT_TO_BACKHAUL_MANAGER)                                                          \
    DO(STATE_WAIT_RETRY_CONNECT_TO_BACKHAUL_MANAGER)                                               \
    DO(STATE_WAIT_FOR_BACKHAUL_MANAGER_REGISTER_RESPONSE)                                          \
    DO(STATE_JOIN_INIT)                                                                            \
    DO(STATE_WAIT_FOR_FRONTHAUL_THREADS_JOINED)                                                    \
    DO(STATE_BACKHAUL_ENABLE)                                                                      \
    DO(STATE_SEND_BACKHAUL_MANAGER_ENABLE)                                                         \
    DO(STATE_WAIT_FOR_BACKHAUL_MANAGER_CONNECTED_NOTIFICATION)                                     \
    DO(STATE_BACKHAUL_MANAGER_CONNECTED)                                                           \
    DO(STATE_WAIT_FOR_AUTO_CONFIGURATION_COMPLETE)                                                 \
    DO(STATE_OPERATIONAL)                                                                          \
    DO(STATE_STOPPED)

enum eSlaveState : int {
#define DEFINE_eSlaveState(X) X,
    EACH_SLAVESTATE(DEFINE_eSlaveState)
#undef DEFINE_eSlaveState
};

class AgentInfoPublisher {
public:
    eSlaveState operator=(eSlaveState state);
    operator eSlaveState() const { return cur; }

    /**
     * @brief Publish the current and best states and ControllerConnected to AgentDM.
     *
     * Failed writes are retried on the next call. Must be called outside AgentDB scopes.
     */
    void publish(bool desired_controller_connected);

private:
    void publish_state();

    eSlaveState cur = {};
    eSlaveState max = {};
    bool changed    = false;

    // ControllerConnected value last written to AgentDM, -1 before the first successful write.
    int m_published_controller_connected = -1;
};

} // namespace beerocks

#endif // _AGENT_INFO_PUBLISHER_H_
