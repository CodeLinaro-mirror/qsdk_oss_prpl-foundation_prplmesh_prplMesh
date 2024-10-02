/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include "tlvf_example_vendor_utils.h"
#include <iomanip>
#include <type_traits>
#include <vector>

using namespace example_vendor;
using namespace ieee1905_1;

// Define a OUI with your <vendor-oui> like EXAMPLE_OUI.
#define EXAMPLE_OUI 0x123456

/**
 * @class multi_example_vendor
 * @brief Example implementation for handling vendor-specific TLVs in IEEE 1905.1 messages.
 *
 * This class demonstrates how a vendor can register their OUI and associate vendor-specific TLV
 * handlers with certain IEEE 1905.1 message types using a nested map. The outer map uses the
 * vendor OUI as the key, and the inner map associates specific message types with their corresponding
 * TLV handler functions.
 *
 * **Instructions for New Vendors:**
 * 1. **Define a unique OUI** for your vendor.
 * 2. **Implement your vendor-specific TLV functions**:
 *    - Create a utility class similar to `tlvf_example_vendor_utils` that contains your TLV functions.
 *    - Inherit from both `multi_vendor::tlvf_handler` and your utility class.
 * 3. **Register your TLV functions** for each relevant message type by adding them to the
 *    `tlv_function_table`, which is a nested map.
 *    - The first map key is the vendor OUI.
 *    - The second map key is the IEEE 1905.1 message type.
 *    - The value is a vector of function pointers to TLV handlers.
 * 4. **Handle Additional Message Types**:
 *    - If additional message types require TLV functions, update the m_message_types vector and constructor
 *      with the necessary `case` statements to register the functions accordingly.
 */
class multi_example_vendor : public multi_vendor::tlvf_handler,
                             public example_vendor::tlvf_example_vendor_utils {
public:
    /**
     * @brief Constructor for the multi_example_vendor class.
     *
     * The constructor registers the vendor's OUI and associates vendor-specific TLV handlers
     * with specific IEEE 1905.1 message types using a nested map structure.
     *
     * **Note for New Vendors:**
     * To extend this class for your vendor, you need to:
     * - Add your TLV functions to the `tlv_function_table` in the nested map format.
     * - Ensure to inherit from a utility class similar to `tlvf_example_vendor_utils` that defines
     *   your TLV functions.
     */
    multi_example_vendor()
    {
        LOG(INFO) << "Constructor called of class multi_example_vendor";

        // Step 1: Loop through each message type the vendor will handle and register the
        // appropriate TLV handlers.
        for (auto msg_type : m_message_types) {
            switch (msg_type) {
            // Register the TLV handler for the AP_AUTOCONFIGURATION_SEARCH_MESSAGE.
            case ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE: {

                tlv_function_table[EXAMPLE_OUI]
                                  [ieee1905_1::eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE]
                                      .push_back(add_example_vendor_tlv);

                break;
            }
            // Register the TLV handler for the AP_CAPABILITY_REPORT_MESSAGE.
            case ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE: {

                tlv_function_table[EXAMPLE_OUI]
                                  [ieee1905_1::eMessageType::AP_CAPABILITY_REPORT_MESSAGE]
                                      .push_back(add_example_vendor_tlv);

                break;
            }
            default: {
                LOG(WARNING) << "This msg type " << msg_type
                             << " is not added in the m_message_types vector of this OUI :"
                             << std::hex << EXAMPLE_OUI;
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
static multi_example_vendor example_vendor_obj;
