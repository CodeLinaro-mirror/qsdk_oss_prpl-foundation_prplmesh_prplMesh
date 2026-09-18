/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../afc_spectrum_parser.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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

/* Build JSON with \x22 instead of \", which confuses cppcheck 2.4. */
std::string quoted(const std::string &key) { return std::string("\x22") + key + "\x22"; }

std::string escaped_quoted(const std::string &key)
{
    return std::string("\\\x22") + key + "\\\x22";
}

son::afc_spectrum_parser::sAvailableChannel make_channel(uint8_t op_class, uint8_t channel,
                                                         int8_t max_eirp_dbm, bool max_eirp_valid)
{
    son::afc_spectrum_parser::sAvailableChannel entry;
    entry.operating_class = op_class;
    entry.channel         = channel;
    entry.max_eirp_dbm    = max_eirp_dbm;
    entry.max_eirp_valid  = max_eirp_valid;
    return entry;
}

void parses_available_channel_info_and_max_eirp()
{
    const std::string response_json =
        std::string("{") + quoted("availableSpectrumInquiryResponses") + ":[{" +
        quoted("availableChannelInfo") + ":[{" + quoted("globalOperatingClass") + ":131," +
        quoted("channelCfi") + ":[5,21,37]," + quoted("maxEirp") + ":[36,30.5,24]},{" +
        quoted("globalOperatingClass") + ":133," + quoted("channelCfi") + ":[7]," +
        quoted("maxEirp") + ":[20]}]}]}";

    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    if (!check(son::afc_spectrum_parser::parse_available_channel_info(response_json, channels),
               "parse availableChannelInfo")) {
        return;
    }
    if (!check(channels.size() == 4, "parsed 4 channels")) {
        return;
    }

    check(channels[0].operating_class == 131, "ch0 op class");
    check(channels[0].channel == 5, "ch0 channel");
    check(channels[0].max_eirp_valid, "ch0 eirp valid");
    check(channels[0].max_eirp_dbm == 36, "ch0 eirp");

    check(channels[1].operating_class == 131, "ch1 op class");
    check(channels[1].channel == 21, "ch1 channel");
    check(channels[1].max_eirp_valid, "ch1 eirp valid");
    check(channels[1].max_eirp_dbm == 30, "ch1 eirp truncated from 30.5");

    check(channels[2].operating_class == 131, "ch2 op class");
    check(channels[2].channel == 37, "ch2 channel");
    check(channels[2].max_eirp_dbm == 24, "ch2 eirp");

    check(channels[3].operating_class == 133, "ch3 op class");
    check(channels[3].channel == 7, "ch3 channel");
    check(channels[3].max_eirp_dbm == 20, "ch3 eirp");
}

void parses_escaped_json_payload()
{
    const std::string escaped = std::string("{") + escaped_quoted("availableChannelInfo") + ":[{" +
                                escaped_quoted("globalOperatingClass") + ":131," +
                                escaped_quoted("channelCfi") + ":[5]," + escaped_quoted("maxEirp") +
                                ":[36]}]}";

    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    if (!check(son::afc_spectrum_parser::parse_available_channel_info(escaped, channels),
               "parse escaped JSON")) {
        return;
    }
    if (!check(channels.size() == 1, "escaped JSON yields 1 channel")) {
        return;
    }
    check(channels[0].operating_class == 131, "escaped op class");
    check(channels[0].channel == 5, "escaped channel");
    check(channels[0].max_eirp_dbm == 36, "escaped eirp");
}

void rejects_empty_or_missing_channel_info()
{
    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    check(!son::afc_spectrum_parser::parse_available_channel_info("", channels),
          "empty JSON rejected");
    check(channels.empty(), "empty JSON clears output");

    const std::string no_info = std::string("{") + quoted("responseCode") + ":0}";
    check(!son::afc_spectrum_parser::parse_available_channel_info(no_info, channels),
          "missing availableChannelInfo rejected");
    check(channels.empty(), "missing availableChannelInfo clears output");
}

