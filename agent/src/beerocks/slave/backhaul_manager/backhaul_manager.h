/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BACKHAUL_MANAGER_H
#define _BACKHAUL_MANAGER_H

#include "../tasks/task_pool.h"
#if defined(USE_PRPLMESH_WHM)
#include "dpp_cce_targets.h"
#include "dpp_enrollee_channel_pair.h"
#include "dpp_enrollee_keystore.h"
#endif
#include "fdb_monitor.h"
#include "wan_monitor.h"

#include <bcl/beerocks_backport.h>
#include <bcl/beerocks_cmdu_client_factory.h>
#include <bcl/beerocks_cmdu_server.h>
#include <bcl/beerocks_config_file.h>
#include <bcl/beerocks_defines.h>
#include <bcl/beerocks_eventloop_thread.h>
#include <bcl/beerocks_timer_manager.h>
#include <bcl/beerocks_ucc_server.h>
#include <bcl/network/network_utils.h>
#include <bcl/network/sockets_impl.h>
#include <btl/broker_client.h>
#include <btl/broker_client_factory.h>
#include <bwl/sta_wlan_hal.h>

#include <beerocks/tlvf/beerocks_message_header.h>

#include <tlvf/CmduMessageTx.h>
#include <tlvf/wfa_map/tlvErrorCode.h>

#include "../agent_db.h"
#include "../agent_ucc_listener.h"

#include <chrono>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <random>

namespace beerocks {

namespace bpl {
enum class eErrorCode;
}

class ChannelSelectionTask;

class BackhaulManager : public EventLoopThread {

public:
    BackhaulManager(const config_file::sConfigSlave &config,
                    const std::set<std::string> &slave_ap_ifaces_,
                    const std::set<std::string> &slave_sta_ifaces_, int stop_on_failure_attempts_);
    ~BackhaulManager();

    /**
     * @brief Initialize backhaul manager.
     *
     * @return true on success and false otherwise.
     */
    virtual bool thread_init() override;

    /**
     * @brief Stops backhaul manager.
     */
    virtual void on_thread_stop() override;

    /**
     * @brief Sends given CMDU message through the specified socket connection.
     *
     * @param fd File descriptor of the connected socket.
     * @param cmdu_tx CMDU message to send.
     * @return true on success and false otherwise.
     */
    bool send_cmdu(int fd, ieee1905_1::CmduMessageTx &cmdu_tx);

