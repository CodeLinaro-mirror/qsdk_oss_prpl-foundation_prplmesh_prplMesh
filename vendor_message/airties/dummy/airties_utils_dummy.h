/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __AIRTIES_UTILS_DUMMY_H__
#define __AIRTIES_UTILS_DUMMY_H__

#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>

#include "vendor_message_slave.h"
#include <beerocks/tlvf/beerocks_message.h>

using namespace vendor_message;

namespace airties {

class AirtiesUtils {
public:
    static std::vector<std::shared_ptr<ieee1905_1::tlvVendorSpecific>> airties_vs_tlv_list;

    /**
     * @brief It will perform the reboot or factory reset depends upon an action_value.
     * @param action value of either reboot or factory reset
     * @param offset of the payload info.
     * @return false if unsuccessed.
     */
    static bool handle_reboot_and_factory_reset(uint8_t action_value);

    static bool parse_airties_tlv(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                  ieee1905_1::CmduMessageRx &cmdu_rx,
                                  ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool parse_airties_message_type_tlv(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                               ieee1905_1::CmduMessageRx &cmdu_rx,
                                               ieee1905_1::CmduMessageTx &cmdu_tx);

    /**
     * @brief Validation of next tlv in AirTies vendor specific message.
     * @param context of class VendorMessageSlave.
     * @param Controller mac address.
     * @param cmdu tx context.
     * @param cmdu rx context.
     * @param Pointer to next tlv.
     * @return true on success and false otherwise.
     */
    static bool parse_tlv_reboot_reset(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                       ieee1905_1::CmduMessageTx &cmdu_tx,
                                       ieee1905_1::CmduMessageRx &cmdu_rx);
};
} // namespace airties
#endif // __AIRTIES_UTILS_DUMMY_H__
