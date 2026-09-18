/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _AFC_SPECTRUM_HELPER_H_
#define _AFC_SPECTRUM_HELPER_H_

#include <cstdint>
#include <string>
#include <unordered_set>

namespace beerocks {
namespace afc_spectrum_helper {

constexpr char AFC_STATS_PATH[]                = "Device.WiFi.AFC.Stats.";
constexpr char AFC_REQUEST_PARAM[]             = "AvailableSpectrumRequest";
constexpr char AFC_RESPONSE_PARAM[]            = "AvailableSpectrumResponse";
constexpr char AFC_GRANT_STATUS_PARAM[]        = "GrantStatus";
constexpr char RADIO_POWER_TYPE_PARAM[]        = "PowerType";
constexpr char RADIO_POSSIBLE_CHANNELS_PARAM[] = "PossibleChannels";

/**
 * @brief Read AFC Available Spectrum Inquiry request/response from platform datamodel.
 *
 * @param[out] request Inquiry request payload from Device.WiFi.AFC.Stats.AvailableSpectrumRequest.
 * @param[out] response Inquiry response payload from Device.WiFi.AFC.Stats.AvailableSpectrumResponse.
 * @return true on success, false otherwise.
 */
bool read_available_spectrum_inquiry_data(std::string &request, std::string &response);

/**
 * @brief Check whether the latest AFC grant completed successfully.
 *
 * Reads Device.WiFi.AFC.Stats.GrantStatus and expects "Success".
 *
 * @return true if GrantStatus is "Success", false otherwise.
 */
bool is_afc_grant_successful();

/**
 * @brief Check whether the given radio operates in 6 GHz Standard Power mode.
 *
 * Reads Device.WiFi.Radio.{i}.PowerType for the radio matching @p radio_iface_name.
 *
 * @param[in] radio_iface_name Fronthaul radio interface name.
 * @return true if PowerType is "StandardPower".
 */
bool is_standard_power_mode(const std::string &radio_iface_name);

/**
 * @brief Parse a comma-separated PossibleChannels DM string into channel indices.
 *
 * Used by read_possible_channels() after reading Device.WiFi.Radio.{i}.PossibleChannels.
 *
 * @param[in] possible_channels_str Comma-separated channel list (e.g. "5,21,37").
 * @param[out] channels Cleared then filled with parsed channel indices.
 * @return true if at least one channel was parsed.
 */
bool parse_possible_channels_list(const std::string &possible_channels_str,
                                  std::unordered_set<uint8_t> &channels);

/**
 * @brief Read AFC-allowed channels from Device.WiFi.Radio.{i}.PossibleChannels.
 *
 * @param[in] radio_iface_name Fronthaul radio interface name.
 * @param[out] channels Cleared then filled with allowed beacon/center channel indices.
 * @return true if at least one channel was parsed.
 */
bool read_possible_channels(const std::string &radio_iface_name,
                            std::unordered_set<uint8_t> &channels);

/**
 * @brief True when AFC regulatory Channel Preference reporting applies on this radio.
 *
 * Requires StandardPower, successful AFC grant, and populated inquiry payloads in DM.
 * Uses a single Ambiorix connection for all DM reads.
 *
 * @param[in] radio_iface_name Fronthaul radio interface name.
 * @return true when regulatory Channel Preference reporting applies (inquiry TLV 0xD / CPQ 0xC).
 */
bool is_afc_regulatory_preference_applicable(const std::string &radio_iface_name);

} // namespace afc_spectrum_helper
} // namespace beerocks

#endif // _AFC_SPECTRUM_HELPER_H_
