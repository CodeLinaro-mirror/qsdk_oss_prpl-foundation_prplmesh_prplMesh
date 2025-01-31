/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "common_utility.h"
#include "vendor_message_slave.h"
#include <airties_utils.h>
#include <tlvf/airties/tlvAirtiesMsgType.h>

using namespace airties;

namespace vendor_message {

std::vector<uint32_t> vendors_oui_list;

uint8_t extract_uint8(const uint8_t *buffer, size_t offset) { return buffer[offset]; }

uint16_t extract_uint16(const uint8_t *buffer, size_t offset)
{
    return (buffer[offset] << 8) | buffer[offset + 1];
}

bool parse_vendor_message_type(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx,
                               ieee1905_1::CmduMessageTx &cmdu_tx)
{
    if (!cmdu_rx.getMessageLength()) {
        LOG(ERROR) << "cmdu is not initialized!";
        return false;
    }

    //Get the list of tlv present in the VS message into an each vendor separate vector.
    for (auto const &tlv : cmdu_rx.getClassList<ieee1905_1::tlvVendorSpecific>()) {
        if (!tlv) {
            LOG(ERROR) << "getClass of Vendor Specific null";
            continue;
        }

        auto tlv_vendor_oui = tlv->vendor_oui();

        // Process TLV based on the vendor OUI.
        switch (tlv_vendor_oui) {
        case (airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES):
            //This vector will be used for iterating over the oui's for parsing the tlv.
            vendors_oui_list.push_back(tlv_vendor_oui);
            //Push back of airties TLVs present in the VS Message into the vector that is defined in airties_utils file.
            airties::AirtiesUtils::airties_vs_tlv_list.push_back(tlv);
            continue;

        default:
            continue;
        }
    }

    if (vendors_oui_list.empty()) {
        LOG(DEBUG) << "vendors_oui_list is empty, skipping TLV parsing.";
        return true;
    }

    // Create a set of unique vendor OUIs from the vector
    std::set<uint32_t> uniqueSet(vendors_oui_list.begin(), vendors_oui_list.end());
    std::vector<uint32_t> uniqueVec(uniqueSet.begin(), uniqueSet.end());

    // Parse TLVs for each unique vendor OUI
    for (const uint32_t &v_oui : uniqueVec) {
        switch (v_oui) {
        case (airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES): {
            if (airties::AirtiesUtils::airties_vs_tlv_list.empty()) {
                LOG(DEBUG) << "airties_vs_tlv_list is empty, skipping Airties TLV parsing.";
                break;
            }
            if (!airties::AirtiesUtils::parse_airties_tlv(btl_ctx, dst_mac, cmdu_rx, cmdu_tx)) {
                LOG(ERROR) << "Failed to parse Airties";
                return false;
            }
        } break;
        default:
            LOG(DEBUG) << " No other vendors support for now";
            break;
        }
    }
    vendors_oui_list.clear();
    return true;
}
} // namespace vendor_message
