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

using sAffiliatedAP = AgentDB::sAPMLDConfiguration::sAffiliatedAP;
using sBssid        = AgentDB::sRadio::sFront::sBssid;

const auto ruid       = tlvf::mac_from_string("00:11:22:33:44:55");
const auto bssid      = tlvf::mac_from_string("02:11:22:33:44:55");
const auto ap_mld_mac = tlvf::mac_from_string("02:aa:bb:cc:dd:ee");

sBssid ready_bss()
{
    sBssid bss{};
    bss.enabled   = true;
    bss.ssid      = "mlo-ssid";
    bss.mac       = bssid;
    bss.apmld_mac = ap_mld_mac;
    bss.link_id   = 2;
    return bss;
}

TEST(AffiliatedAPTest, default_state_is_safe_to_serialize)
{
    const sAffiliatedAP affiliated{};

    EXPECT_EQ(affiliated.ruid, net::network_utils::ZERO_MAC);
    EXPECT_EQ(affiliated.bssid, net::network_utils::ZERO_MAC);
    EXPECT_EQ(affiliated.link_id, DISABLED_MLDUNIT);
    EXPECT_FALSE(affiliated.has_valid_link_id());
}

TEST(AffiliatedAPTest, link_id_accepts_only_the_four_bit_range)
{
    for (const auto &test_case :
         {std::make_pair(-1, false), std::make_pair(0, true), std::make_pair(1, true),
          std::make_pair(2, true), std::make_pair(15, true), std::make_pair(16, false),
          std::make_pair(85, false)}) {
        sAffiliatedAP affiliated{};
        affiliated.link_id = static_cast<int8_t>(test_case.first);
        EXPECT_EQ(affiliated.has_valid_link_id(), test_case.second) << test_case.first;
    }
}

TEST(AffiliatedAPTest, imports_matching_ready_bss)
{
    sAffiliatedAP affiliated{};
    affiliated.ruid = ruid;

    EXPECT_TRUE(affiliated.update_from_bss(ready_bss(), "mlo-ssid"));
    EXPECT_EQ(affiliated.bssid, bssid);
    EXPECT_EQ(affiliated.link_id, 2);
}

TEST(AffiliatedAPTest, rejects_bss_that_is_not_ready_or_matching)
{
    const auto original_bssid = tlvf::mac_from_string("02:00:00:00:00:01");
    sAffiliatedAP affiliated{};
    affiliated.bssid   = original_bssid;
    affiliated.link_id = 7;

    auto expect_rejected_without_mutation = [&affiliated, &original_bssid](sBssid bss,
                                                                           const char *ssid) {
        EXPECT_FALSE(affiliated.update_from_bss(bss, ssid));
        EXPECT_EQ(affiliated.bssid, original_bssid);
        EXPECT_EQ(affiliated.link_id, 7);
    };

    auto bss = ready_bss();
    bss.ssid = "other-ssid";
    expect_rejected_without_mutation(bss, "mlo-ssid");

    bss         = ready_bss();
    bss.enabled = false;
    expect_rejected_without_mutation(bss, "mlo-ssid");

    bss     = ready_bss();
    bss.mac = net::network_utils::ZERO_MAC;
    expect_rejected_without_mutation(bss, "mlo-ssid");

    bss           = ready_bss();
    bss.apmld_mac = net::network_utils::ZERO_MAC;
    expect_rejected_without_mutation(bss, "mlo-ssid");

    for (const int id : {-1, 16, 85}) {
        bss         = ready_bss();
        bss.link_id = static_cast<int8_t>(id);
        expect_rejected_without_mutation(bss, "mlo-ssid");
    }
}

TEST(AffiliatedAPTest, fresh_rebuild_can_restore_each_radio_independently)
{
    const std::array<sMacAddr, 3> bssids = {tlvf::mac_from_string("ea:c7:cf:b1:38:c9"),
                                            tlvf::mac_from_string("ea:c7:cf:b1:37:ca"),
                                            tlvf::mac_from_string("e8:c7:cf:b1:35:cc")};
    for (size_t i = 0; i < bssids.size(); ++i) {
        auto bss    = ready_bss();
        bss.mac     = bssids[i];
        bss.link_id = i;
        sAffiliatedAP affiliated{};
        affiliated.bssid   = tlvf::mac_from_string("00:00:00:80:ff:6f");
        affiliated.link_id = 85;

        // Reconfiguration rebuilds the record even if no new radio event arrives.
        for (int rebuild = 0; rebuild < 3; ++rebuild) {
            affiliated = sAffiliatedAP{};
            EXPECT_TRUE(affiliated.update_from_bss(bss, "mlo-ssid"));
            EXPECT_EQ(affiliated.bssid, bssids[i]);
            EXPECT_EQ(affiliated.link_id, i);
        }
    }
}

TEST(AffiliatedAPTest, unavailable_link_stays_unknown_until_new_bss_is_ready)
{
    auto bss    = ready_bss();
    bss.enabled = false;
    sAffiliatedAP affiliated{};
    EXPECT_FALSE(affiliated.update_from_bss(bss, "mlo-ssid"));
    EXPECT_EQ(affiliated.bssid, net::network_utils::ZERO_MAC);
    EXPECT_FALSE(affiliated.has_valid_link_id());

    bss.enabled = true;
    bss.mac     = tlvf::mac_from_string("02:00:00:00:00:03");
    bss.link_id = 1;
    EXPECT_TRUE(affiliated.update_from_bss(bss, "mlo-ssid"));
    EXPECT_EQ(affiliated.bssid, bss.mac);
    EXPECT_EQ(affiliated.link_id, 1);
}

} // namespace
} // namespace beerocks
