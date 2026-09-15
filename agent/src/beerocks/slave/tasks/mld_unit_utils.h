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

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace beerocks {
namespace mld_unit_utils {

struct sMldUnitAssignment {
    sMldUnitAssignment(const std::string &ssid_, int8_t mld_unit_)
        : ssid(ssid_), mld_unit(mld_unit_)
    {
    }

    std::string ssid;
    int8_t mld_unit;
};

struct sMldUnitSelection {
    int8_t mld_unit                      = DISABLED_MLDUNIT;
    int8_t preconfigured_mld_unit        = DISABLED_MLDUNIT;
    bool conflicting_preconfigured_units = false;
    bool preconfigured_unit_in_use       = false;
};

/**
 * @brief Select a preconfigured MLD unit or the first unused unit.
 *
 * A preconfigured unit is authoritative only while it belongs to the requested SSID and is not
 * already owned by a different AP MLD.
 */
inline sMldUnitSelection
select_ap_mld_unit(const std::string &ssid, uint8_t max_mlds,
                   const std::vector<sMldUnitAssignment> &assigned_mld_units,
                   const std::vector<sMldUnitAssignment> &preconfigured_bss_units)
{
    sMldUnitSelection selection;
    std::unordered_set<int8_t> used_mld_units;

    for (const auto &assignment : assigned_mld_units) {
        if (assignment.mld_unit != DISABLED_MLDUNIT) {
            used_mld_units.insert(assignment.mld_unit);
        }
    }

    for (const auto &assignment : preconfigured_bss_units) {
        if (assignment.mld_unit == DISABLED_MLDUNIT) {
            continue;
        }

        used_mld_units.insert(assignment.mld_unit);
        if (assignment.ssid != ssid) {
            continue;
        }

        if (selection.preconfigured_mld_unit != DISABLED_MLDUNIT &&
            selection.preconfigured_mld_unit != assignment.mld_unit) {
            selection.conflicting_preconfigured_units = true;
            return selection;
        }
        selection.preconfigured_mld_unit = assignment.mld_unit;
    }

    if (selection.preconfigured_mld_unit != DISABLED_MLDUNIT) {
        const auto unit_belongs_to_other_ssid = [&](const auto &assignment) {
            return assignment.mld_unit == selection.preconfigured_mld_unit &&
                   assignment.ssid != ssid;
        };
        selection.preconfigured_unit_in_use =
            std::any_of(assigned_mld_units.begin(), assigned_mld_units.end(),
                        unit_belongs_to_other_ssid) ||
            std::any_of(preconfigured_bss_units.begin(), preconfigured_bss_units.end(),
                        unit_belongs_to_other_ssid);

        if (!selection.preconfigured_unit_in_use) {
            selection.mld_unit = selection.preconfigured_mld_unit;
            return selection;
        }
    }

    for (uint8_t mld_unit = 0; mld_unit < max_mlds; ++mld_unit) {
        if (used_mld_units.find(static_cast<int8_t>(mld_unit)) == used_mld_units.end()) {
            selection.mld_unit = static_cast<int8_t>(mld_unit);
            return selection;
        }
    }

    return selection;
}

// Membership comes from the current request; local units are reused only by the selector.
template <typename MldRequests, typename BssContainer>
void populate_mld_id_in_bss_infos(const std::string &radio_iface, const MldRequests &requests,
                                  BssContainer &bss_infos)
{
    auto radio_it = requests.find(radio_iface);
    for (auto &bss_info : bss_infos) {
        bss_info.mld_id = DISABLED_MLDUNIT;
        if (radio_it == requests.end()) {
            continue;
        }
        const auto &ssid_mld_map = radio_it->second;
        auto ssid_it             = ssid_mld_map.find(bss_info.payload_config.ssid);
        if (ssid_it != ssid_mld_map.end()) {
            bss_info.mld_id = std::get<0>(ssid_it->second);
        }
    }
}

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