    /**
     * @brief Forwards given received CMDU message through the specified socket connection.
     *
     * @param fd File descriptor of the connected socket.
     * @param iface_index Index of the network interface that the CMDU message was received on.
     * @param dst_mac Destination MAC address.
     * @param src_mac Source MAC address.
     * @param cmdu_rx Received CMDU message to forward.
     * @return true on success and false otherwise.
     */
    bool forward_cmdu_to_uds(int fd, uint32_t iface_index, const sMacAddr &dst_mac,
                             const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Sends CMDU to transport for dispatching.
     *
     * @param cmdu CMDU message to send.
     * @param dst_mac Destination MAC address.
     * @param src_mac Source MAC address.
     * @param iface_name Name of the network interface to use (set to empty string to send on all
     * available interfaces).
     * @param iface_type Type of the interface identified by iface_name.
     * @return true on success and false otherwise.
     */
    bool
    send_cmdu_to_broker(ieee1905_1::CmduMessageTx &cmdu, const sMacAddr &dst_mac,
                        const sMacAddr &src_mac, const std::string &iface_name = "",
                        beerocks::transport::messages::CmduTxMessage::InterfaceType iface_type =
                            beerocks::transport::messages::CmduTxMessage::IF_TYPE_NONE);

    /**
     * @brief Sends ACK CMDU to controller
     *
     * @param cmdu CMDU message to send.
     * @param mid The message id to attach to the ACK
     * @return true on success and false otherwise.
     */
    bool send_ack_to_controller(ieee1905_1::CmduMessageTx &cmdu_tx, uint32_t mid);

    /**
     * @brief Forwards given received CMDU message to the broker server for dispatching.
     *
     * @param cmdu_rx Received CMDU message to forward.
     * @param dst_mac Destination MAC address.
     * @param src_mac Source MAC address.
     * @param iface_name Name of the network interface to use (set to empty string to send on all
     * available interfaces).
     * @return true on success and false otherwise.
     */
    bool forward_cmdu_to_broker(ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &dst_mac,
                                const sMacAddr &src_mac, const std::string &iface_name = "");

    // For agent_ucc_listener
    /**
     * @brief get radio mac (ruid) of registered slave based on frequency type
     * 
     * @param freq radio frequency to search
     * @return radio mac of the found slave if found, otherwise empty string
     */
    std::string freq_to_radio_mac(eFreqType freq) const;

    /**
     * @brief start WPS PBC
     * 
     * @param radio_mac radio mac of the radio on which to start WPS
     * @return true on success, false on failure
     */
    bool start_wps_pbc(const sMacAddr &radio_mac);

    /**
     * @brief Automatically initiates WPS Push Button Connect (PBC) based on agent state.
     *
     * This function determines the appropriate WPS PBC path depending on the
     * agent's current operational state:
     *  - If the agent is operational (fronthaul APs active), WPS PBC is triggered
     *    on all fronthaul AP interfaces.
     *  - If the agent is not operational (backhaul STA onboarding or disconnected),
     *    WPS PBC is triggered on Endpoint (bSTA) interface(s).
     *
     * @return true on success, false on failure.
     */
    bool initiate_wps_pbc_auto();

    /** Queue WPS work for this thread; true means accepted, not completed. */
    bool enqueue_wps_request();

    /**
     * @brief Triggers WPS PBC (backhaul STA) on provided frequency
     *
     * @param freq frequency on which WPS PBC will be triggerd
     * Default frequency is 5G
     * @return true if at least one fronthaul radio successfully accepted WPS PBC, false otherwise.
     */
    bool start_wps_pbc_ep_freq(eFreqType freq = eFreqType::FREQ_5G);

    /**
     * @brief Start WPS PBC on a specific fronthaul AP radio.
     *
     * Sends a @c cACTION_BACKHAUL_START_WPS_PBC_REQUEST message to the slave
     * managing the given @p radio_mac.
     *
     * @param radio_mac MAC address of the target AP radio.
     * @return true on success, false if the radio is not found or message send fails.
     */
    bool start_wps_pbc_ap(const sMacAddr &radio_mac);

    /**
     * @brief Start WPS PBC on the backhaul STA interface.
     *
     * This initiates WPS PBC on the all radios
     * @param radio_mac MAC address of the target EP radio.
     * @return true on success, false if the STA HAL is not available or WPS start fails.
     */
    bool start_wps_pbc_ep(const sMacAddr &radio_mac);

    /**
     * @brief Start WPS PBC on the backhaul STA interface.
     *
     * This initiates WPS PBC on the currently selected backhaul STA.
     * @param radio_mac MAC address of the target EP radio.
     * @return true on success, false if the STA HAL is not available or WPS start fails.
     */
    bool start_wps_pbc_sta();

    /**
     * @brief set MBO AssocDisallow property
     * 
     * @param radio_mac radio mac of the radio on which to set the MBO
     * @param bssid mac of the VAP on which to set the property or ZERO_MAC for whole radio
     * @param enable enable or disable the MBO AssocDisallow property
     * @return true on success, false on failure
     */
    bool set_mbo_assoc_disallow(const sMacAddr &radio_mac, const sMacAddr &bssid, bool enable);

    // Forward declaration
    struct sRadioInfo;

private:
    std::shared_ptr<bwl::sta_wlan_hal> get_selected_backhaul_sta_wlan_hal();

    /**
     * @brief Handles the client-disconnected event in the CMDU server.
     *
     * @param fd File descriptor of the socket that got disconnected.
     */
    void handle_disconnected(int fd);

    /**
     * @brief Handles received CMDU message.
     *
     * @param fd File descriptor of the socket connection the CMDU was received through.
     * @param iface_index Index of the network interface that the CMDU message was received on.
     * @param dst_mac Destination MAC address.
     * @param src_mac Source MAC address.
     * @param cmdu_rx Received CMDU to be handled.
     * @return true on success and false otherwise.
     */
    bool handle_cmdu(int fd, uint32_t iface_index, const sMacAddr &dst_mac, const sMacAddr &src_mac,
                     ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Handles CMDU message received from broker.
     *
     * This handler is slightly different than the handler for CMDU messages received from other
     * processes as it checks the source and destination MAC addresses set by the original sender.
     * It also filters out messages that are not addressed to the controller.
     *
     * @param iface_index Index of the network interface that the CMDU message was received on.
     * @param dst_mac Destination MAC address.
     * @param src_mac Source MAC address.
     * @param cmdu_rx Received CMDU to be handled.
     * @return true on success and false otherwise.
     */
    bool handle_cmdu_from_broker(uint32_t iface_index, const sMacAddr &dst_mac,
                                 const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

    void prepare_dpp_enrollee();
    /** Start or advance the bounded first-boot DPP chirp campaign. Called in WAIT_WPS only. */
    void run_dpp_auto_chirp();
    /**
     * True while the DPP auto-chirp campaign defers the expired WAIT_WPS timeout: during a chirp
     * pass, the pause before the next pass of the cycle and a WATCH round that requested a CCE
     * scan (all three only while the Agent is an unconfigured Enrollee) and during a
     * chirp-triggered exchange. Never for more than DPP_CHIRP_HOLD_MAX_SECONDS after expired_at.
     * @param expired_at Time the WAIT_WPS timeout expired (state_time_stamp_timeout).
     */
    bool dpp_chirp_holds_wait_wps(std::chrono::steady_clock::time_point expired_at);
    /**
     * @brief Stop the chirp the campaign owns (see owned_dpp_chirp_iface()) through the HAL
     * of its bSTA. Call it before that HAL is reset or when the DPP intent ends.
     * Best effort and idempotent: without an owned chirp nothing is sent. No pass is counted;
     * the stopped bSTA counts for the EC 6.2.3 pause and chirps first again once allowed.
     * Events of the stopped pass are ignored because no pass runs.
     * @param reason Logged.
     */
    void stop_owned_dpp_chirp(const char *reason);
#if defined(USE_PRPLMESH_WHM)
    /** bSTA whose supplicant chirp the campaign owns, "" when none. */
    std::string owned_dpp_chirp_iface() const;
    uint64_t m_dpp_attachment_generation = 0;
    struct DppBootstrapCache : bwl::sta_dpp::sBootstrapRegistration {
        std::chrono::steady_clock::time_point retry_after{};
        std::chrono::steady_clock::time_point next_validate{};
    };
    dpp_enrollee::DppEnrolleeKeyStore m_dpp_key_store{
        dpp_enrollee::DppEnrolleeKeyStore::default_path()};
    std::map<std::string, DppBootstrapCache> m_dpp_bootstraps;
    bool m_dpp_key_io_disabled = false;
    std::chrono::steady_clock::time_point m_dpp_next_check{};
    // Set by prepare_dpp_enrollee(): every bSTA is operational and has no enabled profile.
    bool m_dpp_agent_unconfigured = false;

    // Auto chirp: one bounded campaign per Agent process, kept in memory (survives RESTART).
    // WATCH first (D14): a ready CCE target or a due fallback starts one cycle of passes, and
    // every cycle ends back in WATCH (D16, D17).
    enum class eDppChirpState { IDLE, PASS, PAUSE, EXCHANGE, WATCH, DONE };
    struct sDppChirpCampaign {
        eDppChirpState state = eDppChirpState::IDLE;
        std::string iface; // bSTA of the running pass or exchange
        int passes = 0; // completed passes in the current cycle
        // The running cycle (D23: the existing chirp): a fallback on the URI channels (D14,
        // trigger=schedule) or a CCE target on the bSTA target_iface (D13, trigger=cce).
        bool fallback_cycle = false;
        std::string target_iface;
        dpp_enrollee::sWatchCadence cadence;   // D16: fast watch rounds until fast_until
        dpp_enrollee::sFallbackTimer fallback; // D14: armed at the first eligible bSTA
        int chirps = 0; // chirps sent (DPP-TX type=13) in the running pass
        // EC 6.2.3 iteration over the bSTAs (G10): picks the bSTA, enforces the pause.
        dpp_enrollee::sChirpIteration iteration;
        // Candidate bSTAs in chirp order at the last pass start; refused starts on one bSTA.
        std::vector<std::string> iteration_ifaces;
        int start_failures = 0;
        // Answered or stopped bSTA: chirps first once the iteration allows it.
        std::string retry_iface;
        std::chrono::steady_clock::time_point deadline{};
        std::chrono::steady_clock::time_point pass_start{};
        // End of the WAIT_WPS hold in PAUSE: set for the pause between two passes of a cycle,
        // cleared in WATCH.
        std::chrono::steady_clock::time_point hold_until{};
        // The last WATCH round requested a CCE scan on at least one bSTA: WATCH holds the
        // WAIT_WPS restart, which would drop the bSTA and its scan results, until the next round.
        bool watch_scanning = false;
        // DPP-CONF-RECEIVED seen in the running exchange.
        bool conf_received = false;
        // Set when a received configuration never led to Connected: WAIT_WPS restarts the bSTA.
        bool restart_bsta = false;
        // Auth Request seen (DPP-RX type=0), no AUTH-SUCCESS yet. wpa_supplicant reports it
        // before its own key check: the campaign is kept here and restored when the request
        // is dropped as not for this Enrollee (AUTH_REQUEST_IGNORED).
        bool auth_pending              = false;
        eDppChirpState auth_prev_state = eDppChirpState::IDLE;
        std::string auth_prev_iface;
        std::chrono::steady_clock::time_point auth_prev_deadline{};
    } m_dpp_chirp;
    // WAIT_WPS expiry whose deferral was logged; each WAIT_WPS entry arms a new expiry.
    std::chrono::steady_clock::time_point m_dpp_hold_logged_for{};
    /**
     * @brief End the current chirp pass and schedule the next one (or end the cycle).
     * @param end Why the pass ended; selects the gap before the next pass and is logged.
     * @param stop_supplicant_chirp Send DPP stop_chirp to the pass iface first; false when the
     *        supplicant already reported DPP-CHIRP-STOPPED.
     */
    void end_dpp_chirp_pass(dpp_enrollee::eChirpPassEnd end, bool stop_supplicant_chirp);
    /**
     * @brief Apply a DPP Enrollee event of iface to the campaign. Only the bSTA of the running
     * pass or exchange changes it (G08); freq is the DPP-TX frequency in MHz, 0 when absent.
     */
    void handle_dpp_enrollee_event(const std::string &iface, bwl::sta_dpp::eEnrolleeEvent type,
                                   uint32_t freq);
    static const char *dpp_chirp_state_name(eDppChirpState state);
    /** Chirp-safe check of a chirp sent on freq (MHz) by sta_iface; unknown radio is unsafe. */
    bool dpp_chirp_freq_safe(const std::string &sta_iface, uint32_t freq);
    // WATCH round counters for the round log (Review F5); kept across RESTART.
    dpp_enrollee::sWatchRoundStats m_dpp_watch_stats;
    // Chirp-safe channels last logged per sta_iface; the log shows on target which channels
    // (6 GHz included) the AgentDB map lists as chirp-safe.
    std::map<std::string, std::string> m_dpp_chirp_safe_logged;
    // Draws the fallback jitter (D14).
    std::mt19937 m_dpp_jitter_rng{std::random_device{}()};
    /** End the running cycle: back off its CCE target (D17) or step the fallback timer
     *  (D14), boost the watch cadence (D16) and return to WATCH at once. */
    void end_dpp_retry_cycle(const char *reason);
    /** D17: CCE target state per bSTA, updated by each WATCH read. */
    std::map<std::string, dpp_enrollee::sCceTargetState> m_dpp_cce_targets;
#endif
    bool backhaul_fsm_main(bool &skip_select);
    bool backhaul_fsm_wired(bool &skip_select);
    bool backhaul_fsm_wireless(bool &skip_select);
    bool is_front_radio(std::string mac);
    bool handle_backhaul_connect();
    bool handle_backhaul_disconnect();

    /**
     * @brief Creates Backhaul STA Steering Response message with 2 tlvs Steering Response
     *        and Error Code.
     *
     * @param error_code One of the error codes presented in wfa_map::tlvErrorCode::eReasonCode.
     * @param target_bssid The target BSSID to steer to.
     * @return True on success and false otherwise
     */
    bool create_backhaul_steering_response(wfa_map::tlvErrorCode::eReasonCode error_code,
                                           const sMacAddr &target_bssid);

    /**
     * @brief Cancels on-going backhaul steering operation.
     */
    void cancel_backhaul_steering_operation();

    // cmdu handlers
    bool handle_master_message(ieee1905_1::CmduMessageRx &cmdu_rx,
                               std::shared_ptr<beerocks_message::cACTION_HEADER> beerocks_header);
    bool handle_slave_backhaul_message(int fd, ieee1905_1::CmduMessageRx &cmdu_rx);
    bool handle_slave_1905_1_message(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                     const sMacAddr &dst_mac, const sMacAddr &src_mac);
    bool handle_1905_1_message(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                               const sMacAddr &dst_mac, const sMacAddr &src_mac);
    // 1905 messages handlers
    bool handle_slave_failed_connection_message(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                const sMacAddr &src_mac);
    bool handle_backhaul_steering_request(ieee1905_1::CmduMessageRx &cmdu_rx,
                                          const sMacAddr &src_mac, uint32_t iface_index);

    //bool sta_handle_event(const std::string &iface,const std::string& event_name, void* event_obj);
    bool hal_event_handler(bwl::base_wlan_hal::hal_event_ptr_t event_ptr, std::string iface);

    bool is_eth_link_up();
    void get_scan_measurement();
    bool select_bssid();
    void platform_notify_error(bpl::eErrorCode code, const std::string &error_data);
    bool send_slaves_enable();

    /**
     * @brief Initializes the bridge FDB monitor and registers it in the event loop.
     *
     * @return true on success and false otherwise.
     */
    bool init_fdb_monitor();

    /**
     * @brief Stops the bridge FDB monitor and unregisters its event loop handlers.
     */
    void stop_fdb_monitor();

    /**
     * @brief Handles bridge FDB monitor notifications.
     *
     * @return true on success and false otherwise.
     */
    bool handle_fdb_monitor();

    /**
     * @brief Tears down all VAPs in all radios.
     * 
     * @return true on success and false otherwise.
     */
    bool send_slaves_tear_down();

    /**
     * @brief Removes the handlers for the external and internal events 
     *        as well as the events themselves for the given radio.
     */
    void
    clear_radio_handlers(const std::shared_ptr<beerocks::BackhaulManager::sRadioInfo> &radio_info);

    std::shared_ptr<bwl::sta_wlan_hal> get_wireless_hal(const std::string &iface = "");
    std::shared_ptr<bwl::sta_wlan_hal> get_wireless_hal(const sMacAddr &radio_mac);

    /**
     * @brief Converts WSC::sWscAttrAuthenticationType to bwl::WiFiSec
     */
    bwl::WiFiSec wsc_to_bwl_authentication(WSC::eWscAuth authtype);

private:
    /**
     * Buffer to hold CMDU to be transmitted.
     */
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];

    /**
     * CMDU to be transmitted.
     */
    ieee1905_1::CmduMessageTx cmdu_tx;

    /**
     * Buffer to hold CMDU to be transmitted by the UCC listener (in certification mode).
     */
    uint8_t m_cert_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];

    /**
     * CMDU to be transmitted by the UCC listener (in certification mode).
     */
    ieee1905_1::CmduMessageTx cert_cmdu_tx;

    struct SBackhaulConfig {
        std::string ssid;
        std::string pass;
        std::string preferred_bssid;
        beerocks::eIfaceType wire_iface_type;
        beerocks::eIfaceType wireless_iface_type;
        bwl::WiFiSec security_type;
        bool mem_only_psk;
        eFreqType backhaul_preferred_radio_band;
    } m_sConfig;

    int unassociated_rssi_measurement_header_id = -1;

    //comes from config file
    const std::set<std::string> slave_ap_ifaces;
    const std::set<std::string> slave_sta_ifaces;

    //used for several states independently
    std::set<std::string> pending_slave_sta_ifaces;

    const std::string m_beerocks_temp_path;
    const uint16_t m_ucc_listener_port;

public:
    std::vector<std::shared_ptr<sRadioInfo>> m_radios_info;
    int get_agent_fd() { return m_agent_fd; }

private:
    int m_agent_fd = beerocks::net::FileDescriptor::invalid_descriptor;

    /**
     * CMDU client connected to the the CMDU server running in platform manager.
     * This object is dynamically created using the CMDU client factory for the platform manager
     * provided in class constructor.
     */
    std::unique_ptr<CmduClient> m_platform_manager_client;

    /**
     * Timer manager to help using application timers.
     */
    std::shared_ptr<beerocks::TimerManager> m_timer_manager;

    int configuration_stop_on_failure_attempts;

    int stop_on_failure_attempts;

    //backlist bssid and timers (remove con(or wrong passphrase) ap from select bssid for limited time )
    struct ap_blacklist_entry {
        std::chrono::steady_clock::time_point timestamp;
        int attempts = 0;
    };
    std::unordered_map<std::string, ap_blacklist_entry> ap_blacklist;

    struct sBackhaulWireInterface {
        AgentDB::sEthernetPort ethernet_port;
        bool bridge_member    = false;
        bool up_and_running   = false;
        bool has_1905_traffic = false; // later
    };
    std::unordered_map<std::string, sBackhaulWireInterface> m_backhaul_wire_interfaces;

    enum class eLoopIfaceType { Unknown, WiredCandidate, WirelessEndpoint };

    struct sLoopIfaceInfo {
        uint32_t iface_index = 0;
        std::string iface_name;
        eLoopIfaceType iface_type              = eLoopIfaceType::Unknown;
        size_t wired_candidate_order           = 0;
        beerocks::eFreqType wireless_freq_type = beerocks::FREQ_UNKNOWN;
        bool selected_backhaul                 = false;
    };

    void handle_duplicate_cmdu_notification(
        const beerocks::btl::BrokerClient::DuplicateCmduNotification &notification);
    sLoopIfaceInfo classify_loop_iface(uint32_t iface_index) const;
    const char *loop_iface_type_to_string(eLoopIfaceType iface_type) const;
    int wireless_loop_preference(beerocks::eFreqType freq_type) const;
    AgentDB::sEthernetPort wired_candidate_with_mac(const AgentDB::sEthernetPort &candidate) const;
    bool find_wired_candidate(const std::string &iface_name,
                              AgentDB::sEthernetPort &candidate) const;
    bool wired_candidate_is_available(const std::string &iface_name) const;
    bool has_available_wired_candidate() const;
    void maybe_send_wired_controller_probe();
    std::string select_wired_controller_probe_radio_iface() const;
    bool send_wired_controller_probe_on_candidate(const std::string &wired_iface);
    bool send_wired_controller_probe_search(const std::string &radio_iface,
                                            const std::string &wired_iface);
    bool remove_wireless_backhaul_ifaces_from_bridge();
    bool register_wan_monitor_handlers();
    void unregister_wan_monitor_handlers();
    bool handle_wan_monitor_events();
    void handle_wan_monitor_event(const wan_monitor::LinkEvent &event);
    bool handle_wired_controller_detected(uint32_t iface_index);
    bool handle_wired_autoconfiguration_response(uint32_t iface_index,
                                                 ieee1905_1::CmduMessageRx &cmdu_rx);

    wan_monitor wan_mon;
    int m_wan_monitor_fd = beerocks::net::FileDescriptor::invalid_descriptor;
    // Runtime fallback guard. Set after wired controller discovery timeout and kept across the
    // BackhaulManager restart so the next ENABLED pass can try wireless onboarding.
    bool m_skip_wired_backhaul = false;
    // Set only when an established backhaul is confirmed lost. Other RESTART users must not cause
    // the Agent to tear down its fronthaul BSSs.
    bool m_teardown_fronthaul_on_disconnect = false;
    // Set when the Agent sends ACTION_BACKHAUL_ENABLE and then waits, without a timeout, for
    // ACTION_BACKHAUL_CONNECTED_NOTIFICATION. Cleared only after handle_backhaul_connect() sends
    // it. A wireless reconnect checks this flag so that a pending initial connection is completed
    // through CONNECTED instead of resuming OPERATIONAL directly.
    bool m_backhaul_connected_notification_pending = false;
    std::chrono::steady_clock::time_point m_backhaul_connect_retry_at{};
    std::string m_preferred_wired_candidate_iface;
    std::chrono::steady_clock::time_point m_next_wired_controller_probe_time =
        std::chrono::steady_clock::time_point::min();
    size_t m_next_wired_controller_probe_candidate_index = 0;

    // Future to hold the DHCP client process exit code
    std::future<int> m_ftDHCPRetCode;

    fdb_monitor m_fdb_monitor; ///< Monitors bridge FDB updates through NETLINK_ROUTE.
    int m_fdb_monitor_fd =
        beerocks::net::FileDescriptor::invalid_descriptor; ///< Registered FDB monitor fd.
    bool m_topology_task_initialized = false; ///< Topology task is ready to receive FDB events.

    // state switch mechanism
    const int SELECT_TIMEOUT_MSC                      = 500;
    const int DEVICE_QUERY_RESPONSE_TIMEOUT_SECONDS   = 3;
    const int WAIT_FOR_SCAN_RESULTS_TIMEOUT_SECONDS   = 20;
    const int WPA_ATTACH_TIMEOUT_SECONDS              = 5;
    const int CONNECTING_TO_MASTER_TIMEOUT_SECONDS    = 30;
    const int MAX_FAILED_SCAN_ATTEMPTS                = 3;
    const int MAX_FAILED_ROAM_SCAN_ATTEMPTS           = 4;
    const int MAX_FAILED_DHCP_ATTEMPTS                = 2;
    const int MAX_WIRELESS_ASSOCIATE_TIMEOUT_SECONDS  = 10;
    const int MAX_WIRELESS_ASSOCIATE_3ADDR_ATTEMPTS   = 2;
    const int MAX_ETH_FAILED_ATTEMPTS                 = 14;
    const int POLL_TIMER_TIMEOUT_MS                   = 1000;
    const int WIRELESS_WAIT_FOR_RECONNECT_TIMEOUT     = 30;
    const int RSSI_POLL_INTERVAL_MS                   = 1000;
    const int STATE_WAIT_ENABLE_TIMEOUT_SECONDS       = 600;
    const int STATE_WAIT_WPS_TIMEOUT_SECONDS          = 120;
#if defined(USE_PRPLMESH_WHM)
    // DPP auto chirp. No data model switch: enabled whenever the bootstrap is READY.
    const bool DPP_AUTO_CHIRP_ENABLED         = true;
    const int DPP_CHIRP_PAUSE_SECONDS         = 30; // after each band iteration (EC 6.2.3)
    const int DPP_CHIRP_BAND_SWITCH_SECONDS   = 2;  // between the bands of one iteration
    const int DPP_CHIRP_PASSES_PER_CYCLE      = 4;
    const int DPP_CHIRP_PASS_WATCHDOG_SECONDS = 60; // pass start guard (lost DPP-CHIRP-STOPPED)
    const int DPP_CHIRP_SECS_PER_URI_CHANNEL  = 6;  // added to the start guard per URI channel
    const int DPP_CHIRP_TX_PROGRESS_SECONDS   = 15; // guard after each chirp sent (DPP-TX)
    const int DPP_CHIRP_PASS_MAX_SECONDS      = 300;
    const int DPP_CHIRP_AUTH_WAIT_SECONDS     = 10; // DPP-RX type=0 -> AUTH-SUCCESS
    const int DPP_CHIRP_EXCHANGE_WAIT_SECONDS = 60; // AUTH/CONF -> Connected
    const int DPP_CHIRP_RETRY_SECONDS         = 10; // chirp RPC refused
    const int DPP_CHIRP_RECHECK_SECONDS       = 2;  // not eligible yet
    const int DPP_CHIRP_EP_SETTLE_SECONDS     = 20; // HAL attach -> first chirp
    const int DPP_CHIRP_HOLD_GRACE_SECONDS    = 12; // WAIT_WPS hold slack: RETRY + RECHECK
    // WAIT_WPS hold cap after the timeout expired; one cycle holds at most about 1340 s, WATCH
    // holds round after round until this cap.
    const int DPP_CHIRP_HOLD_MAX_SECONDS = 1800;
    // Refused chirp starts before the next bSTA in band order gets the pass.
    const int DPP_CHIRP_START_FAILURES_BEFORE_SKIP = 2;
    // CCE watch first (D13, D14, D16, D17): every cycle is a CCE target or a fallback cycle.
    const int DPP_CHIRP_WATCH_INTERVAL_SECONDS      = 60;  // fast rounds, one scan per radio
    const int DPP_CHIRP_WATCH_FIRST_READ_SECONDS    = 10;  // scan time: first scan read early
    const int DPP_CHIRP_WATCH_SLOW_INTERVAL_SECONDS = 120; // outside the fast window
    const int DPP_CHIRP_WATCH_FAST_WINDOW_SECONDS   = 600; // after READY, a rise or a failed cycle
    const int DPP_CHIRP_CCE_FRESH_SECONDS           = 120; // CCE seen at most this long ago
    const size_t DPP_CHIRP_TARGET_MAX_FREQS         = 8;   // D13, logged (D23: chirp unscoped)
    const int DPP_CHIRP_FALLBACK_FIRST_SECONDS      = 180; // READY to the first fallback
    const int DPP_CHIRP_COOLDOWN_JITTER_SECONDS     = 60;  // 0..60 s added to a fallback step
    // Fallback timer after each failed fallback; the last step repeats.
    const std::vector<int> DPP_CHIRP_RETRY_COOLDOWN_SECONDS = {600, 1200, 1800};
    // D17: backoff of a CCE target after each failed target cycle; the last step repeats.
    const std::vector<int> DPP_CHIRP_TARGET_BACKOFF_SECONDS = {120, 240, 480};
#endif
    const int AP_BLACK_LIST_TIMEOUT_SECONDS           = 120;
    const int AP_BLACK_LIST_FAILED_ATTEMPTS_THRESHOLD = 2;
    const int INTERFACE_BRING_UP_TIMEOUT_SECONDS      = 600;
    const int DEAUTH_REASON_PASSPHRASE_MISMACH        = 2;

    std::chrono::steady_clock::time_point state_time_stamp_timeout;
    int state_attempts;
    bool reassociation = false;

    bool hidden_ssid = false;

    std::string selected_bssid;
    // a selected BSSID channel, and to which band it belongs
    std::pair<int, beerocks::eFreqType> selected_bssid_channel;
    std::string roam_selected_bssid;
    std::pair<int, beerocks::eFreqType> roam_selected_bssid_channel;
    bool roam_flag = false;
    std::unordered_map<std::string, net::sScanResult> scan_measurement_list;

    const int RSSI_THRESHOLD_5GHZ       = -80;
    const int RSSI_BAND_DELTA_THRESHOLD = 5;

    std::chrono::steady_clock::time_point rssi_poll_timer;
    // This snippet is commented out since the only place that use it, is also commented out.
    // An event-driven solution will be implemented as part of the task:
    // [TASK] Dynamic switching between wired and wireless
    // https://github.com/prplfoundation/prplMesh/issues/866
    // std::chrono::steady_clock::time_point eth_link_poll_timer;
    // bool m_eth_link_up  = false;

    std::string bssid_bridge_mac;

    std::unique_ptr<beerocks::agent_ucc_listener> m_agent_ucc_listener;

    TaskPool m_task_pool;
    friend ChannelSelectionTask;

public:
    /**
     * Unsuccessful Association Policy
     */
    struct sUnsuccessfulAssociationPolicy {
        /* The values in this struct are set by the controller through a Multi-AP Policy Config Request message,
         * inside the Unsuccessful Association Policy TLV.
         */

        /**
         * Report or Don't report unsuccessful associations of clients
         */
        bool report_unsuccessful_association = false;

        /**
         * Maximum Reporting Rate of failed associations in attempts per minute
         */
        uint32_t maximum_reporting_rate = 0;

        /**
         * Time point at which failed association was reported for the last time.
         */
        std::chrono::steady_clock::time_point last_reporting_time_point =
            std::chrono::steady_clock::time_point::min(); // way in the past

        /**
         * Number of reports in the last minute
         */
        uint32_t number_of_reports_in_last_minute = 0;
    } unsuccessful_association_policy;

    /**
     * @brief Information gathered about a radio (= slave).
     *
     * Radio information is obtained from messages sent by slave threads and is used to build
     * the TLVs to include in notification messages or responses to CMDU query messages.
     */
    struct sRadioInfo {
        sMacAddr radio_mac;       /**< Radio ID (= radio MAC address) */
        std::string hostap_iface; /**< Name of the radio interface */
        std::string sta_iface;    /**< Name of the bSTA interface on the radio (if any) */
#if defined(USE_PRPLMESH_WHM)
        uint64_t dpp_attachment_generation = 0;
        // No DPP chirp before this time (HAL attach + DPP_CHIRP_EP_SETTLE_SECONDS). The attach
        // writes the EndPoint (Enable, MultiAPProfile); a pWHM commit during the chirp
        // reconfigures wpa_supplicant, which drops the runtime DPP settings. Nov-11 Box C:
        // chirp 5 s after the writes got a fronthaul object, 31 s and 65 s worked.
        std::chrono::steady_clock::time_point dpp_chirp_not_before{};
#endif
        uint8_t primary_channel; /**< Primary Channel of the radio */
        std::shared_ptr<bwl::sta_wlan_hal> sta_wlan_hal;
        std::vector<int> sta_hal_ext_events = {beerocks::net::FileDescriptor::invalid_descriptor};
        int sta_hal_int_events              = beerocks::net::FileDescriptor::invalid_descriptor;
    };

#if defined(USE_PRPLMESH_WHM)
    // A bSTA that may chirp now: HAL operational, bootstrap ID registered in the current HAL
    // attachment and validated, settle time over.
    struct sDppChirpCandidate {
        std::shared_ptr<sRadioInfo> info;
        const DppBootstrapCache *bootstrap = nullptr;
        eFreqType band                     = eFreqType::FREQ_UNKNOWN;
        dpp_enrollee::ChannelDfsMap channels;
    };
    /** Candidates in chirp order: preferred backhaul band first, then 5, 2.4 and 6 GHz. */
    std::vector<sDppChirpCandidate> dpp_chirp_candidates(std::chrono::steady_clock::time_point now);
    /** Read the scan results of a candidate (starts no scan) and return its D13 CCE target. */
    dpp_enrollee::sCceTarget read_dpp_cce_target(const sDppChirpCandidate &candidate,
                                                 std::chrono::steady_clock::time_point now);
    /** One WATCH round: a ready CCE target starts a cycle, else a due fallback, else scan. */
    void run_dpp_watch_round(const std::vector<sDppChirpCandidate> &candidates,
                             std::chrono::steady_clock::time_point now);
#endif
private:
    /**
     * The BSSID of the target BSS in an ongoing backhaul steering operation.
     * Empty MAC address if no steering operation is in progress.
     */
    sMacAddr m_backhaul_steering_bssid = beerocks::net::network_utils::ZERO_MAC;

    /**
     * The channel of the target BSS in an ongoing backhaul steering operation.
     * Zero if no steering operation is in progress.
     */
    std::pair<int, beerocks::eFreqType> m_backhaul_steering_channel = {0, beerocks::FREQ_UNKNOWN};

    /**
     * File descriptor of the timer to check if a backhaul steering request timed out.
     */
    int m_backhaul_steering_timer = beerocks::net::FileDescriptor::invalid_descriptor;

    /*
 * State Machines
 */
private:
// Helper MACROs for Enum/String generation
#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

#define FOREACH_STATE(STATE)                                                                       \
    STATE(INIT)                                                                                    \
    STATE(WAIT_ENABLE)                                                                             \
    STATE(ENABLED)                                                                                 \
                                                                                                   \
    STATE(_WIRELESS_START_)                                                                        \
    STATE(INIT_HAL)                                                                                \
    STATE(WPA_ATTACH)                                                                              \
    STATE(INITIATE_SCAN)                                                                           \
    STATE(WAIT_WPS)                                                                                \
    STATE(WAIT_FOR_SCAN_RESULTS)                                                                   \
    STATE(WIRELESS_CONFIG_4ADDR_MODE)                                                              \
    STATE(WIRELESS_ASSOCIATE_4ADDR)                                                                \
    STATE(WIRELESS_ASSOCIATE_4ADDR_WAIT)                                                           \
    STATE(WIRELESS_WAIT_FOR_RECONNECT)                                                             \
    STATE(_WIRELESS_END_)                                                                          \
                                                                                                   \
    STATE(CONNECTED)                                                                               \
    STATE(OPERATIONAL)                                                                             \
    STATE(RESTART)                                                                                 \
    STATE(STOPPED)

    // States ENUM
    enum class EState { FOREACH_STATE(GENERATE_ENUM) };

    // States Strings Array
    static const char *s_arrStates[];

    EState m_eFSMState;

    /**
     * Factory to create broker client instances connected to broker server.
     * Broker client instances are used to exchange CMDU messages with remote processes running in
     * other devices in the network via the broker server running in the transport process.
     */
    std::unique_ptr<beerocks::btl::BrokerClientFactory> m_broker_client_factory;

    /**
     * Factory to create CMDU client instances connected to CMDU server running in platform manager.
     */
    std::unique_ptr<beerocks::CmduClientFactory> m_platform_manager_cmdu_client_factory;

    /**
     * UCC server to exchange commands and replies with UCC certification application.
     */
    std::unique_ptr<beerocks::UccServer> m_ucc_server;

    /**
     * CMDU server address used by CmduServer `m_cmdu_server`.
     */
    std::shared_ptr<beerocks::net::UdsAddress> m_cmdu_server_uds_address;

    /**
     * CMDU server to exchange CMDU messages with clients through socket connections.
     */
    std::unique_ptr<beerocks::CmduServer> m_cmdu_server;

    /**
     * File descriptor of the timer to run internal tasks periodically.
     */
    int m_tasks_timer = beerocks::net::FileDescriptor::invalid_descriptor;

    // Never hold this mutex while executing WPS, accessing AgentDB or entering AMX.
    std::mutex m_wps_request_mutex;
    int m_wps_request_fd       = beerocks::net::FileDescriptor::invalid_descriptor;
    bool m_accept_wps_requests = false;
    // Fails start() if the WPS request eventfd could not be created.
    bool init() override;
    void before_stop() override;

    /**
     * File descriptor of the timer to run the Finite State Machine.
     */
    int m_fsm_timer = beerocks::net::FileDescriptor::invalid_descriptor;

    /**
     * Broker client to exchange CMDU messages with broker server running in transport process.
     */
    std::shared_ptr<beerocks::btl::BrokerClient> m_broker_client;

private:
    /**
     * @brief Callback handler function for "dev_reset_default" WFA-CA command.
     *
     * @param[in] fd File descriptor of the socket connection the command was received through.
     * @param[in] params Command parameters.
     */
    void handle_dev_reset_default(int fd,
                                  const std::unordered_map<std::string, std::string> &params);

    /**
     * @brief Callback handler function for "dev_set_config" WFA-CA command.
     *
     * @param[in] params Command parameters.
     * @param[out] err_string Contains an error description if the function fails.
     * 
     * @return true on success and false otherwise.
     */
    bool handle_dev_set_config(const std::unordered_map<std::string, std::string> &params,
                               std::string &err_string);

    /**
     * @brief Apply pending certification QoS management settings to fronthaul VAPs.
     *
     * @return true on success, false otherwise.
     */
    bool apply_qos_management_settings();

    /** 
     * Flag meaning that agent is in the reset state. 
     * 
     * Agent is held in the reset state from the moment "dev_reset_default" command is received and 
     * until the "dev_set_config" command is received and processed or a timeout expires. 
     */
    bool m_is_in_reset_state = false;

    /** 
     * Flag meaning that reset is completed.
     *
     * The "dev_reset_default" WFA-CA command should only return when reset is completed, i.e. when
     * the device is ready to accept commands. In particular, when later on a "dev_set_config" is
     * received, that command should be able to configure the backhaul right away. Therefore, the
     * "dev_reset_default" command should only return when the backhaul is connected to the slaves
     * again. This function allows the backhaul to signal that it has reached that state.
     */
    bool m_dev_reset_default_completed = false;

    /**
     * File descriptor of the timer to check if a "dev_reset_default" command handling timed out.
     */
    int m_dev_reset_default_timer = beerocks::net::FileDescriptor::invalid_descriptor;

    /**
     * File descriptor of the socket that the "dev_reset_default" command was received through.
     * 
     * The socket descriptor is used to send the reply to UCC client when command processing is 
     * completed. 
     */
    int m_dev_reset_default_fd = beerocks::net::FileDescriptor::invalid_descriptor;

    /**
     * Selected backhaul received on "dev_set_config" WFA-CA command from UCC client.
     * Possible values are "eth" for wired backhaul, the RUID of selected radio or empty string if 
     * "dev_set_config" command has not been received yet.
     */
    std::string m_selected_backhaul;

    /**
     * @brief QoS management settings received from WFA-CA dev_set_config.
     */
    struct sQosManagementSettings {
        bool valid       = false;
        bool mscs_enable = false;
        bool scs_enable  = false;
    } m_qos_management_settings;

    bool m_remove_platform_manager_client = false;
};

} // namespace beerocks

#endif // _BACKHAUL_MANAGER_H
