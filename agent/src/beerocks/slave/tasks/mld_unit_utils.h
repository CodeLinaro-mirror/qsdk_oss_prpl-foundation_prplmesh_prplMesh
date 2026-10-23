/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _MLD_UNIT_UTILS_H_
#define _MLD_UNIT_UTILS_H_

#include "../agent_db.h"

#include <bcl/beerocks_logging.h>
#include <beerocks/tlvf/beerocks_message_common.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace beerocks {
namespace mld_unit_utils {

/**
 * @brief Select a preconfigured MLD unit or the first unused unit.
 *
 * Reuse units only from the requested SSID's affiliated radios, without sharing a unit with
 * another AP MLD. All local units remain reserved when allocating a new unit.
 */
inline int8_t select_ap_mld_unit(const std::string &ssid,
                                 const std::vector<sMacAddr> &affiliated_ruids, uint8_t max_mlds,
                                 const std::vector<AgentDB::sAPMLDConfiguration> &assigned_mlds,
                                 const std::vector<AgentDB::sRadio *> &radios)
{
    std::unordered_set<int8_t> used_mld_units;
    std::unordered_set<int8_t> other_mld_units;
    int8_t preconfigured_mld_unit = DISABLED_MLDUNIT;

    for (const auto &ap_mld : assigned_mlds) {
        if (ap_mld.mld_config.mld_unit != DISABLED_MLDUNIT) {
            used_mld_units.insert(ap_mld.mld_config.mld_unit);
            other_mld_units.insert(ap_mld.mld_config.mld_unit);
        }
    }

    for (const auto *radio : radios) {
        const bool affiliated = std::find(affiliated_ruids.begin(), affiliated_ruids.end(),
                                          radio->front.iface_mac) != affiliated_ruids.end();
        for (const auto &bss : radio->front.bssids) {
            if (bss.mld_id == DISABLED_MLDUNIT) {
                continue;
            }
            used_mld_units.insert(bss.mld_id);
            if (!affiliated || bss.configured_ssid != ssid) {
                other_mld_units.insert(bss.mld_id);
                continue;
            }
            if (preconfigured_mld_unit != DISABLED_MLDUNIT &&
                preconfigured_mld_unit != bss.mld_id) {
                LOG(ERROR) << "Conflicting preconfigured MLD units for AP MLD " << ssid;
                return DISABLED_MLDUNIT;
            }
            preconfigured_mld_unit = bss.mld_id;
        }
    }

    if (preconfigured_mld_unit != DISABLED_MLDUNIT) {
        if (other_mld_units.count(preconfigured_mld_unit) == 0) {
            return preconfigured_mld_unit;
        }
        LOG(WARNING) << "Preconfigured MLD unit " << int(preconfigured_mld_unit) << " for AP MLD "
                     << ssid << " is already used by a different AP MLD";
    }

    for (uint8_t mld_unit = 0; mld_unit < max_mlds; ++mld_unit) {
        if (used_mld_units.find(static_cast<int8_t>(mld_unit)) == used_mld_units.end()) {
            return static_cast<int8_t>(mld_unit);
        }
    }

    LOG(DEBUG) << "No available MLD unit found, returning DISABLED_MLDUNIT";
    return DISABLED_MLDUNIT;
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

inline void update_vaps_mld_units(
    std::array<AgentDB::sRadio::sFront::sBssid, beerocks::IFACE_TOTAL_VAPS> &bssids,
    const beerocks_message::sVapMldUnit vap_mld_units[])
{
    for (size_t vap_idx = 0; vap_idx < bssids.size(); ++vap_idx) {
        auto &bss                 = bssids[vap_idx];
        const int expected_vap_id = int(beerocks::IFACE_VAP_ID_MIN) + int(vap_idx);
        if (vap_mld_units[vap_idx].vap_id != expected_vap_id) {
            LOG(WARNING) << "vap_mld_units mapping mismatch: idx=" << vap_idx
                         << " expected_vap_id=" << expected_vap_id
                         << " got=" << int(vap_mld_units[vap_idx].vap_id);
            bss.mld_id = DISABLED_MLDUNIT;
            bss.configured_ssid.clear();
            continue;
        }
        bss.mld_id          = vap_mld_units[vap_idx].mld_unit;
        bss.configured_ssid = vap_mld_units[vap_idx].configured_ssid;
    }
}

} // namespace mld_unit_utils
} // namespace beerocks

#endif // _MLD_UNIT_UTILS_H_
