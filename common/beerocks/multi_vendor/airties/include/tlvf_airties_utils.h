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

namespace airties {

class tlvf_airties_utils {
public:
    beerocks::wbapi::AmbiorixClient m_ambiorix_cl;

    tlvf_airties_utils()
    {
        LOG_IF(!m_ambiorix_cl.connect(AMBIORIX_USP_BACKEND_PATH, AMBIORIX_PWHM_USP_BACKEND_URI),
               FATAL)
            << "Unable to connect to the ambiorix backend!";
    }

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

    static bool
    get_counters_info(std::shared_ptr<airties::tlvAirtiesEthernetStats> &tlvAirtiesEthStats);
    static bool get_all_counters_info(
        std::shared_ptr<airties::tlvAirtiesEthernetStatsallcntr> &tlvAirtiesEthStats);
};
static tlvf_airties_utils tlvf_air_utils;
} // namespace airties

#endif // __TLVF_AIRTIES_UTILS_H__
