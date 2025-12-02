/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _AP_AUTOCONFIGURATION_TASK_H_
#define _AP_AUTOCONFIGURATION_TASK_H_

#include "task.h"

#include <mapf/common/encryption.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/WSC/EncryptedSettingsPayload.h>
#include <tlvf/WSC/m2.h>
#include <tlvf/WSC/m8.h>
#include <tlvf/wfa_map/tlvProfile2ErrorCode.h>

#include <bcl/son/son_wireless_utils.h>

namespace beerocks {

// Forward declaration for Agent context saving
class slave_thread;

class ApAutoConfigurationTask : public Task {
public:
    ApAutoConfigurationTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);
    ~ApAutoConfigurationTask() {}

    void work() override;

    enum eEvent : uint8_t {
        INIT_TASK,
        START_AP_AUTOCONFIGURATION,
        APPLY_CONFIG_FOR_NEW_IFACE,
    };

    void handle_event(uint8_t event_enum_value, const void *event_obj) override;

    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;

private:
    /* Class members */

    /**
     * @brief AP-Autoconfiguration Task states.
     * 
     * The AP-Autoconfiguration task states represent the AP-Autoconfiguration phases described on
     * IEEE 1905.1-2013 standard, section 10.1.
     * The 'CONTROLLER_DISCOVERY' state refers to the 'Registrar Discovery' phase.
     * The 'AP_CONFIGURATION' state refers to the 'Parameter configuration' phase.
     */
    enum class eState : uint8_t {
        UNCONFIGURED,
        CONTROLLER_DISCOVERY,
        WAIT_FOR_CONTROLLER_DISCOVERY_COMPLETE,
        SEND_AP_AUTOCONFIGURATION_WSC_M1,
        WAIT_AP_AUTOCONFIGURATION_WSC_M2,
        WAIT_AP_CONFIGURATION_COMPLETE,
        CONFIGURED,
        SKIPPED
    };

    struct sConfigurationParams {
        eState state = eState::UNCONFIGURED;
        std::chrono::steady_clock::time_point timeout;
        std::unique_ptr<mapf::encryption::diffie_hellman> dh = nullptr;
        //copy of M1 message used for authentication
        uint8_t *m1_auth_buf         = nullptr;
        size_t m1_auth_buf_len       = 0;
        uint8_t num_of_bss_available = 0;
        std::unordered_set<sMacAddr> enabled_bssids;
        bool sent_vaps_list_update;
        bool received_vaps_list_update;
    };

    /**
     * @brief Per-BSS configuration received via WSC/1905 TLVs.
     *
     * Holds all data needed to (re)configure a single AP BSS on the Agent side.
     * It aggregates attributes parsed from:
     *   - WSC M2 TLV and it's "Encrypted Settings" (e.g. SSID, keying)
     *   - Agent AP MLD Configuration TLV 
     *   - RSN Parameters Configuration TLV 
     **/
    struct sBssConfig {
        WSC::m2::config m2_config;
        WSC::EncryptedSettingsPayload::config payload_config;
        int8_t mld_id = DISABLED_MLDUNIT;
        son::wireless_utils::eAdditionalAuth additional_auth =
            son::wireless_utils::eAdditionalAuth::NONE;
    };

    /**
     * @brief Backhaul STA (bSTA) configuration and targeting from WSC M8.
     **/
    struct sBStaConfig {
        WSC::m8::config m8_config;
        WSC::EncryptedSettingsPayload::config payload_config;
    };

    /**
     * @brief State of AP-Autoconfiguration task on mapped by front radio interface name.
     * 
     * Key:     Front radio interface name.
     * Value:   AP-Autoconfiguration task state struct of the mapped Front radio interface name.
     * 
     * According to IEEE 1905.1-2013, the AP-Autoconfiguration routine shall occur separately
     * for every 1905.1 device supported band. Multi-AP standard extend the definition and add that
     * the 'AP_CONFIGURATION' phase shall occur for each radio of the Multi-AP device (instead of
     * a band).
     */
    std::unordered_map<std::string, sConfigurationParams> m_radios_conf_params;

