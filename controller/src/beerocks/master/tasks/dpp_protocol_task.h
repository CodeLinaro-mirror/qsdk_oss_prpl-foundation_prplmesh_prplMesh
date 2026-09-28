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
     * @brief Mark the Multi-AP Agent for Direct Encap onboarding (logical Ethernet).
     *
     * Does not send Authentication Request; call start_direct_encap_onboarding()
     * or send_dpp_authentication_request() after bootstrap is matched.
     */
    bool set_direct_encap_onboarding_agent(const sMacAddr &agent_mac);

    /**
     * @brief Start Direct Encap onboarding: resolve bootstrap, send Auth Request
     *        as DIRECT_ENCAP_DPP_MESSAGE (tlvDppMessage only; no chirp TLV).
     *
     * Bootstrap resolution (existing db APIs):
     *   1. get_dpp_bootstrap_info_by_mac(agent_mac)
     *   2. else get_sole_dpp_bootstrap_info() when exactly one URI is provisioned
     */
    bool start_direct_encap_onboarding(const sMacAddr &agent_mac);

    /**
     * @brief Current protocol state retained for dpp_onboarding_task.
     */
    const controller_dpp::SessionState &session_state() const { return m_session; }

    /**
     * @brief Clear session + Configurator crypto state (URI replace / success / fail cleanup).
     */
    void reset_session();
    /**
     * @brief Wire progress events into dpp_onboarding_task via task_pool::push_event.
     */
    void configure_onboarding_notifier(task_pool &pool, int onboarding_task_id);

    /**
     * @brief Optionally stage Config Object JSON blobs for the next GAS Configuration Request. 
     *
     * If empty on Config Request RX, objects are built automatically from Controller BSS
     * policy (C-4: mapAgent / mapBackhaulSta / sta) using C-sign from the DPP keystore (C-6).
     * Staged objects override the policy builder when present.
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
    bool handle_cmdu_1905_direct_encap_dpp(const sMacAddr &src_mac,
                                           ieee1905_1::CmduMessageRx &cmdu_rx);
    bool send_dpp_authentication_confirm(const sMacAddr &enrollee_mac,
                                         std::vector<uint8_t> auth_confirm_frame);
    bool send_proxied_encap_dpp_to_agent(const sMacAddr &agent_mac,
                                         const db::sProxiedEncapDppMessage &message);
    bool send_direct_encap_dpp_to_agent(const sMacAddr &agent_mac,
                                        const db::sDirectEncapDppMessage &message);
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
