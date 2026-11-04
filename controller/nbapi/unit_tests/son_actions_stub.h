/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef SON_ACTIONS_STUB_H
#define SON_ACTIONS_STUB_H

#include <tlvf/common/sMacAddr.h>
#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/wfa_map/sPbcSessionId.h>

#include <vector>

namespace son {

/**
 * A message that the stub of son_actions::send_cmdu_to_agent() accepted.
 */
struct sStubSentCmdu {
    sMacAddr destination{};
    ieee1905_1::eMessageType type = ieee1905_1::eMessageType::ACK_MESSAGE;
    /** true if the message has a Multi-AP Push-Button Trigger TLV. */
    bool trigger = false;
    /** Values of the Trigger TLV, valid if trigger is true. */
    wfa_map::sPbcSessionId session_id{};
    bool wps   = false;
    bool dpp   = false;
    bool start = false;
    std::vector<sMacAddr> bssids;
};

/**
 * Messages that the stub of son_actions::send_cmdu_to_agent() accepted, in send order.
 */
extern std::vector<sStubSentCmdu> g_stub_sent_cmdus;

/**
 * The stub of son_actions::send_cmdu_to_agent() refuses each message to this AL MAC address
 * (returns false and records nothing). ZERO_MAC: it refuses no message.
 */
extern sMacAddr g_stub_refused_destination;

} // namespace son

#endif // SON_ACTIONS_STUB_H
