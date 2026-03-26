/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _TOPOLOGY_TASK_H_
#define _TOPOLOGY_TASK_H_

#include "task.h"

#include <beerocks/tlvf/beerocks_message_1905_vs.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/ieee_1905_1/eMediaType.h>

#include <unordered_map>
#include <unordered_set>

namespace beerocks {

// Forward declaration for BackhaulManager context saving
class BackhaulManager;

class TopologyTask : public Task {
public:
    TopologyTask(BackhaulManager &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);
    ~TopologyTask() {}

    void work() override;

    enum eEvent : uint8_t {
        AGENT_DEVICE_INITIALIZED,
        FDB_CHANGED,
    };

    void handle_event(uint8_t event_enum_value, const void *event_obj) override;

    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;

private:
    /**
     * @brief Local interface representation shared by Device Information TLV and
     * non-1905 neighbors snapshot generation.
     */
    struct sAdvertisedLocalInterface {
        std::string ifname;
        ieee1905_1::eMediaType media_type = ieee1905_1::eMediaType::UNKNOWN_MEDIA;
        bool is_wlan                      = false;
        bool is_backhaul                  = false;
        uint8_t ap_chan_bw                = 0;
        uint8_t ap_chan_index1            = 0;
        uint8_t ap_chan_index2            = 0;
    };

    using sAdvertisedLocalInterfaces = std::unordered_map<sMacAddr, sAdvertisedLocalInterface>;
    using sNon1905NeighborsSnapshot  = std::unordered_map<sMacAddr, std::unordered_set<sMacAddr>>;

    /* 1905.1 message handlers: */

    /**
    * @brief Handles 1905 Topology Discovery message.
    * 
    * @param[in] cmdu_rx Received CMDU.
    * @param iface_index Index of the network interface that the CMDU message was received on.
    * @param dst_mac Destination MAC address.
    * @param[in] src_mac MAC address of the message sender.
    */
    void handle_topology_discovery(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                   const sMacAddr &dst_mac, const sMacAddr &src_mac);

    /**
    * @brief Handles 1905 Topology Query message.
    * 
    * @param[in] cmdu_rx Received CMDU.
    * @param[in] src_mac MAC address of the message sender.
    */
    void handle_topology_query(ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &src_mac);

    /**
     * @brief Handles Vendor Specific messages. 
     * 
     * @param[in] cmdu_rx Received CMDU.
     * @param[in] src_mac MAC address of the message sender.
     * @param[in] beerocks_header Shared pointer to beerocks header.
     * @return true, if the message has been handled, otherwise false.
     */
    bool handle_vendor_specific(ieee1905_1::CmduMessageRx &cmdu_rx, const sMacAddr &src_mac,
                                std::shared_ptr<beerocks_header> beerocks_header);

    /* Vendor specific message handlers: */

    /**
     * @brief Handles Vendor Specific Client Associated message. 
     * 
     * @param[in] cmdu_rx Received CMDU.
     * @param[in] beerocks_header Shared pointer to beerocks header.
     */
    void handle_vs_client_associated(ieee1905_1::CmduMessageRx &cmdu_rx,
                                     std::shared_ptr<beerocks_header> beerocks_header);

    /**
     * @brief Handles Vendor Specific Client Disassociated message. 
     * 
     * @param[in] cmdu_rx Received CMDU.
     * @param[in] beerocks_header Shared pointer to beerocks header.
     */
    void handle_vs_client_disassociated(ieee1905_1::CmduMessageRx &cmdu_rx,
                                        std::shared_ptr<beerocks_header> beerocks_header);

    /**
     * @brief Handles Vendor Specific Topology Notification message.
     *
     * @param[in] cmdu_rx Received CMDU.
     * @param[in] beerocks_header Shared pointer to beerocks header.
     */
    void handle_vs_topology_notification_command(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                 std::shared_ptr<beerocks_header> beerocks_header);

    /* Helper functions */
    void send_topology_discovery();
    void send_topology_notification();

    /**
     * @brief Recompute the normalized non-1905 neighbors snapshot and update the cached copy.
     *
     * @return true if the normalized snapshot has changed, otherwise false.
     */
    bool update_non_1905_neighbors_snapshot();

    /**
     * @brief Collect all local interfaces that are advertised in Device Information TLV.
     *
     * @return Map keyed by local interface MAC.
     */
    sAdvertisedLocalInterfaces collect_advertised_local_interfaces() const;

    /**
     * @brief Build a normalized non-1905 neighbors snapshot for advertised interfaces only.
     *
     * @param advertised_ifaces Source of truth for locally advertised interfaces.
     * @return Snapshot keyed by local interface MAC.
     */
    sNon1905NeighborsSnapshot
    collect_non_1905_neighbors_snapshot(const sAdvertisedLocalInterfaces &advertised_ifaces) const;

    /**
     * @brief Add and fill device information and bridging capability tlvs.
     *
     * @param advertised_ifaces Source of truth for locally advertised interfaces.
     * @return true on success, otherwise false.
     */
    bool add_device_information_and_bridging_capability_tlv(
        const sAdvertisedLocalInterfaces &advertised_ifaces);

    /**
     * @brief Add and fill non-1905 neighbor device TLVs for all known non-1905 neighbors.
     *
     * @param snapshot Normalized non-1905 neighbors snapshot keyed by local interface MAC.
     * @return true on success, otherwise false.
     */
    bool add_non_1905_neighbor_device_tlv(const sNon1905NeighborsSnapshot &snapshot);

    /**
     * @brief Add and fill 1905 neighbor device TLVs for all known 1905 neighbors.
     * 
     * @return true on success, otherwise false.
     */
    bool add_1905_neighbor_device_tlv();

    /**
     * @brief Add and fill supported service tlv.
     * 
     * @return true on success, otherwise false.
     */
    bool add_supported_service_tlv();

    /**
     * @brief Add and fill AP operational BSS tlv.
     * 
     * @return true on success, otherwise false.
     */
    bool add_ap_operational_bss_tlv();

    /**
     * @brief Add and fill associated_clients tlv.
     *
     * @return true on success, otherwise false.
     */
    bool add_associated_clients_tlv();

    /**
     * @brief Add and fill bssid_iface_mapping tlv.
     *
     * @return true on success, otherwise false.
     */
    bool add_vs_tlv_bssid_iface_mapping();

    /**
     * @brief Add and fill assoc_sta_mld_configuration reports.
     *
     * @return true on success, otherwise false.
     */
    bool add_assoc_sta_mld_config_reports();

    /**
     * @brief Add and fill BSS configuration report tlv.
     *
     * @return true on success, otherwise false.
     */
    bool add_bss_configuration_report_tlv();

    std::chrono::steady_clock::time_point m_periodic_discovery_timestamp;

    bool m_pending_to_send_topology_notification = false;
    std::chrono::steady_clock::time_point m_topology_notification_timeout =
        std::chrono::steady_clock::now();
    /**
     * @brief Debounce deadline for event-driven non-1905 neighbors snapshot recomputation.
     */
    std::chrono::steady_clock::time_point m_non_1905_neighbors_update_deadline =
        m_non_1905_neighbors_update_deadline.max();
    /**
     * @brief Last normalized non-1905 neighbors snapshot used for diff-based notifications.
     */
    sNon1905NeighborsSnapshot m_last_non_1905_neighbors_snapshot;

    BackhaulManager &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
};

} // namespace beerocks

#endif // _TOPOLOGY_TASK_H_
