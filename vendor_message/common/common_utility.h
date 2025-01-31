/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __COMMON_UTILS_H__
#define __COMMON_UTILS_H__

#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>

#include "vendor_message_slave.h"
#include <beerocks/tlvf/beerocks_message.h>

namespace vendor_message {

/**
     * @brief Hold list of vendor OUI values.
     */
extern std::vector<uint32_t> vendors_oui_list;

/**
     * @brief Extract 8 bit of the payload information.
     * @param payload buffer.
     * @param offset of the payload info.
     * @return true on success and false otherwise.
     */
uint8_t extract_uint8(const uint8_t *buffer, size_t offset);

/**
     * @brief Extract 16 bit of the payload information.
     * @param payload buffer.
     * @param offset of the payload info.
     * @return true on success and false otherwise.
     */
uint16_t extract_uint16(const uint8_t *buffer, size_t offset);

/**
     * @brief This function will collect the vendor-specific tlv into the each vendors vector and vendor's oui.
     * Parsing will be done for each vendor.
     * @param context of class VendorMessageSlave.
     * @param Controller mac address.
     * @param cmdu rx context.
     * @param cmdu tx context.
     * @return true on success and false otherwise.
     */
bool parse_vendor_message_type(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx,
                               ieee1905_1::CmduMessageTx &cmdu_tx);
} // namespace vendor_message
#endif // __COMMON_UTILS_H_
