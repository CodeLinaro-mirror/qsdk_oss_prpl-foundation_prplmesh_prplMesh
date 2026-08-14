/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../afc_spectrum_parser.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

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

} // namespace

namespace son {
namespace tests {

// cppcheck-suppress syntaxError
TEST(afc_spectrum_parser, parses_available_channel_info_and_max_eirp)
{
    const std::string response_json =
        std::string("{") + quoted("availableSpectrumInquiryResponses") + ":[{" +
        quoted("availableChannelInfo") + ":[{" + quoted("globalOperatingClass") + ":131," +
        quoted("channelCfi") + ":[5,21,37]," + quoted("maxEirp") + ":[36,30.5,24]},{" +
        quoted("globalOperatingClass") + ":133," + quoted("channelCfi") + ":[7]," +
        quoted("maxEirp") + ":[20]}]}]}";

    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    ASSERT_TRUE(son::afc_spectrum_parser::parse_available_channel_info(response_json, channels));
    ASSERT_EQ(channels.size(), 4U);

    EXPECT_EQ(channels[0].operating_class, 131);
    EXPECT_EQ(channels[0].channel, 5);
    EXPECT_TRUE(channels[0].max_eirp_valid);
    EXPECT_EQ(channels[0].max_eirp_dbm, 36);

    EXPECT_EQ(channels[1].operating_class, 131);
    EXPECT_EQ(channels[1].channel, 21);
    EXPECT_TRUE(channels[1].max_eirp_valid);
    EXPECT_EQ(channels[1].max_eirp_dbm, 30);

    EXPECT_EQ(channels[2].operating_class, 131);
    EXPECT_EQ(channels[2].channel, 37);
    EXPECT_EQ(channels[2].max_eirp_dbm, 24);

    EXPECT_EQ(channels[3].operating_class, 133);
    EXPECT_EQ(channels[3].channel, 7);
    EXPECT_EQ(channels[3].max_eirp_dbm, 20);
}

TEST(afc_spectrum_parser, parses_escaped_json_payload)
{
    const std::string escaped = std::string("{") + escaped_quoted("availableChannelInfo") + ":[{" +
                                escaped_quoted("globalOperatingClass") + ":131," +
                                escaped_quoted("channelCfi") + ":[5]," + escaped_quoted("maxEirp") +
                                ":[36]}]}";

    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    ASSERT_TRUE(son::afc_spectrum_parser::parse_available_channel_info(escaped, channels));
    ASSERT_EQ(channels.size(), 1U);
    EXPECT_EQ(channels[0].operating_class, 131);
    EXPECT_EQ(channels[0].channel, 5);
    EXPECT_EQ(channels[0].max_eirp_dbm, 36);
}

TEST(afc_spectrum_parser, rejects_empty_or_missing_channel_info)
{
    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    EXPECT_FALSE(son::afc_spectrum_parser::parse_available_channel_info("", channels));
    EXPECT_TRUE(channels.empty());

    const std::string no_info = std::string("{") + quoted("responseCode") + ":0}";
    EXPECT_FALSE(son::afc_spectrum_parser::parse_available_channel_info(no_info, channels));
    EXPECT_TRUE(channels.empty());
}

TEST(afc_spectrum_parser, select_transmit_power_limit_prefers_requested_channel)
{
    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    channels.push_back(make_channel(131, 5, 36, true));
    channels.push_back(make_channel(131, 21, 24, true));
    channels.push_back(make_channel(131, 37, 30, true));

    int8_t limit_dbm = 0;
    ASSERT_TRUE(son::afc_spectrum_parser::select_transmit_power_limit_dbm(channels, 21, limit_dbm));
    EXPECT_EQ(limit_dbm, 24);
}

TEST(afc_spectrum_parser, select_transmit_power_limit_falls_back_to_minimum)
{
    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    channels.push_back(make_channel(131, 5, 36, true));
    channels.push_back(make_channel(131, 21, 24, true));
    channels.push_back(make_channel(131, 37, 30, true));
    channels.push_back(make_channel(133, 7, 18, false));

    int8_t limit_dbm = 0;
    ASSERT_TRUE(son::afc_spectrum_parser::select_transmit_power_limit_dbm(channels, 0, limit_dbm));
    EXPECT_EQ(limit_dbm, 24);

    std::vector<son::afc_spectrum_parser::sAvailableChannel> empty;
    EXPECT_FALSE(son::afc_spectrum_parser::select_transmit_power_limit_dbm(empty, 5, limit_dbm));
}

TEST(afc_spectrum_parser, parses_channels_without_max_eirp)
{
    const std::string response_json = std::string("{") + quoted("availableChannelInfo") + ":[{" +
                                      quoted("globalOperatingClass") + ":131," +
                                      quoted("channelCfi") + ":[1,5]}]}";

    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    ASSERT_TRUE(son::afc_spectrum_parser::parse_available_channel_info(response_json, channels));
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_FALSE(channels[0].max_eirp_valid);
    EXPECT_FALSE(channels[1].max_eirp_valid);

    int8_t limit_dbm = 0;
    EXPECT_FALSE(son::afc_spectrum_parser::select_transmit_power_limit_dbm(channels, 5, limit_dbm));
}

TEST(afc_spectrum_parser, parses_when_max_eirp_shorter_than_channel_list)
{
    const std::string response_json =
        std::string("{") + quoted("availableChannelInfo") + ":[{" + quoted("globalOperatingClass") +
        ":131," + quoted("channelCfi") + ":[5,21,37]," + quoted("maxEirp") + ":[36]}]}";

    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    ASSERT_TRUE(son::afc_spectrum_parser::parse_available_channel_info(response_json, channels));
    ASSERT_EQ(channels.size(), 3U);
    EXPECT_TRUE(channels[0].max_eirp_valid);
    EXPECT_EQ(channels[0].max_eirp_dbm, 36);
    EXPECT_FALSE(channels[1].max_eirp_valid);
    EXPECT_FALSE(channels[2].max_eirp_valid);
}

TEST(afc_spectrum_parser, select_transmit_power_limit_ignores_invalid_preferred_channel)
{
    std::vector<son::afc_spectrum_parser::sAvailableChannel> channels;
    channels.push_back(make_channel(131, 5, 36, true));
    channels.push_back(make_channel(131, 21, 24, true));

    int8_t limit_dbm = 0;
    ASSERT_TRUE(son::afc_spectrum_parser::select_transmit_power_limit_dbm(channels, 99, limit_dbm));
    EXPECT_EQ(limit_dbm, 24);
}

} // namespace tests
} // namespace son
