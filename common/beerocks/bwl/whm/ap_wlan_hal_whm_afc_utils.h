/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _AP_WLAN_HAL_WHM_AFC_UTILS_H_
#define _AP_WLAN_HAL_WHM_AFC_UTILS_H_

#include <bcl/beerocks_string_utils.h>

#include <ambiorix_variant.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_set>

namespace bwl {
namespace whm {
namespace afc_utils {

constexpr char AFC_STATS_PATH[]                = "Device.WiFi.AFC.Stats.";
constexpr char AFC_REQUEST_PARAM[]             = "AvailableSpectrumRequest";
constexpr char AFC_RESPONSE_PARAM[]            = "AvailableSpectrumResponse";
constexpr char AFC_GRANT_STATUS_PARAM[]        = "GrantStatus";
constexpr char RADIO_POWER_TYPE_PARAM[]        = "PowerType";
constexpr char RADIO_POSSIBLE_CHANNELS_PARAM[] = "PossibleChannels";

inline bool parse_possible_channels_list(const std::string &possible_channels_str,
                                         std::unordered_set<uint8_t> &channels)
{
    channels.clear();
    for (const auto &chan_str : beerocks::string_utils::str_split(possible_channels_str, ',')) {
        if (chan_str.empty()) {
            continue;
        }
        channels.insert(static_cast<uint8_t>(beerocks::string_utils::stoi(chan_str)));
    }
    return !channels.empty();
}

inline std::string channels_set_to_string(const std::unordered_set<uint8_t> &channels)
{
    std::ostringstream oss;
    bool first = true;
    for (const auto channel : channels) {
        if (!first) {
            oss << ',';
        }
        oss << static_cast<unsigned>(channel);
        first = false;
    }
    return oss.str();
}

/**
 * @brief Parse InquiryStatus from a pWHM AFC update Ambiorix notification.
 *
 * @param[in] value AFC update event payload.
 * @param[out] inquiry_status Parsed InquiryStatus when present.
 * @return true when Updates/InquiryStatus are present.
 */
inline bool parse_afc_update_inquiry_status(const beerocks::wbapi::AmbiorixVariant *value,
                                            std::string &inquiry_status)
{
    inquiry_status.clear();
    if (!value) {
        return false;
    }

    auto parameters = value->find_child("Updates");
    if (!parameters || parameters->empty()) {
        return false;
    }

    return parameters->read_child(inquiry_status, "InquiryStatus");
}

} // namespace afc_utils
} // namespace whm
} // namespace bwl

#endif // _AP_WLAN_HAL_WHM_AFC_UTILS_H_
