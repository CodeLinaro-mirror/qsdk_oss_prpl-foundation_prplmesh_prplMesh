/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __TLVF_VENDOR_EXAMPLE_UTILS_H__
#define __TLVF_VENDOR_EXAMPLE_UTILS_H__

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
};

} // namespace vendor_example

#endif // __TLVF_EXAMPLE_UTILS_H__
