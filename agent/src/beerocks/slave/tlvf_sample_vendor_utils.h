/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __TLVF_SAMPLE_VENDOR_UTILS_H__
#define __TLVF_SAMPLE_VENDOR_UTILS_H__

#include <tlvf/CmduMessageTx.h>

#include <beerocks/tlvf/beerocks_message.h>

namespace sample_vendor {

class tlvf_sample_vendor_utils {
public:
    /**
     * @brief Adds a new Sample vendor TLV to given message.
     *
     * @param[in,out] cmdu_tx CDMU message.
     *
     * @return True on success and false otherwise.
     */
    static bool add_sample_vendor_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);
};

} // namespace sample_vendor

#endif // __TLVF_AIRTIES_UTILS_H__
