/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include <iomanip>

// Vector to store vendor OUI used for processing vendor-specific TLVs.
// Each vendor class should register its OUI in this vector to ensure that its TLVs
// are processed by the handler. This vector is updated dynamically as each vendor
// class is instantiated. This ensures the new vendor's TLVs can be
// correctly processed by the handler.
std::vector<uint32_t> multi_vendor::tlvf_handler::vendors_oui;

// Initialize the map that will hold vendor specific TLV handler functions for each combination
// of vendor OUI and message type. The functions in the map will be used to
// process specific TLVs associated with those message types.
std::map<std::pair<uint32_t, ieee1905_1::eMessageType>,
         std::vector<multi_vendor::tlvf_handler::tlv_function_t>>
    multi_vendor::tlvf_handler::tlv_function_table;

bool multi_vendor::tlvf_handler::add_tlv(uint32_t vendors_oui, ieee1905_1::CmduMessageTx &cmdu_tx,
                                         ieee1905_1::eMessageType msg_type)
{
    // Find the appropriate TLV handler functions based on the vendor OUI and message type.
    auto it = tlv_function_table.find(std::make_pair(vendors_oui, msg_type));
    if (it != tlv_function_table.end()) {
        bool success = true;
        // Execute all functions registered for this vendor and message type.
        for (const auto &func : it->second) {
            success &= func(cmdu_tx); // Call each function in the list
        }
        return success;
    } else {
        // Log an error if no TLV functions are registered for the provided vendor OUI and message type.
        LOG(ERROR) << "Vendor OUI '" << std::hex << vendors_oui
                   << "' not registered any TLV for the msg type " << msg_type;
        return true;
    }
    return false;
}
