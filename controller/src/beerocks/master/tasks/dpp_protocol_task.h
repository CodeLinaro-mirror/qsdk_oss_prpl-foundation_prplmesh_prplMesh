/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _DPP_PROTOCOL_TASK_H_
#define _DPP_PROTOCOL_TASK_H_

#include "../controller_dpp_protocol.h"
#include "../controller_dpp_session.h"
#include "../db/db.h"
#include "task.h"

namespace son {

class dpp_protocol_task : public task {
public:
    dpp_protocol_task(db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_);

    bool handle_ieee1905_1_msg(const sMacAddr &src_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx) override;

    void work() override {}

    /**
     * @brief Build a controller-native DPP Authentication Request and send it using the
     *        standard PROXIED_ENCAP_DPP_MESSAGE to DPP-capable agents.
     */
    bool send_dpp_authentication_request();

private:
    bool handle_cmdu_1905_chirp_notification(const sMacAddr &src_mac,
                                             ieee1905_1::CmduMessageRx &cmdu_rx);
    bool send_proxied_encap_dpp_to_agent(const sMacAddr &agent_mac,
                                         const db::sProxiedEncapDppMessage &message);

    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    controller_dpp::DppConfiguratorSession m_configurator;
    controller_dpp::SessionState m_session;
    const db::sDppBootstrappingInfo *m_matched_bootstrap = nullptr;
};

} // namespace son

#endif // _DPP_PROTOCOL_TASK_H_
