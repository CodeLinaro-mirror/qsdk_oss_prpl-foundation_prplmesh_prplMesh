/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __TLVF_UTILS_H__
#define __TLVF_UTILS_H__

#include "agent_db.h"

#include <tlvf/CmduMessageTx.h>

#include <beerocks/tlvf/beerocks_message.h>

namespace beerocks {

class tlvf_utils {
public:
    /**
     * @brief Adds a new AP Radio Basic Capabilities TLV to given message.
     *
     * @param[in,out] cmdu_tx CDMU message.
     * @param[in] ruid Radio unique identifier of the radio for which capabilities are reported.
     *
     * @return True on success and false otherwise.
     */
    static bool add_ap_radio_basic_capabilities(ieee1905_1::CmduMessageTx &cmdu_tx,
                                                const sMacAddr &ruid);
    static bool create_operating_channel_report(ieee1905_1::CmduMessageTx &cmdu_tx,
                                                const sMacAddr &radio_mac);

    /**
     * @brief Fill the Channel Scan Capabilities TLV
     * @param[out] cmdu_tx : CMDU message
     * @return True on success
     *
     * one TLV holds info about all radios.
     * only 20MHz opClasses / channels are added to the Channel Scan Capabilities TLV
     */
    static bool add_tlv_channel_scan_capabilities(ieee1905_1::CmduMessageTx &cmdu_tx);

    /**
     * @brief Compute the map {OpClass: vector<Channel>} of Channels that the Agent can
     * scan
     * @param[in] radio : pointer to AgentDB radio instance
     * @param[out] scan_map : map of 20MHz channels available for scanning
     * @return True on success
     */
    static bool get_channel_scan_map(beerocks::AgentDB::sRadio *radio,
                                     std::map<uint8_t, std::vector<uint8_t>> &scan_map);
};

} // namespace beerocks

std::vector<uint8_t> get_operating_class_non_oper_channels(
    const std::unordered_map<uint8_t, beerocks::AgentDB::sRadio::sChannelInfo> &channels_list,
    uint8_t operating_class);

#endif // __TLVF_UTILS_H__
