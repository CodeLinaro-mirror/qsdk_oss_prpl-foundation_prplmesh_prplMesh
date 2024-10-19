/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_vendor_example_utils.h"
#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <cstring>
#include <easylogging++.h>
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
