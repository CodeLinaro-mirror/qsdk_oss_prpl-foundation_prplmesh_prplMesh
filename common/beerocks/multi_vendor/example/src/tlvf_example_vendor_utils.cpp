/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_example_vendor_utils.h"
#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <cstring>
#include <easylogging++.h>
#include <tlvf/example_vendor/tlvExampleVendor.h>

using namespace example_vendor;

/**
 * @brief example TLV to showcase the example vendor specifc TLV
 *
 * This function demonstrates how to add VS TLV to the CMDU message.
 * This TLV will represent the Vendor OUI in the payload.
 * @param cmdu_tx The CMDU message to which the TLV will be added.
 * @return Returns true if the TLV was successfully added, false otherwise.
 */
bool tlvf_example_vendor_utils::add_example_vendor_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    LOG(INFO)
        << "Registered the example TLV for example_vendor but not adding it in the cmdu message "
           "So returning true in the begining of this function. This function is a reference for "
           "new vendors";
    return true;

    // Attempt to create a TLV for example vendor message type
    auto tlv_example_vendor = cmdu_tx.addClass<example_vendor::tlvExampleVendor>();

    // Check if the TLV creation failed
    if (!tlv_example_vendor) {
        LOG(ERROR) << "addClass example_vendor::tlvExampleVendor failed";
        return false;
    }

    // Set the vendor OUI for example
    tlv_example_vendor->vendor_oui() =
        (sVendorOUI(example_vendor::tlvExampleVendor::exampleVendorOUI::EXAMPLE_OUI));
    LOG(INFO) << "Added example VS TLV for example vendor";
    return true;
}