    /**
     * @brief Convert enum of task state to string.
     * 
     * @param status Enum of task state. 
     * @return state as string.
     */
    static const std::string fsm_state_to_string(eState status);

    /**
     * @brief A map that contains the discovery status on each of the Agent supported bands.
     * 
     * Since the discovery states are attached to each bands instead of each radio, need to hold,
     * helper container to manage it.
     * 
     * Key: Band type.
     * Value: Struct that contain flags on the discovery phase of mapped band.
     */
    struct sDiscoveryStatus {
        bool completed = false;
        bool msg_sent  = false;
        bool skipped   = false;
    };
    // Decalaring unordered_map with key which is an enum, does not compiles on older gcc version.
    // It was considered a defect in the standard, and was fixed in C++14, and also fixed in the
    // version of libstdc++ shipping with gcc as of 6.1.
    // To make unordered_map work with an enum as key, std::hash<int> function was added as third
    // template argument.
    std::unordered_map<eFreqType, sDiscoveryStatus, std::hash<int>> m_discovery_status;

    bool m_task_is_active = false;

    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;

    // <RUID, <mld_unit, mld_mode>>
    std::unordered_map<sMacAddr, std::pair<int8_t, uint8_t>> bsta_mld_requests_infos;
    // <iface, <ssid, <mld_unit, mld_mode>>>
    std::unordered_map<std::string, std::unordered_map<std::string, std::tuple<int8_t, uint8_t>>>
        m_ap_mld_requests_infos;

    /* Message handlers: */

    /**
    * @brief Handles 1905 AP Autoconfiguration message.
    * 
    * @param[in] cmdu_rx Received CMDU.
    * @param[in] src_mac MAC address of the message sender.
    */
    void handle_ap_autoconfiguration_response(ieee1905_1::CmduMessageRx &cmdu_rx,
                                              const sMacAddr &src_mac);

