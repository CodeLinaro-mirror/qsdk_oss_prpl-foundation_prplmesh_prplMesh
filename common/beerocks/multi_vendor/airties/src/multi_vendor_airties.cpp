/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include "tlvf_airties_utils.h"
#include <iomanip>
#include <type_traits>
#include <vector>

using namespace airties;
using namespace ieee1905_1;

//Vendor has to define their Vendor OUI.
#define AIRTIES_OUI 0x8841fc

/**
 * @class multi_vendor_airties
 * @brief This class registers Airties vendor-specific TLV handlers for IEEE 1905.1 message types.
 *
 * Airties-specific TLV handlers are associated with specific message types.
 * The vendor must register their OUI and add their handler functions into the `tlv_function_table`
 * defined in the multi_vendor.h file.
 * @note Inherits `multi_vendor::tlvf_handler` to access vector 'vendors_oui'and
 * map 'tlv_function_table'
 * Inherits 'airties::tlvf_airties_utils` to use Airties-specific TLV handlers.
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

        // Loop through the message types that this vendor needs to handle.
        for (auto msg_type : m_message_types) {
            switch (msg_type) {
            case ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE: {
                // Add Airties vendor-specific TLV functions for
                // AP_AUTOCONFIGURATION_WSC_MESSAGE
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE]
                                      .push_back(add_airties_version_reporting_tlv);
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE]
                                      .push_back(add_airties_deviceinfo_tlv);

            } break;
            case ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE: {
                // Add Airties vendor-specific TLV function for
                // AP_AUTOCONFIGURATION_SEARCH_MESSAGE
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE]
                                      .push_back(add_airties_version_reporting_tlv);
            } break;
            case ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE: {
                // Add Airties vendor-specific  TLV function for
                // AP_CAPABILITY_REPORT_MESSAGE
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE]
                                      .push_back(add_airties_version_reporting_tlv);
            } break;
            case ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE: {
                // Add Airties vendor-specific  TLV function for
                // AP_METRICS_RESPONSE_MESSAGE
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE]
                                      .push_back(add_device_metrics);
                tlv_function_table[AIRTIES_OUI]
                                  [ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE]
                                      .push_back(add_airties_ethernet_stats_tlv);
            } break;
            case ieee1905_1::eMessageType::TOPOLOGY_RESPONSE_MESSAGE: {
                tlv_function_table[AIRTIES_OUI][ieee1905_1::eMessageType::TOPOLOGY_RESPONSE_MESSAGE]
                    .push_back(add_airties_ethernet_interface_tlv);
            } break;

            default: {
                // Log an error for unrecognized message types
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
<<<<<<< HEAD
        ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE};
        ieee1905_1::eMessageType::TOPOLOGY_RESPONSE_MESSAGE};
=======
        ieee1905_1::eMessageType::TOPOLOGY_RESPONSE_MESSAGE,
        ieee1905_1::eMessageType::AP_METRICS_RESPONSE_MESSAGE};
>>>>>>> 70934c7a9 (tlvf: Implement Ethernet Stats TLV)
};

// Static object of the Airties vendor object. This ensures the airties vendor's handlers are
// registered during the program's initialization.
static multi_vendor_airties airties_vendor_obj;
