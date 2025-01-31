/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __AIRTIES_UTILS_H__
#define __AIRTIES_UTILS_H__

#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>

#include <beerocks/tlvf/beerocks_message.h>
#include <vendor_message_slave.h>

#include "ambiorix_client.h"
#include "wbapi_utils.h"

using namespace vendor_message;

namespace airties {

class AirtiesUtils {
public:
    beerocks::wbapi::AmbiorixClient m_ambiorix_cl;

    AirtiesUtils()
    {
        LOG_IF(!m_ambiorix_cl.connect(AMBIORIX_USP_BACKEND_PATH, AMBIORIX_PWHM_USP_BACKEND_URI),
               FATAL)
            << "Unable to connect to the ambiorix backend!";
    }

    /**
     * @brief Static list of Airties vendor-specific TLVs.
     */
    static std::vector<std::shared_ptr<ieee1905_1::tlvVendorSpecific>> airties_vs_tlv_list;

    /**
     * @brief Parse Airties TLVs from the incoming VS message.
     * @param btl_ctx Context of class VendorMessageSlave.
     * @param dst_mac Destination MAC address.
     * @param cmdu_rx Context for received cmdu messages.
     * @param cmdu_tx Context for transmitted cmdu messages.
     * @return true on success, false otherwise.
     */
    static bool parse_airties_tlv(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                  ieee1905_1::CmduMessageRx &cmdu_rx,
                                  ieee1905_1::CmduMessageTx &cmdu_tx);

    /**
     * @brief It will perform the reboot or factory reset depends upon an action_value.
     * @param action value of either reboot or factory reset
     * @param offset of the payload info.
     * @return false if unsuccessed.
     */
    static bool handle_reboot_and_factory_reset(uint8_t action_value);

    /**
     * @brief parse the Airties Reboot TLV and handled the incoming action from the controller.
     * @param context of class VendorMessageSlave.
     * @param Controller mac address.
     * @param cmdu tx context.
     * @param cmdu rx context.
     * @return true on success and false otherwise.
     */
    static bool parse_tlv_reboot_reset(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                       ieee1905_1::CmduMessageTx &cmdu_tx,
                                       ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Parse Airties message type TLV from the incoming VS message.
     * @param btl_ctx Context of class VendorMessageSlave.
     * @param dst_mac Destination MAC address.
     * @param cmdu_rx Context for received cmdu messages.
     * @param cmdu_tx Context for transmitted cmdu messages.
     * @return true on success, false otherwise.
     */
    static bool parse_airties_message_type_tlv(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                               ieee1905_1::CmduMessageRx &cmdu_rx,
                                               ieee1905_1::CmduMessageTx &cmdu_tx);
};

static AirtiesUtils airties_instance;

} // namespace airties

#endif // __AIRTIES_UTILS_H__