    /**
     * @brief Parse AP-Autoconfiguration and apply configuration if M2/M8 is present.
     *
     * @param cmdu_rx Received CMDU.
     * @return true on success, otherwise false.
     */
    void handle_ap_autoconfiguration_wsc(ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Parse AP-Autoconfiguration Renew message.
     *
     * This function checks the TLVs in the AP-Autoconfiguration Renew message. If OK, it triggers
     * autoconfiguration.
     *
     * @param cmdu_rx received CMDU containing AP-Autoconfiguration Renew.
     */
    void handle_ap_autoconfiguration_wsc_renew(ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Parse AP MLD Configuration Request message.
     *
     * This function implements the handling of Agent AP MLD Configuration TLV. If OK, it ACK.
     *
     * @param cmdu_rx received CMDU containing AP MLD Configuration Request message.
     */
    bool handle_ap_mld_configuration_request(ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Parse BSTA MLD Configuration Request message.
     *
     * This function implements the handling of BSTA MLD Configuration TLV. If OK, it ACK.
     *
     * @param cmdu_rx received CMDU containing BSTA MLD Configuration Request message.
     */
    void handle_bsta_mld_configuration_request(ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Parse Multi-AP Policy Configuration message.
     * 
     * The function parse the message, set it on the database and apply it on the Agent platform.
     * 
     * @param cmdu_rx received CMDU containing AP-Autoconfiguration Renew.
     */
    void handle_multi_ap_policy_config_request(ieee1905_1::CmduMessageRx &cmdu_rx);

    /* TLV handlers: */
    bool handle_profile2_default_802dotq_settings_tlv(ieee1905_1::CmduMessageRx &cmdu_rx);
    bool handle_profile2_traffic_separation_policy_tlv(
        ieee1905_1::CmduMessageRx &cmdu_rx, std::unordered_set<std::string> &misconfigured_ssids);
    bool handle_wsc_m2_tlv(ieee1905_1::CmduMessageRx &cmdu_rx, const std::string &radio_iface,
                           const std::vector<WSC::m2> &m2_list, std::vector<sBssConfig> &infos,
                           std::unordered_set<std::string> &misconfigured_ssids);
    bool handle_wsc_m8_tlv(const std::string &radio_iface, std::shared_ptr<WSC::m8> m8,
                           sBStaConfig &info);
    bool send_ap_mld_configuration(const std::string &radio_iface, std::string ssid,
                                   int8_t mld_unit, uint8_t mld_mode);
    /**
     * @brief Handle Agent AP MLD Configuration TLV from the controller.
     *
     * Parses the Agent AP MLD Configuration TLV and populates the internal MLD configuration
     * structures.
     *
     * @param[in] cmdu_rx Received CMDU
     * @param[in] radio_iface Radio interface name. If empty, processes all radios(AP MLD Configuration Request Message); otherwise,
     *                        processes only the specified radio interface(AutoConfiguration WSC M2 Message)
     *
     * @return true on success, false if TLV parsing fails or configuration is invalid.
     *
     */
    bool handle_agent_ap_mld_configuration_tlv(ieee1905_1::CmduMessageRx &cmdu_rx,
                                               const std::string &radio_iface);
    bool send_bsta_mld_configuration(const sMacAddr &ruid, int8_t mld_unit, uint8_t mld_mode,
                                     bool initilaization = false);
    int8_t find_available_bsta_mld_unit();
    bool handle_bsta_mld_configuration_tlv(ieee1905_1::CmduMessageRx &cmdu_rx,
                                           const sMacAddr &ruid);
    bool handle_ap_autoconfiguration_wsc_vs_extension_tlv(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                          const std::string &radio_iface);

    bool handle_rsn_parameters_configuration_tlv(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                 std::vector<sBssConfig> &infos,
                                                 const sMacAddr &ruid,
                                                 beerocks::eFreqType freq_type);

    /**
     * @brief Populate MLD ID in BSS infos based on SSID match.
     *
     * @param[in] radio_iface Radio interface name
     * @param[in,out] bss_infos Vector of BSS configurations
     *
     * @return true on success, false if radio_iface is not found
     */
    bool populate_mld_id_in_bss_infos(const std::string &radio_iface,
                                      std::vector<sBssConfig> &bss_infos);
    /**
     * @brief Handles Vendor Specific messages.
     *
     * @param[in] cmdu_rx Received CMDU.
     * @param[in] src_mac MAC address of the message sender.
     * @param[in] fd File descriptor of the socket connection with the slave that sent the message.
     * @param[in] beerocks_header Shared pointer to beerocks header.
     * @return true, if the message has been handled, otherwise false.
     */
    bool handle_vendor_specific(ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &src_mac, int fd,
                                std::shared_ptr<beerocks_header> beerocks_header);

    /* Vendor specific message handlers: */

    void
    handle_vs_wifi_credentials_update_response(ieee1905_1::CmduMessageRx &cmdu_rx, int fd,
                                               std::shared_ptr<beerocks_header> beerocks_header);

    void handle_vs_ap_enabled_notification(ieee1905_1::CmduMessageRx &cmdu_rx, int fd,
                                           std::shared_ptr<beerocks_header> beerocks_header);

    void handle_vs_vaps_list_update_notification(ieee1905_1::CmduMessageRx &cmdu_rx, int fd,
                                                 std::shared_ptr<beerocks_header> beerocks_header);

    void handle_vs_apply_vlan_policy_request(ieee1905_1::CmduMessageRx &cmdu_rx, int fd,
                                             std::shared_ptr<beerocks_header> beerocks_header);

    /* Helper functions */
    /**
     * @brief Wait until given radio @a radio_iface has completed the AP configuration.
     * Perform the necessary actions to actively get the the completed status.
     * 
     * @param radio_iface Radio interface name.
     * @return true if configuration has completed, otherwise false.
     */
    void configuration_complete_wait_action(const std::string &radio_iface);

    bool send_ap_mld_configuration_response_message();

    bool send_ap_autoconfiguration_search_message(const std::string &radio_iface);

    bool send_ap_autoconfiguration_wsc_m1_message(const std::string &radio_iface);

    bool send_ap_bss_configuration_message(const std::string &radio_iface,
                                           const std::vector<sBssConfig> &infos);

    bool send_bsta_configuration(const sMacAddr &radio_mac, const sBStaConfig &info);

    bool send_enable_disable_endpoint(const sMacAddr &radio_mac, const bool enable);

    bool validate_reconfiguration(const std::string &radio_iface, std::vector<sBssConfig> &infos);

    bool send_ap_bss_info_update_request(const std::string &radio_iface);

    bool send_ap_connected_sta_notifications_request(const std::string &radio_iface);

    bool send_platform_version_notification(const std::string &radio_iface,
                                            const std::string &controller_version);

    bool send_monitor_son_config(const std::string &radio_iface,
                                 const beerocks_message::sSonConfig &son_config);

    bool send_error_response_message(
        const std::vector<std::pair<wfa_map::tlvProfile2ErrorCode::eReasonCode, sMacAddr>>
            &bss_errors);

    /**
     * @brief Diffie-Hellman public key exchange keys calculation class member params authkey and
     * keywrapauth are computed on success.
     *
     * @param[in] remote_pubkey Public key received from the controller in WSC tlv.
     * @param[in] nonce Nonce received from the controller in WSC tlv.
     * @param[out] authkey 32 bytes calculated authentication key.
     * @param[out] keywrapkey 16 bytes calculated key wrap key.
     * @return true on success, otherwise false.
     */
    bool ap_autoconfiguration_wsc_calculate_keys(const std::string &fronthaul_iface,
                                                 const uint8_t *remote_pubkey, const uint8_t *nonce,
                                                 uint8_t authkey[32], uint8_t keywrapkey[16]);

    /**
     * @brief ap autoconfiguration global authenticator attribute calculation.
     *
     * Calculate authentication on the Full (M1 || M2*) or (M1 || M8*) whereas M2*|M8* = M2*|M8* without the authenticator
     * attribute. M1 is a saved buffer of the swapped M1 sent in the WSC autoconfiguration sent by
     * the agent.
     *
     * @param [in] wsc WSC M2 or M8 attribute list from the Controller.
     * @param [in] authenticator M2 or M8 authenticator attribute from the Controller.
     * @param [out] authkey Authentication key.
     * @return true on success, otherwise false.
     */
    bool
    ap_autoconfiguration_wsc_authenticate(const std::string &fronthaul_iface, WSC::WscAttrList &wsc,
                                          uint8_t authkey[32],
                                          uint8_t authenticator[WSC::WSC_AUTHENTICATOR_LENGTH]);

    /**
     * @brief Parse the encrypted settings from m2 or m8, and load the into the BSS configuration
     * @a config.
     *
     * @param [in] encrypted_settings WSC M2/M8 Encrypted settings attribute from the Controller.
     * @param [in] authkey Authentication key.
     * @param [in] keywrapkey Key wrapper.
     * @param [out] config BSS configuration.
     * @return true on success, otherwise false.
     */
    bool ap_autoconfiguration_wsc_parse_encrypted_settings(
        WSC::cWscAttrEncryptedSettings encrypted_settings, uint8_t authkey[32],
        uint8_t keywrapkey[16], WSC::EncryptedSettingsPayload::config &payload_config);

    /**
     * @brief Parse the vendor extension from m2 for Service Status
     *
     * This Service Status TLV will be used to change radio state.
     *
     * @param[in] cmdu_rx Received CMDU.
     * @param radio_iface Radio interface name.
     * @return true on success, otherwise false.
     */
    bool
    airties_vs_ap_autoconfiguration_wsc_parse_service_status(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                             const std::string &radio_iface);
    /**
     * @brief Parse the vendor extension from m2 for Radio Operational Mode
     *
     * @param [in] radio_iface Radio interface name.
     * @param [in] m2 WSC M2 TLV from the Controller.
     * @return true on success, otherwise false.
     */
    bool airties_vs_ap_autoconfiguration_wsc_parse_radio_operational_mode_config(
        const std::string &radio_iface, const WSC::m2 &m2);

    bool add_wsc_m1_tlv(const std::string &radio_iface);

    /**
     * @brief Find an available MLD unit ID for AP MLD.
     *
     * Searches for an unused MLD unit by checking which units are already assigned
     * to AP MLD configurations.
     *
     * @return Available MLD unit ID (0 to max_mlds-1), or DISABLED_MLDUNIT if none available.
     */
    int8_t find_available_ap_mld_unit();
};

} // namespace beerocks

#endif // _AP_AUTOCONFIGURATION_TASK_H_
