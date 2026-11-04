/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "son_actions_stub.h"

#include "son_actions.h"

#include <tlvf/CmduMessageRx.h>
#include <tlvf/wfa_map/tlvMultiApPushButtonTrigger.h>

#include <tuple>

namespace son {

std::vector<sStubSentCmdu> g_stub_sent_cmdus;
sMacAddr g_stub_refused_destination{};

bool son_actions::send_cmdu_to_agent(const sMacAddr &dest_mac, ieee1905_1::CmduMessageTx &cmdu_tx,
                                     db &database, const std::string &radio_mac)
{
    if (dest_mac == g_stub_refused_destination) {
        return false;
    }
    if (!cmdu_tx.finalize()) {
        return false;
    }
    std::vector<uint8_t> buffer(cmdu_tx.getMessageBuff(),
                                cmdu_tx.getMessageBuff() + cmdu_tx.getMessageLength());
    ieee1905_1::CmduMessageRx cmdu_rx(buffer.data(), buffer.size());
    if (!cmdu_rx.parse()) {
        return false;
    }

    sStubSentCmdu sent;
    sent.destination = dest_mac;
    sent.type        = cmdu_rx.getMessageType();
    auto tlv         = cmdu_rx.getClass<wfa_map::tlvMultiApPushButtonTrigger>();
    if (tlv) {
        sent.trigger    = true;
        sent.session_id = tlv->pbc_session_id();
        sent.wps        = tlv->push_button_method().wps;
        sent.dpp        = tlv->push_button_method().dpp;
        sent.start      = (tlv->state() == wfa_map::tlvMultiApPushButtonTrigger::eState::START);
        for (size_t i = 0; i < tlv->bssid_list_length(); i++) {
            auto entry = tlv->bssid_list(i);
            if (std::get<0>(entry)) {
                sent.bssids.push_back(std::get<1>(entry));
            }
        }
    }
    g_stub_sent_cmdus.push_back(sent);
    return true;
}

} // namespace son
