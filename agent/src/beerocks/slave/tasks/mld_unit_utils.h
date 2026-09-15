/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _MLD_UNIT_UTILS_H_
#define _MLD_UNIT_UTILS_H_

#include <bcl/beerocks_defines.h>
#include <beerocks/tlvf/beerocks_message_common.h>

#include <bitset>

namespace beerocks {
namespace mld_unit_utils {

template <typename BssContainer>
std::bitset<beerocks::IFACE_TOTAL_VAPS>
update_vaps_mld_units(BssContainer &bssids, const beerocks_message::sVapMldUnit vap_mld_units[])
{
    std::bitset<beerocks::IFACE_TOTAL_VAPS> mismatched_vap_ids;

    for (size_t vap_idx = 0; vap_idx < bssids.size(); ++vap_idx) {
        auto &bss                 = bssids[vap_idx];
        const int expected_vap_id = int(beerocks::IFACE_VAP_ID_MIN) + int(vap_idx);
        if (vap_mld_units[vap_idx].vap_id != expected_vap_id) {
            mismatched_vap_ids.set(vap_idx);
            bss.mld_id = DISABLED_MLDUNIT;
            bss.configured_ssid.clear();
            continue;
        }
        bss.mld_id          = vap_mld_units[vap_idx].mld_unit;
        bss.configured_ssid = vap_mld_units[vap_idx].configured_ssid;
    }

    return mismatched_vap_ids;
}

} // namespace mld_unit_utils
} // namespace beerocks

#endif // _MLD_UNIT_UTILS_H_
