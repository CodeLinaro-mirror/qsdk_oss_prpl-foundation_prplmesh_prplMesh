/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "vendor_message_slave_dummy.h"

using namespace vendor_message;

bool VendorMessageSlave::init()
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}

bool VendorMessageSlave::send_cmdu_to_controller(const sMacAddr &dst_mac,
                                                 ieee1905_1::CmduMessageTx &cmdu_tx,
                                                 const uint16_t &mid,
                                                 ieee1905_1::eMessageType msg_type)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}
