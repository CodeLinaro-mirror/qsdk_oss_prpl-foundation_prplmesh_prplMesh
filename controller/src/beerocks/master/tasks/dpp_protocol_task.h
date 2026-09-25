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
#include <string>

namespace son {

class task_pool;

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

    /**
     * @brief Current protocol state retained for dpp_onboarding_task.
     */
    const controller_dpp::SessionState &session_state() const { return m_session; }

    /**
     * @brief Clear session + Configurator crypto state (URI replace / success / fail cleanup).
     */
    void reset_session();

    /**
     * @brief Stage Config Object JSON blobs for the next GAS Configuration Request.
     *
     * Objects must already be built (signed Connector / C-sign) by the Configurator
     * policy path (C-4/C-6). On Config Request RX, dpp_protocol_task sends them via
     * PROXIED_ENCAP_DPP_MESSAGE (DPP_GAS_FRAME).
     */
    void set_pending_configuration_objects(std::vector<std::string> config_object_jsons,
                                           bool send_conn_status = false);

    /**
     * @brief Build GAS Configuration Response and send PROXIED_ENCAP_DPP_MESSAGE.
     */
    bool send_dpp_configuration_response(const std::vector<std::string> &config_object_jsons,
                                         bool send_conn_status = false);
private:
    bool handle_cmdu_1905_chirp_notification(const sMacAddr &src_mac,
                                             ieee1905_1::CmduMessageRx &cmdu_rx);
    bool handle_cmdu_1905_proxied_encap_dpp(const sMacAddr &src_mac,
                                            ieee1905_1::CmduMessageRx &cmdu_rx);
    bool send_dpp_authentication_confirm(const sMacAddr &enrollee_mac,
                                         std::vector<uint8_t> auth_confirm_frame);
    bool send_proxied_encap_dpp_to_agent(const sMacAddr &agent_mac,
                                         const db::sProxiedEncapDppMessage &message);

    void push_dpp_onboarding_task_event(int event_type, const std::string &reason = {});
    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    controller_dpp::DppConfiguratorSession m_configurator;
    controller_dpp::SessionState m_session;
    const db::sDppBootstrappingInfo *m_matched_bootstrap = nullptr;
    task_pool *m_task_pool                               = nullptr;
    int m_dpp_onboarding_task_id                         = -1;
    bool m_active_request_conn_status                    = false;
};

} // namespace son

#endif // _DPP_PROTOCOL_TASK_H_
