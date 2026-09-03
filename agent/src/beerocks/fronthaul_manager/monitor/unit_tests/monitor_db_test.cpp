/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../monitor_db.h"

#include <gtest/gtest.h>

namespace {

TEST(MonitorDbTest, DisconnectOnlyErasesStationFromMatchingVap)
{
    constexpr int8_t old_vap_id     = 1;
    constexpr int8_t current_vap_id = 2;
    const std::string sta_mac       = "02:00:00:00:00:01";

    son::monitor_db db;
    auto old_vap     = db.vap_add("wlan0.1", old_vap_id);
    auto current_vap = db.vap_add("wlan0.2", current_vap_id);
    db.sta_add(sta_mac, current_vap_id);

    EXPECT_FALSE(db.sta_erase(sta_mac, old_vap_id));
    auto sta = db.sta_find(sta_mac);
    ASSERT_TRUE(sta);
    EXPECT_EQ(sta->get_vap_id(), current_vap_id);
    EXPECT_EQ(old_vap->sta_get_count(), 0);
    EXPECT_EQ(current_vap->sta_get_count(), 1);

    EXPECT_TRUE(db.sta_erase(sta_mac, current_vap_id));
    EXPECT_FALSE(db.sta_find(sta_mac));
    EXPECT_EQ(current_vap->sta_get_count(), 0);
}

} // namespace
