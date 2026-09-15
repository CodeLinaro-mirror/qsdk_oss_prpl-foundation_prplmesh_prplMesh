/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../agent_db.h"

#include <bcl/network/network_utils.h>

#include <gtest/gtest.h>

namespace beerocks {
namespace {

using Configuration = AgentDB::sAPMLDConfiguration;
using Affiliate     = Configuration::sAffiliatedAP;

const auto mld_mac                = tlvf::mac_from_string("02:aa:bb:cc:dd:ee");
const std::vector<sMacAddr> ruids = {
    tlvf::mac_from_string("00:11:22:33:44:01"),
    tlvf::mac_from_string("00:11:22:33:44:02"),
    tlvf::mac_from_string("00:11:22:33:44:03"),
};

sMacAddr bssid(size_t index)
{
    return tlvf::mac_from_string("02:11:22:33:44:0" + std::to_string(index + 1));
}

Configuration configuration(const std::vector<size_t> &indices, int8_t unit = 2,
                            eMLOModes mode = MLO_MODE_STR)
{
    Configuration result;
    result.mld_config.mld_ssid = "mlo-ssid";
    result.mld_config.mld_mac  = mld_mac;
    result.mld_config.mld_unit = unit;
    result.mld_config.mld_mode = mode;
    for (const auto index : indices) {
        Affiliate affiliate;
        affiliate.ruid    = ruids.at(index);
        affiliate.bssid   = bssid(index);
        affiliate.link_id = static_cast<int8_t>(index);
        result.affiliated_aps.push_back(affiliate);
    }
    return result;
}

void clear_reported_fields(Configuration &configuration)
{
    configuration.mld_config.mld_mac = net::network_utils::ZERO_MAC;
    for (auto &affiliate : configuration.affiliated_aps) {
        affiliate.bssid   = net::network_utils::ZERO_MAC;
        affiliate.link_id = DISABLED_MLDUNIT;
    }
}

void expect_unknown(const Affiliate &affiliate)
{
    EXPECT_EQ(affiliate.bssid, net::network_utils::ZERO_MAC);
    EXPECT_EQ(affiliate.link_id, DISABLED_MLDUNIT);
}

// Suppress cppcheck syntax error for gtest TEST macro.
// cppcheck-suppress syntaxError
TEST(AffiliatedAPRetentionTest, defaults_and_all_valid_link_ids_are_safe)
{
    Affiliate affiliate{};
    EXPECT_EQ(affiliate.ruid, net::network_utils::ZERO_MAC);
    EXPECT_EQ(affiliate.bssid, net::network_utils::ZERO_MAC);
    EXPECT_EQ(affiliate.link_id, DISABLED_MLDUNIT);

    for (int id = 0; id <= 15; ++id) {
        affiliate.link_id = static_cast<int8_t>(id);
        EXPECT_TRUE(affiliate.has_valid_link_id());

        auto previous                      = configuration({0, 1});
        previous.affiliated_aps[0].link_id = static_cast<int8_t>(id);
        auto rebuilt                       = configuration({0, 1});
        clear_reported_fields(rebuilt);
        rebuilt.retain_unchanged_links(previous, ruids[1]);
        EXPECT_EQ(rebuilt.affiliated_aps[0].link_id, id);
    }

    for (const int id : {-1, 16, 85}) {
        affiliate.link_id = static_cast<int8_t>(id);
        EXPECT_FALSE(affiliate.has_valid_link_id());
    }
}

TEST(AffiliatedAPRetentionTest, three_radio_rebuild_preserves_only_confirmed_other_radios)
{
    const auto previous = configuration({0, 1, 2});
    auto rebuilt        = configuration({0, 1, 2});
    clear_reported_fields(rebuilt);
    rebuilt.retain_unchanged_links(previous, ruids[2]);

    EXPECT_EQ(rebuilt.mld_config.mld_mac, mld_mac);
    EXPECT_EQ(rebuilt.affiliated_aps[0].link_id, 0);
    EXPECT_EQ(rebuilt.affiliated_aps[1].link_id, 1);
    expect_unknown(rebuilt.affiliated_aps[2]);
}

TEST(AffiliatedAPRetentionTest, link_zero_and_pending_unknown_are_handled)
{
    const auto previous = configuration({0, 1, 2});
    auto first          = configuration({0, 1, 2});
    clear_reported_fields(first);
    first.retain_unchanged_links(previous, ruids[1]);
    EXPECT_EQ(first.affiliated_aps[0].link_id, 0);
    expect_unknown(first.affiliated_aps[1]);
    EXPECT_EQ(first.affiliated_aps[2].link_id, 2);

    auto second = configuration({0, 1, 2});
    clear_reported_fields(second);
    second.retain_unchanged_links(first, ruids[2]);
    EXPECT_EQ(second.affiliated_aps[0].link_id, 0);
    expect_unknown(second.affiliated_aps[1]);
    expect_unknown(second.affiliated_aps[2]);
}

TEST(AffiliatedAPRetentionTest, later_notification_survives_the_next_m2_rebuild)
{
    auto after_notification = configuration({0, 1, 2});
    clear_reported_fields(after_notification);
    after_notification.mld_config.mld_mac        = mld_mac;
    after_notification.affiliated_aps[1].bssid   = bssid(1);
    after_notification.affiliated_aps[1].link_id = 1;

    auto rebuilt = configuration({0, 1, 2});
    clear_reported_fields(rebuilt);
    rebuilt.retain_unchanged_links(after_notification, ruids[2]);
    EXPECT_EQ(rebuilt.affiliated_aps[1].bssid, bssid(1));
    EXPECT_EQ(rebuilt.affiliated_aps[1].link_id, 1);
    expect_unknown(rebuilt.affiliated_aps[2]);
}

TEST(AffiliatedAPRetentionTest, reordered_membership_matches_by_ruids)
{
    const auto previous = configuration({0, 1, 2});
    auto rebuilt        = configuration({2, 0, 1});
    clear_reported_fields(rebuilt);
    rebuilt.retain_unchanged_links(previous, ruids[1]);

    EXPECT_EQ(rebuilt.affiliated_aps[0].ruid, ruids[2]);
    EXPECT_EQ(rebuilt.affiliated_aps[0].bssid, bssid(2));
    EXPECT_EQ(rebuilt.affiliated_aps[1].bssid, bssid(0));
    expect_unknown(rebuilt.affiliated_aps[2]);
}

TEST(AffiliatedAPRetentionTest, descriptor_changes_do_not_retain_fields)
{
    const auto previous     = configuration({0, 1, 2});
    const auto current_ruid = ruids[2];

    auto changed_ssid                = configuration({0, 1, 2});
    changed_ssid.mld_config.mld_ssid = "different";
    auto changed_unit                = configuration({0, 1, 2}, 3);
    auto disabled_unit               = configuration({0, 1, 2}, DISABLED_MLDUNIT);
    auto changed_mode                = configuration({0, 1, 2}, 2, MLO_MODE_NSTR);
    auto removed_link                = configuration({0, 1});
    auto added_link                  = configuration({0, 1, 2});
    added_link.affiliated_aps.push_back({{}, ruids[0], bssid(0), 0});
    auto replaced_link                   = configuration({0, 1, 2});
    replaced_link.affiliated_aps[2].ruid = tlvf::mac_from_string("00:11:22:33:44:09");

    for (auto *changed : {&changed_ssid, &changed_unit, &disabled_unit, &changed_mode,
                          &removed_link, &added_link, &replaced_link}) {
        clear_reported_fields(*changed);
        changed->retain_unchanged_links(previous, current_ruid);
        EXPECT_EQ(changed->mld_config.mld_mac, net::network_utils::ZERO_MAC);
        for (const auto &affiliate : changed->affiliated_aps) {
            expect_unknown(affiliate);
        }
    }
}

TEST(AffiliatedAPRetentionTest, missing_current_context_disables_retention)
{
    const auto previous   = configuration({0, 1, 2});
    auto explicit_request = configuration({0, 1, 2});
    clear_reported_fields(explicit_request);
    explicit_request.retain_unchanged_links(previous, net::network_utils::ZERO_MAC);

    EXPECT_EQ(explicit_request.mld_config.mld_mac, net::network_utils::ZERO_MAC);
    for (const auto &affiliate : explicit_request.affiliated_aps) {
        expect_unknown(affiliate);
    }
}

TEST(AffiliatedAPRetentionTest, duplicate_or_zero_ruids_reject_descriptor)
{
    const auto previous              = configuration({0, 1, 2});
    auto duplicate                   = configuration({0, 1, 2});
    duplicate.affiliated_aps[2].ruid = duplicate.affiliated_aps[1].ruid;
    clear_reported_fields(duplicate);
    duplicate.retain_unchanged_links(previous, ruids[2]);
    EXPECT_EQ(duplicate.mld_config.mld_mac, net::network_utils::ZERO_MAC);

    auto zero                   = configuration({0, 1, 2});
    zero.affiliated_aps[1].ruid = net::network_utils::ZERO_MAC;
    clear_reported_fields(zero);
    zero.retain_unchanged_links(previous, ruids[2]);
    EXPECT_EQ(zero.mld_config.mld_mac, net::network_utils::ZERO_MAC);

    auto malformed_previous                   = configuration({0, 1, 2});
    malformed_previous.affiliated_aps[2].ruid = malformed_previous.affiliated_aps[1].ruid;
    auto rebuilt                              = configuration({0, 1, 2});
    clear_reported_fields(rebuilt);
    rebuilt.retain_unchanged_links(malformed_previous, ruids[2]);
    EXPECT_EQ(rebuilt.mld_config.mld_mac, net::network_utils::ZERO_MAC);
}

TEST(AffiliatedAPRetentionTest, invalid_previous_fields_leave_mld_unknown)
{
    auto previous                      = configuration({0, 1, 2});
    previous.affiliated_aps[0].bssid   = net::network_utils::ZERO_MAC;
    previous.affiliated_aps[1].link_id = 85;
    auto rebuilt                       = configuration({0, 1, 2});
    clear_reported_fields(rebuilt);
    rebuilt.retain_unchanged_links(previous, ruids[2]);

    expect_unknown(rebuilt.affiliated_aps[0]);
    expect_unknown(rebuilt.affiliated_aps[1]);
    expect_unknown(rebuilt.affiliated_aps[2]);
    EXPECT_EQ(rebuilt.mld_config.mld_mac, net::network_utils::ZERO_MAC);
}

TEST(AffiliatedAPRetentionTest, unknown_previous_mld_mac_prevents_retention)
{
    auto previous               = configuration({0, 1, 2});
    previous.mld_config.mld_mac = net::network_utils::ZERO_MAC;
    auto rebuilt                = configuration({0, 1, 2});
    clear_reported_fields(rebuilt);
    rebuilt.retain_unchanged_links(previous, ruids[2]);
    EXPECT_EQ(rebuilt.mld_config.mld_mac, net::network_utils::ZERO_MAC);
    for (const auto &affiliate : rebuilt.affiliated_aps) {
        expect_unknown(affiliate);
    }
}

} // namespace
} // namespace beerocks
