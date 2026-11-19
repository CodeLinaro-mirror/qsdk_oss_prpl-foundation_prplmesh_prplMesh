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
#include <chrono>
#include <string>
#include <unordered_map>

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
     *
     * @note Direct Encap production trigger is out of scope for PPM-4079 / FEAT-68
     * Phase 1 (proxied chirp path only). APIs are retained for a later FEAT.
     */
    bool set_direct_encap_onboarding_agent(const sMacAddr &agent_mac);

    /**
     * @brief Start Direct Encap onboarding: resolve bootstrap, send Auth Request
     *        as DIRECT_ENCAP_DPP_MESSAGE (tlvDppMessage only; no chirp TLV).
     *
     * Bootstrap resolution (existing db APIs):
     *   1. get_dpp_bootstrap_info_by_mac(agent_mac)
     *   2. else get_sole_dpp_bootstrap_info() when exactly one URI is provisioned
     *
     * @note Not wired to a controller production trigger in this MR (see
     * set_direct_encap_onboarding_agent()).
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
     * policy (mapAgent / mapBackhaulSta / sta) using C-sign from the DPP keystore.
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
    /**
     * @brief Send a 1905 ACK_MESSAGE (EasyMesh v6.1 section 17.1.32) with @a mid to @a dest_mac.
     */
    bool send_1905_ack(const sMacAddr &dest_mac, uint16_t mid);

    /**
     * @brief Chirp guard: one DPP session at a time, first Proxy Agent wins.
     *
     * An Enrollee chirp is usually heard by several Agents (for example the Controller's
     * co-located Agent and a daisy-chained Agent), so the same URI arrives in several
     * Chirp Notifications within milliseconds. Restarting the session for each of them
     * sends a second Authentication Request with a new nonce through another Proxy Agent;
     * the Enrollee answers only the first one and the exchange fails.
     *
     * @return true if the chirp must be ignored because a session is in progress. A
     * session that got no Authentication Response within five seconds (Wi-Fi Easy Connect
     * v3.0 section 6.3.1.2) is taken over by the next chirp (returns false).
     */
    bool chirp_blocked_by_active_session(const sMacAddr &src_mac,
                                         const db::sDppBootstrappingInfo &info) const;
    /**
     * @brief Bootstrapping entry of the current session, looked up by alias.
     * @return nullptr when no session is active or the URI was removed or replaced.
     */
    const db::sDppBootstrappingInfo *matched_bootstrap() const;
    /** @brief URI M: (Enrollee MAC) of the current session entry, ZERO_MAC when unknown. */
    sMacAddr matched_bootstrap_mac() const;
    /** @brief Remember the session entry by alias and public key (no pointer into the db). */
    void set_matched_bootstrap(const db::sDppBootstrappingInfo &info);
    /**
     * @brief Replay the cached Configuration Response for a retransmitted GAS Configuration
     * Request (same Enrollee, identical request bytes) of the current session.
     * @return true if the request was a retransmission and was handled.
     */
    bool replay_cached_configuration_response(const sMacAddr &enrollee_mac,
                                              const std::vector<uint8_t> &request);
    /**
     * @brief Decide whether an already onboarded bootstrapping entry is onboarded again.
     *
     * The Enrollee MAC is taken from the Chirp Notification, else from the URI M: field,
     * else from the onboarding session. The entry is onboarded again only when that MAC is
     * not a connected station in the current topology (backhaul lost, re-chirping).
     *
     * @param[out] enrollee_mac MAC that was checked (ZERO_MAC when none is known).
     * @return true if a new session must be started.
     */
    bool onboarded_enrollee_absent(const db::sDppBootstrappingInfo &info,
                                   const sMacAddr &chirp_enrollee, sMacAddr &enrollee_mac);
    /** @brief Enrollee MAC of the current session (ZERO_MAC when unknown). */
    sMacAddr session_enrollee_mac() const;
    /** @brief Mark the matched bootstrapping entry onboarded and record its Enrollee MAC. */
    void mark_matched_bootstrap_onboarded();
    void push_dpp_onboarding_task_event(int event_type, const std::string &reason = {});
    /**
     * @brief Log a Chirp Notification whose hash matches no bootstrapping URI.
     *
     * An Enrollee chirps every few seconds until it is onboarded and every Proxy Agent that
     * hears it sends a Chirp Notification (EasyMesh v6.1 section 5.3.4), so the same unknown
     * hash arrives many times a minute. The first one per hash is logged as a warning with
     * the bootstrapping table, repeats at debug level, and a summary with the repeat count
     * once per minute. Logging only: every notification is still processed.
     */
    void log_unmatched_chirp(const sMacAddr &src_mac, const std::string &hash_hex, size_t hash_len);
    db &m_database;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    controller_dpp::DppConfiguratorSession m_configurator;
    controller_dpp::SessionState m_session;
    // Session URI by alias + public key: a pointer into the bootstrap store would dangle when
    // the entry is removed or replaced during the session.
    std::string m_matched_alias;
    std::string m_matched_public_key;
    task_pool *m_task_pool            = nullptr;
    int m_dpp_onboarding_task_id      = -1;
    bool m_active_request_conn_status = false;

    /** Log state of one unmatched chirp hash (see log_unmatched_chirp()). */
    struct sUnmatchedChirp {
        uint32_t repeats = 0; ///< Notifications since the last report.
        std::chrono::steady_clock::time_point last_report;
        std::chrono::steady_clock::time_point last_seen;
        size_t bootstrap_entries = 0; ///< URI count at the last table dump.
    };
    /** Unmatched chirp hashes by hex string; kept across DPP sessions. */
    std::unordered_map<std::string, sUnmatchedChirp> m_unmatched_chirps;
};

} // namespace son

#endif // _DPP_PROTOCOL_TASK_H_
