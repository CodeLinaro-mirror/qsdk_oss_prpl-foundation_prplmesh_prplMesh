/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include "tlvf_vendor_example_utils.h"

using namespace vendor_example;
using namespace ieee1905_1;

/**
 * @class MultiVendorExample
 * @brief Example implementation for handling vendor-specific TLVs in IEEE 1905.1 messages.
 *
 * How to implement for your vendor:
 *  1) Implement your TLV builder functions in a utility header (see tlvf_vendor_example_utils.h).
 *     Signature must be: bool fn(ieee1905_1::CmduMessageTx &);
 *  2) Create a small registrar class, inherit from tlvf_handler and your utils.
 *  3) In the constructor, call register_handler(eMessageType::<...>, your_fn) for each message.
 *  4) Create one static instance to run registrations at program startup.
 */
class MultiVendorExample : public multi_vendor::tlvf_handler,
                           public vendor_example::tlvf_vendor_example_utils {
public:
    MultiVendorExample()
    {
        LOG(INFO) << "MultiVendorExample: registering vendor-specific TLV handlers";

        // Register handlers for the messages you support
        multi_vendor::tlvf_handler::register_handler(
            eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE, add_vendor_example_tlv);

        multi_vendor::tlvf_handler::register_handler(eMessageType::AP_CAPABILITY_REPORT_MESSAGE,
                                                     add_vendor_example_tlv);

        // Add more registrations as needed
        // multi_vendor::tlvf_handler::register_handler(<MessageType>, <YourHandler>);
    }
};

// Ensure registration happens at startup.
namespace {
MultiVendorExample g_vendor_example_init;
}
