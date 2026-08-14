/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <gtest/gtest.h>

#include "../whm/ap_wlan_hal_whm_afc_utils.h"

namespace bwl {
namespace tests {

using bwl::whm::afc_utils::channels_set_to_string;
using bwl::whm::afc_utils::parse_afc_update_inquiry_status;
using bwl::whm::afc_utils::parse_possible_channels_list;

namespace {

/**
 * @brief Access AmbiorixVariant's underlying amxc_var_t for test fixture construction.
 *
 * AmbiorixVariant::add_child() cannot nest AmbiorixVariant values (set() overload is ambiguous),
 * so nested AFC event payloads are built with amxc_var_add_key().
 */
class AmbiorixVariantAccess : public beerocks::wbapi::AmbiorixVariantBaseAccess {
public:
    using AmbiorixVariantBaseAccess::get_amxc_var_ptr;
};

} // namespace

TEST(ap_wlan_hal_whm_afc, parse_possible_channels_list_basic)
{
    std::unordered_set<uint8_t> channels;
    ASSERT_TRUE(parse_possible_channels_list("5,21,37,7", channels));
    ASSERT_EQ(channels.size(), 4U);
    EXPECT_NE(channels.count(5), 0U);
    EXPECT_NE(channels.count(21), 0U);
    EXPECT_NE(channels.count(37), 0U);
    EXPECT_NE(channels.count(7), 0U);
}

TEST(ap_wlan_hal_whm_afc, parse_possible_channels_list_skips_empty_tokens)
{
    std::unordered_set<uint8_t> channels;
    ASSERT_TRUE(parse_possible_channels_list("5,,21,", channels));
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_NE(channels.count(5), 0U);
    EXPECT_NE(channels.count(21), 0U);
}

TEST(ap_wlan_hal_whm_afc, parse_possible_channels_list_rejects_empty)
{
    std::unordered_set<uint8_t> channels;
    EXPECT_FALSE(parse_possible_channels_list("", channels));
    EXPECT_TRUE(channels.empty());
    EXPECT_FALSE(parse_possible_channels_list(",,", channels));
    EXPECT_TRUE(channels.empty());
}

TEST(ap_wlan_hal_whm_afc, channels_set_to_string_round_trip)
{
    std::unordered_set<uint8_t> channels;
    ASSERT_TRUE(parse_possible_channels_list("5,21,37", channels));

    std::unordered_set<uint8_t> round_trip;
    ASSERT_TRUE(parse_possible_channels_list(channels_set_to_string(channels), round_trip));
    EXPECT_EQ(round_trip, channels);
}

TEST(ap_wlan_hal_whm_afc, dm_path_constants)
{
    EXPECT_STREQ(bwl::whm::afc_utils::AFC_STATS_PATH, "Device.WiFi.AFC.Stats.");
    EXPECT_STREQ(bwl::whm::afc_utils::AFC_REQUEST_PARAM, "AvailableSpectrumRequest");
    EXPECT_STREQ(bwl::whm::afc_utils::AFC_RESPONSE_PARAM, "AvailableSpectrumResponse");
    EXPECT_STREQ(bwl::whm::afc_utils::AFC_GRANT_STATUS_PARAM, "GrantStatus");
    EXPECT_STREQ(bwl::whm::afc_utils::RADIO_POWER_TYPE_PARAM, "PowerType");
    EXPECT_STREQ(bwl::whm::afc_utils::RADIO_POSSIBLE_CHANNELS_PARAM, "PossibleChannels");
}

TEST(ap_wlan_hal_whm_afc, parse_afc_update_inquiry_status_update)
{
    beerocks::wbapi::AmbiorixVariant event_data(AMXC_VAR_ID_HTABLE);
    AmbiorixVariantAccess access;
    amxc_var_t *root = access.get_amxc_var_ptr(event_data);
    ASSERT_NE(root, nullptr);

    amxc_var_t *updates = amxc_var_add_key(amxc_htable_t, root, "Updates", NULL);
    ASSERT_NE(updates, nullptr);
    ASSERT_NE(amxc_var_add_key(cstring_t, updates, "InquiryStatus", "UPDATE"), nullptr);

    std::string inquiry_status;
    ASSERT_TRUE(parse_afc_update_inquiry_status(&event_data, inquiry_status));
    EXPECT_EQ(inquiry_status, "UPDATE");
}

TEST(ap_wlan_hal_whm_afc, parse_afc_update_inquiry_status_rejects_missing_updates)
{
    beerocks::wbapi::AmbiorixVariant event_data(AMXC_VAR_ID_HTABLE);
    std::string inquiry_status;
    EXPECT_FALSE(parse_afc_update_inquiry_status(&event_data, inquiry_status));
    EXPECT_TRUE(inquiry_status.empty());
}

TEST(ap_wlan_hal_whm_afc, parse_afc_update_inquiry_status_rejects_null_event)
{
    std::string inquiry_status;
    EXPECT_FALSE(parse_afc_update_inquiry_status(nullptr, inquiry_status));
}

} // namespace tests
} // namespace bwl
