/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_vendor_example_utils.h"
#include "../../multi_vendor.h"
#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <cstring>
#include <easylogging++.h>
#include <tlvf/ieee_1905_1/sVendorOUI.h>
#include <tlvf/vendor_example/tlvVendorExample.h>

using namespace vendor_example;

/**
 * @brief TLV example to showcase the vendor specifc TLV
 *
 * This function demonstrates how to add VS TLV to the CMDU message.
 * This TLV will represent the Vendor OUI in the payload.
 * @param cmdu_tx The CMDU message to which the TLV will be added.
 * @return Returns true if the TLV was successfully added, false otherwise.
 */
bool tlvf_vendor_example_utils::add_vendor_example_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    LOG(INFO)
        << "Registered the TLV example for vendor_example but not adding it in the cmdu message "
           "So returning true in the begining of this function. This function is a reference for "
           "new vendors";
    return true;

    // Attempt to create a TLV for example vendor message type
    auto tlv_vendor_example = cmdu_tx.addClass<vendor_example::tlvVendorExample>();

    // Check if the TLV creation failed
    if (!tlv_vendor_example) {
        LOG(ERROR) << "addClass vendor_example::tlvVendorExample failed";
        return false;
    }

    // Set the vendor OUI for example
    tlv_vendor_example->vendor_oui() =
        (sVendorOUI(vendor_example::tlvVendorExample::vendorExampleOUI::EXAMPLE_OUI));
    LOG(INFO) << "Added Vendor Specific TLV for vendor example";
    return true;
}

/**
 * @brief Parser example to showcase vendor-specific TLV parsing
 *
 * This function demonstrates how to parse a vendor-specific TLV from a received CMDU message.
 * It uses the safe_read_vendor_tlv_data helper to securely extract the OUI with comprehensive
 * bounds checking, preventing out-of-bounds memory access from malformed or truncated TLVs.
 *
 * Example usage patterns:
 * 1. Extract only OUI:
 *    safe_read_vendor_tlv_data(cmdu_rx, nullptr, 0, 0, &oui)
 *
 * 2. Extract OUI + 2-byte subtype immediately after OUI:
 *    safe_read_vendor_tlv_data(cmdu_rx, &subtype, sizeof(subtype), 0, &oui)
 *
 * 3. Extract OUI + 4-byte field at offset 2 after OUI:
 *    safe_read_vendor_tlv_data(cmdu_rx, &field, sizeof(field), 2, &oui)
 *
 * @param cmdu_rx The received CMDU message containing the TLV to parse.
 * @return Parsed TLV object on success, nullptr if this parser doesn't handle it.
 */
std::shared_ptr<BaseClass>
tlvf_vendor_example_utils::parse_vendor_example_tlv(ieee1905_1::CmduMessageRx &cmdu_rx)
{
    uint32_t oui = 0;

    // Use the safe helper to validate and extract OUI with comprehensive bounds checking
    // This prevents out-of-bounds memory access from malformed or truncated TLVs
    if (!multi_vendor::tlvf_handler::safe_read_vendor_tlv_data(cmdu_rx, nullptr, 0, 0, &oui)) {
        // Helper already logged the error
        return nullptr;
    }

    // Check if this is our vendor's OUI
    if (oui != vendor_example::tlvVendorExample::EXAMPLE_OUI) {
        // Not our vendor's TLV, return nullptr to let other parsers try
        LOG(DEBUG) << "OUI mismatch: 0x" << std::hex << oui << " (expected 0x"
                   << vendor_example::tlvVendorExample::EXAMPLE_OUI << ")";
        return nullptr;
    }

    // OUI matches! Now parse the TLV
    LOG(DEBUG) << "Parsing vendor example TLV with OUI: 0x" << std::hex << oui;
    auto tlv = cmdu_rx.addVendorClass<vendor_example::tlvVendorExample>();

    if (!tlv) {
        LOG(ERROR) << "Failed to parse vendor example TLV";
        return nullptr;
    }

    LOG(DEBUG) << "Successfully parsed vendor example TLV";
    return tlv;
}
