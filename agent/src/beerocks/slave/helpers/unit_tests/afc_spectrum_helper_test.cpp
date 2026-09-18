/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../afc_spectrum_helper.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_set>

namespace {

int g_fails = 0;

bool check(bool cond, const char *msg)
{
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        ++g_fails;
    }
    return cond;
}

using beerocks::afc_spectrum_helper::parse_possible_channels_list;

void parse_list_basic()
{
    std::unordered_set<uint8_t> channels;
    check(parse_possible_channels_list("5,21,37,7", channels), "parse 5,21,37,7");
    check(channels.size() == 4, "size is 4");
    check(channels.count(5) != 0, "has channel 5");
    check(channels.count(21) != 0, "has channel 21");
    check(channels.count(37) != 0, "has channel 37");
    check(channels.count(7) != 0, "has channel 7");
}

void parse_list_skips_empty_tokens()
{
    std::unordered_set<uint8_t> channels;
    check(parse_possible_channels_list("5,,21,", channels), "parse 5,,21,");
    check(channels.size() == 2, "size is 2");
    check(channels.count(5) != 0, "has channel 5");
    check(channels.count(21) != 0, "has channel 21");
}

void parse_list_rejects_empty()
{
    std::unordered_set<uint8_t> channels;
    check(!parse_possible_channels_list("", channels), "empty string is rejected");
    check(channels.empty(), "empty string clears output");
    check(!parse_possible_channels_list(",,", channels), "comma-only is rejected");
    check(channels.empty(), "comma-only clears output");
}

void parse_list_clears_previous_contents()
{
    std::unordered_set<uint8_t> channels;
    channels.insert(1);
    channels.insert(2);
    channels.insert(3);
    check(parse_possible_channels_list("9", channels), "parse 9");
    check(channels.size() == 1, "previous contents cleared");
    check(channels.count(9) != 0, "has channel 9");
}

void dm_path_constants()
{
    check(std::string(beerocks::afc_spectrum_helper::AFC_STATS_PATH) == "Device.WiFi.AFC.Stats.",
          "AFC_STATS_PATH");
    check(std::string(beerocks::afc_spectrum_helper::AFC_REQUEST_PARAM) ==
              "AvailableSpectrumRequest",
          "AFC_REQUEST_PARAM");
    check(std::string(beerocks::afc_spectrum_helper::AFC_RESPONSE_PARAM) ==
              "AvailableSpectrumResponse",
          "AFC_RESPONSE_PARAM");
    check(std::string(beerocks::afc_spectrum_helper::AFC_GRANT_STATUS_PARAM) == "GrantStatus",
          "AFC_GRANT_STATUS_PARAM");
    check(std::string(beerocks::afc_spectrum_helper::RADIO_POWER_TYPE_PARAM) == "PowerType",
          "RADIO_POWER_TYPE_PARAM");
    check(std::string(beerocks::afc_spectrum_helper::RADIO_POSSIBLE_CHANNELS_PARAM) ==
              "PossibleChannels",
          "RADIO_POSSIBLE_CHANNELS_PARAM");
}

} // namespace

int main()
{
    parse_list_basic();
    parse_list_skips_empty_tokens();
    parse_list_rejects_empty();
    parse_list_clears_previous_contents();
    dm_path_constants();
    return g_fails == 0 ? 0 : 1;
}
