/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "common_utility.h"
#include "vendor_message_slave.h"

namespace vendor_message {

uint8_t extract_uint8(const uint8_t *buffer, size_t offset) { return buffer[offset]; }

uint16_t extract_uint16(const uint8_t *buffer, size_t offset)
{
    return (buffer[offset] << 8) | buffer[offset + 1];
}

bool parse_vendor_message_type(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx,
                               ieee1905_1::CmduMessageTx &cmdu_tx)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}
} // namespace vendor_message
