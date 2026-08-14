/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BWL_AFC_SPECTRUM_HELPER_H_
#define _BWL_AFC_SPECTRUM_HELPER_H_

#include <bcl/beerocks_string_utils.h>

#include <cstdint>
#include <string>
#include <unordered_set>

namespace bwl {
namespace whm {
namespace afc_spectrum_helper {

constexpr char AFC_STATS_PATH[]                = "Device.WiFi.AFC.Stats.";
constexpr char AFC_REQUEST_PARAM[]             = "AvailableSpectrumRequest";
constexpr char AFC_RESPONSE_PARAM[]            = "AvailableSpectrumResponse";
constexpr char AFC_GRANT_STATUS_PARAM[]        = "GrantStatus";
constexpr char RADIO_POWER_TYPE_PARAM[]        = "PowerType";
constexpr char RADIO_POSSIBLE_CHANNELS_PARAM[] = "PossibleChannels";

/**
 * @brief Read AFC Available Spectrum Inquiry request/response from platform datamodel.
 */
bool read_available_spectrum_inquiry_data(std::string &request, std::string &response);

/**
 * @brief Check whether the latest AFC grant completed successfully.
 */
bool is_afc_grant_successful();

/**
 * @brief Check whether the given radio operates in 6 GHz Standard Power mode.
 */
bool is_standard_power_mode(const std::string &radio_iface_name);

/**
 * @brief Parse a comma-separated PossibleChannels DM string into channel indices.
 *
 * Header-inline so unit tests can cover parsing without linking Ambiorix/WHM.
 */
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

/**
 * @brief Read AFC-allowed channels from Device.WiFi.Radio.{i}.PossibleChannels.
 */
bool read_possible_channels(const std::string &radio_iface_name,
                            std::unordered_set<uint8_t> &channels);

/**
 * @brief True when AFC regulatory Channel Preference reporting applies on this radio.
 */
bool is_afc_regulatory_preference_applicable(const std::string &radio_iface_name);

} // namespace afc_spectrum_helper
} // namespace whm
} // namespace bwl

#endif // _BWL_AFC_SPECTRUM_HELPER_H_
