/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include "tlvf_sample_vendor_utils.h"
#include <iomanip>
#include <type_traits>
#include <vector>

using namespace sample_vendor;
using namespace ieee1905_1;

// Define a OUI with your <vendor-oui> like SAMPLE_OUI.
#define SAMPLE_OUI 0x123456

/**
 * @class multi_vendor_sample
 * @brief Sample implementation for handling vendor-specific TLVs in IEEE 1905.1 messages.
 *
 * This class serves as a blueprint for new vendors who want to add their vendor-specific TLV
 * handlers to the prplMesh framework. It demonstrates the necessary steps for registering
 * vendor-specific information and functions for handling certain message types.
 *
 * **Instructions for New Vendors:**
 * 1. **Define a unique OUI** for your vendor.
 * 2. **Register your OUI** with the `vendors_oui` vector to ensure it is recognized.
 * 3. **Implement your vendor-specific TLV functions**:
 *    - Create a class similar to `tlvf_sample_vendor_utils` to define your TLV handling functions.
 *    - Ensure your class inherits from both `multi_vendor::tlvf_handler` and your utility class.
 * 4. **Register your TLV functions** with the appropriate IEEE 1905.1 message types by
 *    adding them to the `tlv_function_table` in the constructor of your class.
 * 5. **Handle Additional Message Types**:
 *    - If you need to add TLV functions for other message types not currently handled, modify
 *      the `switch` statement in the constructor to include those message types and register
 *      the appropriate TLV functions.
 */
class multi_vendor_sample : public multi_vendor::tlvf_handler,
                            public sample_vendor::tlvf_sample_vendor_utils {
public:
    /**
     * @brief Constructor for the multi_vendor_sample class.
     *
     * The constructor performs the following steps:
     * - Registers the vendor's OUI to ensure the framework recognizes it.
     * - Associates vendor-specific TLV handlers with specific IEEE 1905.1 message types.
     *
     * **Note for New Vendors:**
     * When adding your own vendor implementation, you should:
     * - Register your unique OUI.
     * - Add your TLV functions to the `tlv_function_table` for each relevant message type.
     * - Inherit from a utility class similar to `tlvf_sample_vendor_utils` that contains
     *   your TLV functions.
     * - If handling additional message types, ensure to add the necessary `case` statements
     *   in the `switch` below and map your TLV functions accordingly.
     */
    multi_vendor_sample()
    {
        LOG(INFO) << "Constructor called of class multi_vendor_sample";

        // Step 1: Register the vendor OUI in the vendors_oui vector.
        vendors_oui.push_back(SAMPLE_OUI);

        // Step 2: Loop through each message type the vendor will handle and register the
        // appropriate TLV handlers.
        for (auto msg_type : m_message_types) {
            switch (msg_type) {
            // Register the TLV handler for the AP_AUTOCONFIGURATION_SEARCH_MESSAGE.
            case ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE: {
                tlv_function_table
                    [std::make_pair(SAMPLE_OUI,
                                    ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE)]
                        .push_back(add_sample_vendor_tlv);
                break;
            }
            // Register the TLV handler for the AP_CAPABILITY_REPORT_MESSAGE.
            case ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE: {
                tlv_function_table[std::make_pair(
                                       SAMPLE_OUI,
                                       ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE)]
                    .push_back(add_sample_vendor_tlv);
                break;
            }
            default: {
                // Log an error for unrecognized message types
                LOG(ERROR) << "This msg type " << msg_type
                           << " is not added in the m_message_types vector of this OUI :"
                           << std::hex << SAMPLE_OUI;
                break;
            }
            }
        }
    }

private:
    /**
     * @brief List of IEEE 1905.1 message types handled by this vendor.
     *
     * Vendor should specify all message types they intend to handle with their TLV in this vector.
     * This list should include all message types that require custom TLV processing.
     *
     * **Note for New Vendors:**
     * Add all message types your vendor will handle to this list. Ensure that you also
     * add the necessary `case` statements in the constructor's `switch` to register
     * your TLV functions.
     */
    const std::vector<ieee1905_1::eMessageType> m_message_types = {
        ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE,
        ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE};
};

//Step 3: Create a static instance of the vendor class.
//This ensures that the vendor's handlers are registered automatically when the program starts.
//New vendor should create a similar static object for their class to ensure proper initialization.
static multi_vendor_sample sample_vendor_obj;
