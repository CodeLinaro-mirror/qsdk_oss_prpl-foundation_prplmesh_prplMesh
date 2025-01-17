/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef __TLVF_AIRTIES_UTILS_H__
#define __TLVF_AIRTIES_UTILS_H__

#include <tlvf/CmduMessageTx.h>

#include <beerocks/tlvf/beerocks_message.h>

namespace airties {

class tlvf_airties_utils {
public:
    bool is_airties_platform_common_stp_enabled() const;

    /**
     * @brief Adds a new Airties Version Reporting TLV to given message.
     *
     * @param[in,out] cmdu_tx CDMU message.
     *
     * @return True on success and false otherwise.
     */
    static bool add_airties_version_reporting_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);

    static bool add_airties_msgtype_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);
};

} // namespace airties

#endif // __TLVF_AIRTIES_UTILS_H__
