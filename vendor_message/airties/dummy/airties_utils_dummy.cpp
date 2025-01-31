/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "airties_utils_dummy.h"
#include "vendor_message_slave.h"

#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>

using namespace airties;
using namespace vendor_message;

bool AirtiesUtils::handle_reboot_and_factory_reset(uint8_t action_value)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}

bool AirtiesUtils::parse_airties_tlv(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                     ieee1905_1::CmduMessageRx &cmdu_rx,
                                     ieee1905_1::CmduMessageTx &cmdu_tx)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}

bool AirtiesUtils::parse_airties_message_type_tlv(VendorMessageSlave &btl_ctx,
                                                  const sMacAddr &dst_mac,
                                                  ieee1905_1::CmduMessageRx &cmdu_rx,
                                                  ieee1905_1::CmduMessageTx &cmdu_tx)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}

bool AirtiesUtils::parse_tlv_reboot_reset(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                          ieee1905_1::CmduMessageTx &cmdu_tx,
                                          ieee1905_1::CmduMessageRx &cmdu_rx)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}
