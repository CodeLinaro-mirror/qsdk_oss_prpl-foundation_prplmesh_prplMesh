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
    }

    for (const int id : {-1, 16, 85}) {
        affiliate.link_id = static_cast<int8_t>(id);
        EXPECT_FALSE(affiliate.has_valid_link_id());
    }
}

TEST(AffiliatedAPRetentionTest, sequential_m2_preserves_confirmed_links_and_pending_unknowns)
{
    const auto previous = configuration({0, 1, 2});
    auto first          = configuration({0, 1, 2});
    clear_reported_fields(first);
    first.retain_unchanged_links(previous, ruids[1]);
    EXPECT_EQ(first.mld_config.mld_mac, mld_mac);
    EXPECT_EQ(first.affiliated_aps[0].bssid, bssid(0));
    EXPECT_EQ(first.affiliated_aps[0].link_id, 0);
    expect_unknown(first.affiliated_aps[1]);
    EXPECT_EQ(first.affiliated_aps[2].link_id, 2);

    auto second = configuration({0, 1, 2});
    clear_reported_fields(second);
    second.retain_unchanged_links(first, ruids[2]);
    EXPECT_EQ(second.affiliated_aps[0].link_id, 0);
    expect_unknown(second.affiliated_aps[1]);
    expect_unknown(second.affiliated_aps[2]);

    // A delayed notification confirms one pending radio before the next M2.
    second.affiliated_aps[1].bssid   = bssid(1);
    second.affiliated_aps[1].link_id = 1;
    auto third                       = configuration({0, 1, 2});
    clear_reported_fields(third);
    third.retain_unchanged_links(second, ruids[0]);
    EXPECT_EQ(third.mld_config.mld_mac, mld_mac);
    expect_unknown(third.affiliated_aps[0]);
    EXPECT_EQ(third.affiliated_aps[1].bssid, bssid(1));
    EXPECT_EQ(third.affiliated_aps[1].link_id, 1);
    expect_unknown(third.affiliated_aps[2]);
}

TEST(AffiliatedAPRetentionTest, changed_descriptor_or_explicit_request_does_not_retain_fields)
{
    const auto previous     = configuration({0, 1, 2});
    const auto current_ruid = ruids[2];

    auto changed_ssid                    = configuration({0, 1, 2});
    changed_ssid.mld_config.mld_ssid     = "different";
    auto changed_unit                    = configuration({0, 1, 2}, 3);
    auto changed_mode                    = configuration({0, 1, 2}, 2, MLO_MODE_NSTR);
    auto replaced_link                   = configuration({0, 1, 2});
    replaced_link.affiliated_aps[2].ruid = tlvf::mac_from_string("00:11:22:33:44:09");

    for (auto *changed : {&changed_ssid, &changed_unit, &changed_mode, &replaced_link}) {
        clear_reported_fields(*changed);
        changed->retain_unchanged_links(previous, current_ruid);
        EXPECT_EQ(changed->mld_config.mld_mac, net::network_utils::ZERO_MAC);
        for (const auto &affiliate : changed->affiliated_aps) {
            expect_unknown(affiliate);
        }
    }

    auto explicit_request = configuration({0, 1, 2});
    clear_reported_fields(explicit_request);
    explicit_request.retain_unchanged_links(previous, net::network_utils::ZERO_MAC);
    EXPECT_EQ(explicit_request.mld_config.mld_mac, net::network_utils::ZERO_MAC);
    for (const auto &affiliate : explicit_request.affiliated_aps) {
        expect_unknown(affiliate);
    }
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

    auto without_mld_mac               = configuration({0, 1, 2});
    without_mld_mac.mld_config.mld_mac = net::network_utils::ZERO_MAC;
    auto rebuilt_without_mld_mac       = configuration({0, 1, 2});
    clear_reported_fields(rebuilt_without_mld_mac);
    rebuilt_without_mld_mac.retain_unchanged_links(without_mld_mac, ruids[2]);
    for (const auto &affiliate : rebuilt_without_mld_mac.affiliated_aps) {
        expect_unknown(affiliate);
    }
}

TEST(AffiliatedAPUpdateTest, only_own_radio_updates_link_and_known_mld_mac_is_kept)
{
    std::vector<Configuration> configurations = {configuration({0, 1})};

    // Same SSID on a radio that is not a link of the AP MLD.
    EXPECT_FALSE(update_ap_mld_link(configurations, "mlo-ssid", ruids[2], bssid(2), 2,
                                    net::network_utils::ZERO_MAC));
    EXPECT_EQ(configurations[0].mld_config.mld_mac, mld_mac);
    EXPECT_EQ(configurations[0].affiliated_aps[1].bssid, bssid(1));

    // A link without an AP MLD MAC keeps the MLD MAC while another link is valid.
    EXPECT_TRUE(update_ap_mld_link(configurations, "mlo-ssid", ruids[1], bssid(1), DISABLED_MLDUNIT,
                                   net::network_utils::ZERO_MAC));
    EXPECT_EQ(configurations[0].mld_config.mld_mac, mld_mac);
    EXPECT_EQ(configurations[0].affiliated_aps[1].bssid, bssid(1));
    EXPECT_EQ(configurations[0].affiliated_aps[1].link_id, DISABLED_MLDUNIT);

    // The MLD MAC is cleared once no link has a valid link ID.
    EXPECT_TRUE(update_ap_mld_link(configurations, "mlo-ssid", ruids[0], bssid(0), DISABLED_MLDUNIT,
                                   net::network_utils::ZERO_MAC));
    EXPECT_EQ(configurations[0].mld_config.mld_mac, net::network_utils::ZERO_MAC);

    EXPECT_FALSE(update_ap_mld_link(configurations, "other-ssid", ruids[0], bssid(0), 0, mld_mac));
}

TEST(AffiliatedAPUpdateTest, same_ssid_ap_mlds_on_different_radios_are_updated_separately)
{
    const auto other_mld_mac                  = tlvf::mac_from_string("02:aa:bb:cc:dd:ff");
    std::vector<Configuration> configurations = {configuration({0, 1}), configuration({2}, 3)};
    for (auto &configuration : configurations) {
        clear_reported_fields(configuration);
    }

    EXPECT_TRUE(
        update_ap_mld_link(configurations, "mlo-ssid", ruids[2], bssid(2), 0, other_mld_mac));
    EXPECT_EQ(configurations[0].mld_config.mld_mac, net::network_utils::ZERO_MAC);
    expect_unknown(configurations[0].affiliated_aps[0]);
    expect_unknown(configurations[0].affiliated_aps[1]);
    EXPECT_EQ(configurations[1].mld_config.mld_mac, other_mld_mac);
    EXPECT_EQ(configurations[1].affiliated_aps[0].bssid, bssid(2));
    EXPECT_EQ(configurations[1].affiliated_aps[0].link_id, 0);

    EXPECT_TRUE(update_ap_mld_link(configurations, "mlo-ssid", ruids[1], bssid(1), 1, mld_mac));
    EXPECT_EQ(configurations[0].mld_config.mld_mac, mld_mac);
    EXPECT_EQ(configurations[0].affiliated_aps[1].link_id, 1);
    EXPECT_EQ(configurations[1].mld_config.mld_mac, other_mld_mac);
}

} // namespace
} // namespace beerocks
