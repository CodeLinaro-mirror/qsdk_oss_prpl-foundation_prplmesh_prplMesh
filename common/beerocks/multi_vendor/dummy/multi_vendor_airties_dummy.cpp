/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include "tlvf_airties_utils_dummy.h"
#include <iomanip>
#include <type_traits>
#include <vector>

using namespace airties;
using namespace ieee1905_1;

//Vendor has to define their Vendor OUI.
#define AIRTIES_OUI 0x8841fc

/*
 * Dummy changes for Legacy Platform. 
 * The TLV functions are not implemented.
 * It can be a placeholder if it need to be
 * implemented if needed in future.
 */
class multi_vendor_airties : public multi_vendor::tlvf_handler, public airties::tlvf_airties_utils {
public:
    /**
     * @brief Constructor for the multi_vendor_airties class.
     *
     * The constructor registers the Airties OUI and associates the vendor-specific TLV handlers
     * with the appropriate IEEE 1905.1 message types. This ensures that the correct vendor-specific
     * TLVs are processed when these message types are sent.
     */
    multi_vendor_airties()
    {
        LOG(INFO) << "Constructor called for class multi_vendor_airties";

        for (auto msg_type : m_message_types) {
            switch (msg_type) {
            case ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE: {
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE]
                                      .push_back(add_airties_version_reporting_tlv);
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE]
                                      .push_back(add_airties_deviceinfo_tlv);

            } break;
            case ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE: {
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE]
                                      .push_back(add_airties_version_reporting_tlv);
            } break;
            case ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE: {
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE]
                                      .push_back(add_airties_version_reporting_tlv);
            } break;
            case ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE: {
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE]
                                      .push_back(add_device_metrics);
            } break;
            default: {
                LOG(WARNING) << "This msg type " << msg_type
                             << " is not added in the m_message_types vector for oui-" << std::hex
                             << AIRTIES_OUI;
            } break;
            }
        }
    }

private:
    // List of IEEE 1905.1 message types that this vendor handles with specific VS TLVs.
    const std::vector<ieee1905_1::eMessageType> m_message_types = {
        ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE,
        ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE,
        ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE,
        ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE};
};

// Static object of the Airties vendor object. This ensures the airties vendor's handlers are
// registered during the program's initialization.
static multi_vendor_airties airties_vendor_obj;
