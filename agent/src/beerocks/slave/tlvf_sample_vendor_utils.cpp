/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_sample_vendor_utils.h"
#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <cstring>
#include <easylogging++.h>
#include <tlvf/sample_vendor/tlvSampleVendor.h>

using namespace sample_vendor;

/**
 * @brief sample TLV to showcase the sample vendor specifc TLV
 *
 * This function demonstrates how to add VS TLV to the CMDU message.
 * This TLV will represent the Vendor OUI in the payload.
 * @param cmdu_tx The CMDU message to which the TLV will be added.
 * @return Returns true if the TLV was successfully added, false otherwise.
 */
bool tlvf_sample_vendor_utils::add_sample_vendor_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    LOG(INFO)
        << "Registered the sample TLV for sample_vendor but not adding it in the cmdu message "
           "So returning true in the begining of this function. This function is a reference for "
           "new vendors";
    return true;
    // Attempt to create a TLV for sample vendor message type
    auto tlv_sample_vendor = cmdu_tx.addClass<sample_vendor::tlvSampleVendor>();

    // Check if the TLV creation failed
    if (!tlv_sample_vendor) {
        LOG(ERROR) << "addClass wfa_map::tlvSampleVendor failed";
        return false;
    }

    // Set the vendor OUI for Airties
    tlv_sample_vendor->vendor_oui() =
        (sVendorOUI(sample_vendor::tlvSampleVendor::sampleVendorOUI::SAMPLE_OUI));
    LOG(INFO) << "Added sample VS TLV for sample vendor";
    return true;
}
