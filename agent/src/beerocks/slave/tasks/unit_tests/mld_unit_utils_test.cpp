/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../mld_unit_utils.h"

#include <beerocks/tlvf/beerocks_message_apmanager.h>
#include <tlvf/ClassList.h>

#include <gtest/gtest.h>

#include <array>

namespace {

using beerocks::mld_unit_utils::preserve_mld_unit_for_matching_ssid;
using beerocks::mld_unit_utils::select_ap_mld_unit;
using beerocks::mld_unit_utils::sMldUnitAssignment;
using beerocks::mld_unit_utils::update_vaps_mld_units;

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, PreservesPreconfiguredUnitForMatchingSsid)
{
    const auto selection = select_ap_mld_unit("home", 4, {}, {{"home", 2}, {"home", 2}});

    EXPECT_EQ(selection.mld_unit, 2);
    EXPECT_FALSE(selection.conflicting_preconfigured_units);
    EXPECT_FALSE(selection.preconfigured_unit_in_use);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, ReservesUnitsUsedByOtherSsids)
{
    const auto selection = select_ap_mld_unit("backhaul", 4, {{"home", 0}}, {{"guest", 1}});

    EXPECT_EQ(selection.mld_unit, 2);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, RejectsPreconfiguredUnitOwnedByDifferentSsid)
{
    const auto selection = select_ap_mld_unit("backhaul", 4, {{"home", 2}}, {{"backhaul", 2}});

    EXPECT_EQ(selection.preconfigured_mld_unit, 2);
    EXPECT_TRUE(selection.preconfigured_unit_in_use);
    EXPECT_EQ(selection.mld_unit, 0);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, RejectsConflictingUnitsForSameSsid)
{
    const auto selection = select_ap_mld_unit("home", 4, {}, {{"home", 1}, {"home", 2}});

    EXPECT_TRUE(selection.conflicting_preconfigured_units);
    EXPECT_EQ(selection.mld_unit, beerocks::DISABLED_MLDUNIT);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, DoesNotCarryMldUnitAcrossSsidReassignment)
{
    EXPECT_EQ(preserve_mld_unit_for_matching_ssid("old", 3, "new", 1), 1);
    EXPECT_EQ(preserve_mld_unit_for_matching_ssid("home", 3, "home", 1), 3);
}

struct sTestBss {
    bool active   = false;
    int8_t mld_id = beerocks::DISABLED_MLDUNIT;
};

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, KeepsMldUnitWhileDisabledAndAfterReenable)
{
    std::array<sTestBss, beerocks::IFACE_TOTAL_VAPS> bssids{};
    std::array<beerocks_message::sVapMldUnit, beerocks::IFACE_TOTAL_VAPS> mld_units{};
    for (size_t i = 0; i < mld_units.size(); ++i) {
        mld_units[i].vap_id   = int(beerocks::IFACE_VAP_ID_MIN) + int(i);
        mld_units[i].mld_unit = beerocks::DISABLED_MLDUNIT;
    }
    mld_units[1].mld_unit = 2;

    EXPECT_FALSE(update_vaps_mld_units(bssids, mld_units.data()).any());
    EXPECT_FALSE(bssids[1].active);
    EXPECT_EQ(bssids[1].mld_id, 2);

    bssids[1].active = true;
    EXPECT_FALSE(update_vaps_mld_units(bssids, mld_units.data()).any());
    EXPECT_TRUE(bssids[1].active);
    EXPECT_EQ(bssids[1].mld_id, 2);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, ClearsUnitOnVapIdMismatch)
{
    std::array<sTestBss, beerocks::IFACE_TOTAL_VAPS> bssids{};
    std::array<beerocks_message::sVapMldUnit, beerocks::IFACE_TOTAL_VAPS> mld_units{};
    for (size_t i = 0; i < mld_units.size(); ++i) {
        bssids[i].mld_id      = 2;
        mld_units[i].vap_id   = int(beerocks::IFACE_VAP_ID_MIN) + int(i);
        mld_units[i].mld_unit = 2;
    }
    mld_units[1].vap_id = mld_units[0].vap_id;

    const auto mismatches = update_vaps_mld_units(bssids, mld_units.data());

    EXPECT_TRUE(mismatches.test(1));
    EXPECT_EQ(mismatches.count(), 1);
    EXPECT_EQ(bssids[1].mld_id, beerocks::DISABLED_MLDUNIT);
    EXPECT_EQ(bssids[0].mld_id, 2);
}

template <typename Message> void initialize_required_fields(Message &) {}

void initialize_required_fields(beerocks_message::cACTION_APMANAGER_JOINED_NOTIFICATION &message)
{
    auto channel_list = message.create_channel_list();
    ASSERT_NE(channel_list, nullptr);
    ASSERT_TRUE(message.add_channel_list(channel_list));
}

template <typename Message> void verify_mld_units_round_trip()
{
    std::array<uint8_t, 4096> buffer{};
    ClassList writer(buffer.data(), buffer.size());
    auto outgoing = writer.addClass<Message>();
    ASSERT_NE(outgoing, nullptr);
    initialize_required_fields(*outgoing);

    for (size_t i = 0; i < beerocks::IFACE_TOTAL_VAPS; ++i) {
        outgoing->vap_mld_unit_list().vap_mld_units[i].vap_id =
            int(beerocks::IFACE_VAP_ID_MIN) + int(i);
        outgoing->vap_mld_unit_list().vap_mld_units[i].mld_unit =
            (i == 1) ? 2 : beerocks::DISABLED_MLDUNIT;
    }
    ASSERT_TRUE(writer.finalize());

    ClassList reader(buffer.data(), writer.getMessageLength(), true);
    auto incoming = reader.addClass<Message>();
    ASSERT_NE(incoming, nullptr);

    for (size_t i = 0; i < beerocks::IFACE_TOTAL_VAPS; ++i) {
        EXPECT_EQ(incoming->vap_mld_unit_list().vap_mld_units[i].vap_id,
                  int(beerocks::IFACE_VAP_ID_MIN) + int(i));
        EXPECT_EQ(incoming->vap_mld_unit_list().vap_mld_units[i].mld_unit,
                  (i == 1) ? 2 : beerocks::DISABLED_MLDUNIT);
    }
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, JoinedNotificationPreservesMldUnits)
{
    verify_mld_units_round_trip<beerocks_message::cACTION_APMANAGER_JOINED_NOTIFICATION>();
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, VapListUpdateNotificationPreservesMldUnits)
{
    verify_mld_units_round_trip<
        beerocks_message::cACTION_APMANAGER_HOSTAP_VAPS_LIST_UPDATE_NOTIFICATION>();
}

} // namespace
