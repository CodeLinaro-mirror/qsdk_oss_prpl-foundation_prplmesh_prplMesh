/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

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
 * 2) Define the Vendor OUI: In the vendor-specific CPP file,
 * 3) Register TLV Handlers: In the vendor-specific class constructor,
 *    register TLV handler functions for the vendor-specific message types
 *    in the `tlv_function_table` using the vendor's OUI as the key in the
 *    outer map and the message type as the key in the inner map.
 */

namespace multi_vendor {

class tlvf_handler {

public:
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
     * @brief Maps vendor OUIs and message types to TLV handler functions.
     *
     * This static member represents a nested map structure. The outer map uses
     * the vendor OUI as the key, and the inner map uses the message type
     * (`ieee1905_1::eMessageType`) as the key. Each inner map contains a
     * vector of TLV handler functions (`tlv_function_t`) that can be
     * registered for specific combinations of vendor OUI and message type.
     */
    static std::map<uint32_t, std::map<ieee1905_1::eMessageType, std::vector<tlv_function_t>>>
        tlv_function_table;
    /**
     * @brief Adds a vendor-specific TLV to a CMDU message.
     *
     * This static method looks up the vendor-specific TLV handler functions
     * for the given OUI and message type, and applies them to the CMDU message.
     *
     * @param[in] vendor_oui The vendor OUI to use for the TLV.
     * @param[in,out] cmdu_tx The CMDU message to which the TLV will be added.
     * @param[in] msg_type The message type for which the TLV should be added.
     *
     * @return True if the TLV was successfully added, false otherwise.
     */
    static bool add_vs_tlv(uint32_t oui, ieee1905_1::CmduMessageTx &cmdu_tx,
                           ieee1905_1::eMessageType msg_type);

    /**
     * @brief Adds TLVs for all vendors based on the message type.
     *
     * This static method iterates through all registered vendor OUIs in
     * `tlv_function_table` and applies their respective TLV handler functions
     * for the specified message type to the CMDU message.
     *
     * @param[in,out] cmdu_tx The CMDU message to which the TLVs will be added.
     * @param[in] msg_type The message type for which the TLVs should be added.
     *
     * @return True if all relevant TLVs were successfully added, false otherwise.
     */
    static bool add_vs_tlv(ieee1905_1::CmduMessageTx &cmdu_tx, ieee1905_1::eMessageType msg_type);
};

} // namespace multi_vendor
