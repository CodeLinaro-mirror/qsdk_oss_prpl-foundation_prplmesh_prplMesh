/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <beerocks/tlvf/beerocks_message.h>
#include <functional>
#include <map>
#include <tlvf/CmduMessageTx.h>
#include <vector>

/*
 * Framework for vendors for adding the Vendor Specific TLV to CMDU message.
 * This file multiVendor.h contains all the information
 * related to Vendor Specific TLV implementation.
 * This framework will ensure that any vendor can add their new TLVs
 * in the existing prplMesh messages.
 * Steps to follow:
 * 1) Create a Vendor Namespace. This will contain all the vendor specific data.
 * 2) Define the Vendor OUI: In the vendor-specific CPP file, define the new OUI and push it into the vendors_oui vector
 *    within the vendor's class constructor.
 * 3) Register TLV Handlers: In the vendor-specific CPP file, use the class constructor to register TLV handler functions
 *    for the vendor-specific message types in the `tlv_function_table` using the OUI and message type.
 */

namespace multi_vendor {

class tlvf_handler {

public:
    /**
	 * @brief Stores a list of vendor OUI (Organizationally Unique Identifiers).
	 *
	 * This static member holds a vector of OUI values used for vendor-specific
	 * TLV (Type-Length-Value) handling.
	 */
    static std::vector<uint32_t> vendors_oui;

    /**
	 * @brief Type definition for a function that processes TLV messages.
	 *
	 * This typedef defines a function pointer type that takes a reference to a
	 * `ieee1905_1::CmduMessageTx` object as single TLV function pointer argument
	 * and returns a boolean value. It is used for
	 * mapping TLV types to their respective handling functions.
	 */
    typedef std::function<bool(ieee1905_1::CmduMessageTx &)> tlv_function_t;

    /**
	 * @brief Maps message types to TLV handler functions.
	 *
	 * This static member maps a pair consisting of a vendor OUI and a Vendor message
	 * type to a vector of TLV handler functions. Each pair represents a combination of vendor
	 * and message type for which specific TLV processing functions can be registered and invoked.
	 */
    static std::map<std::pair<uint32_t, ieee1905_1::eMessageType>, std::vector<tlv_function_t>>
        tlv_function_table;

    /**
	 * @brief Adds a vendor-specific TLV to a CMDU message.
	 *
	 * This static method adds vendor’s TLV functions to the CMDU message based on the
	 * given message type.
	 *
	 * @param[in] vendor_oui The vendor OUI to use for the TLV.
	 * @param[in,out] cmdu_tx The CMDU message to which the TLV will be added.
	 * @param[in] msg_type The message type for which the TLV should be added.
	 *
	 * @return True if the TLV was successfully added, false otherwise.
	 */
    static bool add_tlv(uint32_t, ieee1905_1::CmduMessageTx &cmdu_tx,
                        ieee1905_1::eMessageType msg_type);
};

} // namespace multi_vendor
