/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __TLVF_VENDOR_EXAMPLE_UTILS_H__
#define __TLVF_VENDOR_EXAMPLE_UTILS_H__

#include <memory>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>

namespace vendor_example {

class tlvf_vendor_example_utils {
public:
    /**
     * @brief Adds a new Example vendor TLV to given message.
     *
     * @param[in,out] cmdu_tx CDMU message.
     *
     * @return True on success and false otherwise.
     */
    static bool add_vendor_example_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);

    /**
     * @brief Parses a vendor-specific TLV from the received message.
     *
     * This function peeks at the TLV buffer to check the OUI before parsing.
     * If the OUI matches the vendor example OUI, it parses and returns the TLV.
     * Otherwise, it returns nullptr to allow other parsers to try.
     *
     * @param[in] cmdu_rx Received CMDU message.
     *
     * @return Parsed TLV object on success, nullptr if this parser doesn't handle it.
     */
    static std::shared_ptr<BaseClass> parse_vendor_example_tlv(ieee1905_1::CmduMessageRx &cmdu_rx);
};

} // namespace vendor_example

#endif // __TLVF_EXAMPLE_UTILS_H__
