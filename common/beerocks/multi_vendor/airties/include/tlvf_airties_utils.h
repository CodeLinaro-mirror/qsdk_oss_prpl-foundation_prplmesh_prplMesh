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

    static bool add_airties_ethernet_stats_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool add_radio_capability(ieee1905_1::CmduMessageTx &cmdu_tx);

    /**
     * @brief Assigns a sequential port ID for the given interface name.
     *
     * Each interface name is mapped to a unique, persistent ID.
     * If the interface was already assigned an ID, the same ID is returned.
     * Otherwise, the next available ID is assigned, starting from 1.
     *
     * @param interface_name The interface name used as the key (e.g., "eth0", "lan1", "wan").
     * @return The assigned port ID for this interface.
     */
    static uint8_t assign_unique_port_id(const std::string &interface_name);
};
} // namespace airties

#endif // __TLVF_AIRTIES_UTILS_H__
