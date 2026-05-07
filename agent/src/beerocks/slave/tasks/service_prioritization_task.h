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

namespace beerocks {

// Forward declaration for BackhaulManager context saving
class slave_thread;

class ServicePrioritizationTask : public Task {
public:
    ServicePrioritizationTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);

    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;
    bool clear_configuration() { return qos_flush_setup(); };

private:
    void handle_service_prioritization_request(ieee1905_1::CmduMessageRx &cmdu_rx,
                                               const sMacAddr &src_mac);
    void handle_slave_channel_selection_response(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                 const sMacAddr &src_mac);
    //helper Funtion
    inline uint8_t get_tid_byte(const wfa_map::cTidToLinkMapping::sTidToLinkMapping_byte &map)
    {
        return (map.bit0 << 0) | (map.bit1 << 1) | (map.bit2 << 2) | (map.bit3 << 3) |
               (map.bit4 << 4) | (map.bit5 << 5) | (map.bit6 << 6) | (map.bit7 << 7);
    }
    /**
    * @brief Parse TidToLinkMappingPolicy TLV and add into DB
    * @Return true if parsed TLV and added data in DB, otherwise false
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

    bool qos_apply_active_rule();
    bool qos_flush_setup();
    bool qos_setup_single_value_map(uint8_t pcp);
    bool qos_setup_dscp_map();
    bool qos_setup_up_map();

    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;

    std::shared_ptr<beerocks::bpl::ServicePrioritizationUtils> service_prio_utils;

    enum : uint8_t { QOS_USE_DSCP_MAP = 0x08, QOS_USE_UP = 0x09 };
};

} // namespace beerocks

#endif // _SERVICE_PRIORITIZATION_TASK_H_
