/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _SERVICE_PRIORITIZATION_TASK_H_
#define _SERVICE_PRIORITIZATION_TASK_H_

#include "task.h"

#include <bpl/bpl.h>
#include <bpl/bpl_service_prio_utils.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/wfa_map/tlvTidToLinkMappingPolicy.h>

#include <chrono>
#include <string>
#include <unordered_map>

namespace beerocks {

// Forward declaration for BackhaulManager context saving
class slave_thread;

class ServicePrioritizationTask : public Task {
public:
    ServicePrioritizationTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);

    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;
    void work() override;
    void handle_event(uint8_t event_enum_value, const void *event_obj) override;
    bool clear_configuration();

    enum eEvent : uint8_t {
        QOS_NEW_WDS_IFACE   = 0,
        QOS_CLEAR_WDS_IFACE = 1,
    };

private:
    struct sPendingWdsIfaceState {
        std::chrono::steady_clock::time_point not_before;
        std::chrono::steady_clock::time_point deadline;
    };

    void handle_service_prioritization_request(ieee1905_1::CmduMessageRx &cmdu_rx,
                                               const sMacAddr &src_mac);

    /**
     * @brief Forward QoS management descriptor TLVs to the owning AP manager.
     *
     * @param cmdu_rx Service Prioritization Request CMDU.
     * @return true on success, false otherwise.
     */
    bool handle_qos_management_descriptors(ieee1905_1::CmduMessageRx &cmdu_rx);
    void handle_slave_channel_selection_response(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                 const sMacAddr &src_mac);

    // helper funtion
    inline uint8_t get_tid_byte(const wfa_map::cTidToLinkMapping::sTidToLinkMapping_byte &map)
    {
        return *reinterpret_cast<const uint8_t *>(&map);
    }

    /**
    * @brief parse tidtolinkmappingpolicy tlv and add into db
    *
    * @return true if parsed tlv and added data in db, otherwise false
    * */
    bool handle_tid_to_link_mapping_policy_tlv(
        std::shared_ptr<wfa_map::tlvTidToLinkMappingPolicy> tlvTidToLinkMapping);

    /**
    * @brief Sends notification to HostAP/Driver about the current service prioritization config
    *
    * @return true if config applied or handled properly, otherwise false.
    * */
    bool send_service_prio_config(const beerocks_message::sServicePrioConfig &request);

    void gather_iface_details(std::list<bpl::ServicePrioritizationUtils::sInterfaceTagInfo> *);

    void run_at(std::chrono::steady_clock::time_point due);
    void request_wds_retry(const std::string &iface_name,
                           std::chrono::steady_clock::time_point not_before,
                           std::chrono::steady_clock::time_point deadline);
    void clear_scheduled_work();
    bool should_run_now() const;
    bool handle_new_wds_iface(const std::string &iface_name);
    bool handle_clear_wds_iface(const std::string &iface_name);
    bool retry_pending_wds_ifaces();

    bool qos_apply_active_rule();
    bool qos_flush_setup();
    bool qos_setup_single_value_map(uint8_t pcp);
    bool qos_setup_dscp_map();
    bool qos_setup_up_map();

    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;

    std::shared_ptr<beerocks::bpl::ServicePrioritizationUtils> service_prio_utils;

    bool m_pending = false;
    std::chrono::steady_clock::time_point m_next_run{std::chrono::steady_clock::time_point::min()};
    std::unordered_map<std::string, sPendingWdsIfaceState> m_pending_wds_ifaces;

    enum : uint8_t { QOS_USE_DSCP_MAP = 0x08, QOS_USE_UP = 0x09 };

    static constexpr int DEBOUNCE_MS          = 200;
    static constexpr int WDS_SETTLE_MS        = 500;
    static constexpr int WDS_RETRY_TIMEOUT_MS = 2000;
};

} // namespace beerocks

#endif // _SERVICE_PRIORITIZATION_TASK_H_
