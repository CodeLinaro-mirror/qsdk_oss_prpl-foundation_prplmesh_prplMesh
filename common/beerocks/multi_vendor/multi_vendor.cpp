/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include <iomanip>

// Initialize the map that holds vendor-specific TLV handler functions.
// This nested map structure maps vendor OUIs (outer map) to message types (inner map),
// with each message type being associated with a vector of TLV handler functions.
// The functions stored in this map will be invoked to process TLVs for the respective
// vendor and message type combinations.
std::map<uint32_t, std::map<ieee1905_1::eMessageType,
                            std::vector<multi_vendor::tlvf_handler::tlv_function_t>>>
    multi_vendor::tlvf_handler::tlv_function_table;

bool multi_vendor::tlvf_handler::add_vs_tlv(ieee1905_1::CmduMessageTx &cmdu_tx,
                                            ieee1905_1::eMessageType msg_type)
{
    bool success = true;

    // Iterate over all entries (vendor OUI) in the function table.
    for (const auto &first_entry : tlv_function_table) {

        // Attempt to add TLVs for the current vendor
        if (!add_vs_tlv(first_entry.second, cmdu_tx, msg_type)) {
            LOG(ERROR) << "Failed invoking add_vs_tlv for Vendor OUI: " << std::hex
                       << first_entry.first << " in message type " << msg_type;
            success = false;
        }
    }

    return success;
}

bool multi_vendor::tlvf_handler::add_vs_tlv(
    const std::map<ieee1905_1::eMessageType,
                   std::vector<multi_vendor::tlvf_handler::tlv_function_t>> &function_table,
    ieee1905_1::CmduMessageTx &cmdu_tx, ieee1905_1::eMessageType msg_type)
{
    // Find the message type that will have the vector of functions.
    auto inner_it = function_table.find(msg_type);
    if (inner_it != function_table.end()) {
        bool success = true;
        // Execute all functions registered for this message type.
        for (const auto &func : inner_it->second) {
            // Invoke the handler function and accumulate success.
            success &= func(cmdu_tx);
        }
        return success;
    } else {
        LOG(WARNING) << "No TLV found for this message type : " << std::hex << msg_type;
        return true;
    }
}
