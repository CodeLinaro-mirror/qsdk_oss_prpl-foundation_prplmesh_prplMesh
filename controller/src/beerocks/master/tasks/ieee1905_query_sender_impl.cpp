/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "ieee1905_query_sender_impl.h"
#include "../son_actions.h"

using namespace son;

RealIEEE1905QuerySender::RealIEEE1905QuerySender(db &database) : database(database) {}

bool RealIEEE1905QuerySender::send_topology_query(const sMacAddr &dest_mac,
                                                  ieee1905_1::CmduMessageTx &cmdu_tx)
{
    return son_actions::send_topology_query_msg(dest_mac, cmdu_tx, database);
}

bool RealIEEE1905QuerySender::send_higher_layer_query(const sMacAddr &dest_mac,
                                                      ieee1905_1::CmduMessageTx &cmdu_tx)
{
    if (!cmdu_tx.create(0, ieee1905_1::eMessageType::HIGHER_LAYER_QUERY_MESSAGE)) {
        LOG(ERROR) << "Failed building HIGHER_LAYER_QUERY_MESSAGE";
        return false;
    }

    return son_actions::send_cmdu_to_agent(dest_mac, cmdu_tx, database);
}
