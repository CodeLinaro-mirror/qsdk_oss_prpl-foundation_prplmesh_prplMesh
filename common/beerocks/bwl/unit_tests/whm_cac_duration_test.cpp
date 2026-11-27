/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../whm/utils_wlan_hal_whm.h"
#include <bwl/key_value_parser.h>
#include <gtest/gtest.h>

namespace {

class TestKeyValueParser : public bwl::KeyValueParser {
public:
    using bwl::KeyValueParser::parse_event;
};

// Use the real key/value parser before the HAL's duration conversion: vendor
// fields without '=' remain part of cac_time, as in Freedom's foreground event.
// cppcheck-suppress syntaxError
TEST(whm_cac_duration, hostapd_event_formats)
{
    struct TestCase {
        const char *event;
        uint16_t duration;
    };
    const TestCase cases[] = {
        {"<3>DFS-CAC-START freq=5320 chan=64 sec_chan=-1, width=0, seg0=62, seg1=0, "
         "cac_time=60s bitmap:0x0000",
         60},
        {"<3>DFS-CAC-START freq=5320 chan=64 chan_offset=-1 width=2 seg0=5310 seg1=0 "
         "cac_time=60s chan_width_device=0 cf_device=0 (background)",
         60},
        {"<3>DFS-CAC-START freq=5300 chan=60 sec_chan=1, width=0, seg0=62, seg1=0, "
         "cac_time=60s",
         60},
        {"<3>DFS-CAC-START freq=5600 chan=120 cac_time=600s bitmap:0x0000", 600},
        {"<3>DFS-CAC-START freq=5320 chan=64 cac_time=60", 60},
        {"<3>DFS-CAC-START freq=5320 chan=64 cac_time=65535s bitmap:0x0000", 65535},
    };

    TestKeyValueParser parser;
    for (const auto &test : cases) {
        SCOPED_TRACE(test.event);
        bwl::parsed_line_t parsed;
        parser.parse_event(test.event, parsed);
        ASSERT_EQ(parsed.count("cac_time"), 1U);
        EXPECT_EQ(bwl::whm::utils_wlan_hal_whm::cac_duration_from_string(parsed.at("cac_time")),
                  test.duration);
    }
}

// cppcheck-suppress syntaxError
TEST(whm_cac_duration, invalid_duration_does_not_wrap)
{
    for (const auto *value : {"", "unknown", "-1s", "65536s"}) {
        SCOPED_TRACE(value);
        EXPECT_EQ(bwl::whm::utils_wlan_hal_whm::cac_duration_from_string(value), 0);
    }
}

} // namespace