void select_transmit_power_limit_prefers_requested_channel()
{
    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    channels.push_back(make_channel(131, 5, 36, true));
    channels.push_back(make_channel(131, 21, 24, true));
    channels.push_back(make_channel(131, 37, 30, true));

    int8_t limit_dbm = 0;
    check(son::afc_spectrum_parser::select_transmit_power_limit_dbm(channels, 21, limit_dbm),
          "select preferred channel 21");
    check(limit_dbm == 24, "preferred channel eirp 24");
}

void select_transmit_power_limit_falls_back_to_minimum()
{
    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    channels.push_back(make_channel(131, 5, 36, true));
    channels.push_back(make_channel(131, 21, 24, true));
    channels.push_back(make_channel(131, 37, 30, true));
    channels.push_back(make_channel(133, 7, 18, false));

    int8_t limit_dbm = 0;
    check(son::afc_spectrum_parser::select_transmit_power_limit_dbm(channels, 0, limit_dbm),
          "select minimum eirp");
    check(limit_dbm == 24, "minimum valid eirp 24");

    std::vector<son::afc_spectrum_parser::sAvailableChannel> empty;
    check(!son::afc_spectrum_parser::select_transmit_power_limit_dbm(empty, 5, limit_dbm),
          "empty list rejected");
}

void parses_channels_without_max_eirp()
{
    const std::string response_json = std::string("{") + quoted("availableChannelInfo") + ":[{" +
                                      quoted("globalOperatingClass") + ":131," +
                                      quoted("channelCfi") + ":[1,5]}]}";

    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    if (!check(son::afc_spectrum_parser::parse_available_channel_info(response_json, channels),
               "parse without maxEirp")) {
        return;
    }
    if (!check(channels.size() == 2, "2 channels without maxEirp")) {
        return;
    }
    check(!channels[0].max_eirp_valid, "ch0 eirp invalid");
    check(!channels[1].max_eirp_valid, "ch1 eirp invalid");

    int8_t limit_dbm = 0;
    check(!son::afc_spectrum_parser::select_transmit_power_limit_dbm(channels, 5, limit_dbm),
          "no valid eirp to select");
}

void parses_when_max_eirp_shorter_than_channel_list()
{
    const std::string response_json =
        std::string("{") + quoted("availableChannelInfo") + ":[{" + quoted("globalOperatingClass") +
        ":131," + quoted("channelCfi") + ":[5,21,37]," + quoted("maxEirp") + ":[36]}]}";

    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    if (!check(son::afc_spectrum_parser::parse_available_channel_info(response_json, channels),
               "parse short maxEirp")) {
        return;
    }
    if (!check(channels.size() == 3, "3 channels with short maxEirp")) {
        return;
    }
    check(channels[0].max_eirp_valid, "first eirp present");
    check(channels[0].max_eirp_dbm == 36, "first eirp 36");
    check(!channels[1].max_eirp_valid, "second eirp missing");
    check(!channels[2].max_eirp_valid, "third eirp missing");
}

void select_transmit_power_limit_ignores_invalid_preferred_channel()
{
    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    channels.push_back(make_channel(131, 5, 36, true));
    channels.push_back(make_channel(131, 21, 24, true));

    int8_t limit_dbm = 0;
    check(son::afc_spectrum_parser::select_transmit_power_limit_dbm(channels, 99, limit_dbm),
          "missing preferred channel falls back");
    check(limit_dbm == 24, "fallback eirp 24");
}

} // namespace

int main()
{
    parses_available_channel_info_and_max_eirp();
    parses_escaped_json_payload();
    rejects_empty_or_missing_channel_info();
    select_transmit_power_limit_prefers_requested_channel();
    select_transmit_power_limit_falls_back_to_minimum();
    parses_channels_without_max_eirp();
    parses_when_max_eirp_shorter_than_channel_list();
    select_transmit_power_limit_ignores_invalid_preferred_channel();
    return g_fails == 0 ? 0 : 1;
}
