/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __TLVF_AIRTIES_UTILS_H__
#define __TLVF_AIRTIES_UTILS_H__

#include <tlvf/CmduMessageTx.h>

#include <beerocks/tlvf/beerocks_message.h>
#include <tlvf/airties/tlvAirtiesEthernetStats.h>

#include "ambiorix_client.h"
#include "wbapi_utils.h"

namespace beerocks {
namespace bpl {
extern beerocks::wbapi::AmbiorixClient m_ambiorix_cl;
} // namespace bpl
} // namespace beerocks
namespace airties {

class tlvf_airties_utils {
public:
    bool is_airties_platform_common_stp_enabled() const;

    /**
     * @brief Adds a new Airties Version Reporting TLV to given message.
     *
     * @param[in,out] cmdu_tx CDMU message.
     *
     * @return True on success and false otherwise.
     */
    static bool add_airties_version_reporting_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool add_airties_deviceinfo_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool add_device_metrics(ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool add_airties_msgtype_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool add_airties_ethernet_interface_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);

    static uint64_t get_value_from_dm(std::string param, std::string cntr_path);

    static bool add_airties_ethernet_stats_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool add_radio_capability(ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool
    get_counters_info(std::shared_ptr<airties::tlvAirtiesEthernetStats> &tlvAirtiesEthStats);
    static bool get_all_counters_info(
        std::shared_ptr<airties::tlvAirtiesEthernetStatsallcntr> &tlvAirtiesEthStats);

    /**
     * @brief Assigns a unique port ID for the given interface name.
     *
     * This function ensures that each interface name is assigned a unique port ID.
     * If the interface has already been assigned an ID, it returns the same ID.
     *
     * The function attempts to extract a numeric suffix from the interface name
     * (e.g., "eth3" → 3 or "eth0" → 0, "eth0.1" -> 1, eth3-> 2) and uses it as the port ID if it's not already used.
     * Otherwise, it assigns the next available ID starting from 1.
     *
     * @param interface_name The name of the interface (e.g., "eth1", "wan0").
     * @return A unique port ID associated with the given interface name.
     */
    static uint8_t assign_unique_port_id(const std::string &alias_raw);
};
} // namespace airties

#endif // __TLVF_AIRTIES_UTILS_H__
