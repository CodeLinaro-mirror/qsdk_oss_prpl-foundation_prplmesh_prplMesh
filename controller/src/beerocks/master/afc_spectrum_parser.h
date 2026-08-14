/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _AFC_SPECTRUM_PARSER_H_
#define _AFC_SPECTRUM_PARSER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace son {
namespace afc_spectrum_parser {

struct sAvailableChannel {
    uint8_t operating_class = 0;
    uint8_t channel         = 0;
    int8_t max_eirp_dbm     = 0;
    bool max_eirp_valid     = false;
};

/**
 * @brief Parse AFC AvailableSpectrumInquiryResponse JSON for available channels / maxEirp.
 *
 * Extracts entries from availableChannelInfo[] (globalOperatingClass, channelCfi[], maxEirp[]).
 * Does not require a full JSON library; targeted for WinnForum AFC S2D response shape.
 *
 * @param[in] response_json Opaque JSON string from Available Spectrum Inquiry Response TLV.
 * @param[out] channels Parsed available channels (may be empty on parse failure).
 * @return true if at least one available channel was parsed, false otherwise.
 */
bool parse_available_channel_info(const std::string &response_json,
                                  std::vector<sAvailableChannel> &channels);

/**
 * @brief Choose a Transmit Power Limit (dBm) from parsed AFC channel info.
 *
 * Prefers maxEirp of @p preferred_channel when present; otherwise uses the minimum
 * maxEirp among entries (conservative for §8.2.5 power enforcement).
 *
 * @return true if a limit was selected.
 */
bool select_transmit_power_limit_dbm(const std::vector<sAvailableChannel> &channels,
                                     uint8_t preferred_channel, int8_t &limit_dbm);

} // namespace afc_spectrum_parser
} // namespace son

#endif // _AFC_SPECTRUM_PARSER_H_
