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

#include <algorithm>
#include <array>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

using beerocks::mld_unit_utils::populate_mld_id_in_bss_infos;
using beerocks::mld_unit_utils::select_ap_mld_unit;
using beerocks::mld_unit_utils::update_vaps_mld_units;
using Radio = beerocks::AgentDB::sRadio;
using Bssid = Radio::sFront::sBssid;
using ApMld = beerocks::AgentDB::sAPMLDConfiguration;

ApMld assigned_mld(const std::string &ssid, int8_t unit)
{
    ApMld mld;
    mld.mld_config.mld_ssid = ssid;
    mld.mld_config.mld_unit = unit;
    return mld;
}

void configure_bss(Radio &radio, size_t index, const std::string &ssid, int8_t unit)
{
    radio.front.bssids[index].configured_ssid = ssid;
    radio.front.bssids[index].mld_id          = unit;
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, ReservesUnitsUsedByOtherSsids)
{
    Radio radio("wlan0", "");
    configure_bss(radio, 0, "guest", 1);
    EXPECT_EQ(select_ap_mld_unit("backhaul", {{}}, 4, {assigned_mld("home", 0)}, {&radio}), 2);
}

struct sTestBssConfig {
    struct {
        std::string ssid;
    } payload_config;
    int8_t mld_id;
};

using MldRequests =
    std::unordered_map<std::string, std::unordered_map<std::string, std::tuple<int8_t, uint8_t>>>;

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, IndependentSameSsidGroupsKeepTheirUnitsInEitherOrder)
{
    Radio radio0("wlan0", ""), radio1("wlan1", "");
    radio0.front.iface_mac = {{0x02, 0, 0, 0, 0, 1}};
    radio1.front.iface_mac = {{0x02, 0, 0, 0, 0, 2}};
    configure_bss(radio0, 0, "home", 0);
    configure_bss(radio1, 0, "home", 1);
    const std::vector<Radio *> radios{&radio0, &radio1};
    for (bool reverse : {false, true}) {
        std::vector<ApMld> assigned;
        for (int index : {int(reverse), int(!reverse)}) {
            const auto unit =
                select_ap_mld_unit("home", {radios[index]->front.iface_mac}, 4, assigned, radios);
            ASSERT_EQ(unit, index);
            assigned.push_back(assigned_mld("home", unit));
        }
    }
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, RemovedMldMembershipClearsOutgoingUnits)
{
    MldRequests requests{{"wlan0", {{"home", {2, 0}}, {"guest", {1, 0}}}}};
    std::vector<sTestBssConfig> configs{{{"home"}, beerocks::DISABLED_MLDUNIT},
                                        {{"guest"}, beerocks::DISABLED_MLDUNIT}};
    populate_mld_id_in_bss_infos("wlan0", requests, configs);
    ASSERT_EQ(configs[0].mld_id, 2);
    ASSERT_EQ(configs[1].mld_id, 1);

    requests.at("wlan0").erase("guest");
    populate_mld_id_in_bss_infos("wlan0", requests, configs);
    EXPECT_EQ(configs[0].mld_id, 2);
    EXPECT_EQ(configs[1].mld_id, beerocks::DISABLED_MLDUNIT);

    requests.at("wlan0").clear();
    populate_mld_id_in_bss_infos("wlan0", requests, configs);
    EXPECT_EQ(configs[0].mld_id, beerocks::DISABLED_MLDUNIT);

    // A radio absent from the request must also clear any old local unit.
    requests.clear();
    configs[0].mld_id = 2;
    populate_mld_id_in_bss_infos("wlan0", requests, configs);
    EXPECT_EQ(configs[0].mld_id, beerocks::DISABLED_MLDUNIT);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, LocalUnitCannotOverrideCollisionCheckedSelection)
{
    Radio radio("wlan0", "");
    configure_bss(radio, 0, "home", 2);
    const auto unit = select_ap_mld_unit("home", {{}}, 4, {assigned_mld("guest", 2)}, {&radio});
    ASSERT_EQ(unit, 0);
    const MldRequests requests{{"wlan0", {{"home", {unit, 0}}}}};
    std::vector<sTestBssConfig> configs{{{"home"}, 2}};
    populate_mld_id_in_bss_infos("wlan0", requests, configs);
    EXPECT_EQ(configs[0].mld_id, 0);
    EXPECT_EQ(std::get<0>(requests.at("wlan0").at("home")), 0);

    configure_bss(radio, 1, "guest", 2);
    EXPECT_EQ(select_ap_mld_unit("home", {{}}, 4, {}, {&radio}), 0);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, DisabledSelectionClearsExistingUnit)
{
    Radio radio("wlan0", "");
    configure_bss(radio, 0, "home", 1);
    configure_bss(radio, 1, "home", 2);
    const auto unit = select_ap_mld_unit("home", {{}}, 4, {}, {&radio});
    ASSERT_EQ(unit, beerocks::DISABLED_MLDUNIT);
    const MldRequests requests{{"wlan0", {{"home", {unit, 0}}}}};
    std::vector<sTestBssConfig> configs{{{"home"}, 2}};
    populate_mld_id_in_bss_infos("wlan0", requests, configs);
    EXPECT_EQ(configs[0].mld_id, beerocks::DISABLED_MLDUNIT);
}

template <size_t Length>
void set_configured_ssid(char (&destination)[Length], const std::string &ssid)
{
    ASSERT_LT(ssid.size(), Length);
    std::fill(std::begin(destination), std::end(destination), '\0');
    std::copy(ssid.begin(), ssid.end(), destination);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, ClearsUnitForMismatchedOrAbsentVap)
{
    std::array<Bssid, beerocks::IFACE_TOTAL_VAPS> bssids{};
    std::array<beerocks_message::sVapMldUnit, beerocks::IFACE_TOTAL_VAPS> mld_units{};
    for (size_t i = 0; i < mld_units.size(); ++i) {
        bssids[i].mld_id          = 2;
        bssids[i].configured_ssid = "old";
        mld_units[i].vap_id       = int(beerocks::IFACE_VAP_ID_MIN) + int(i);
        mld_units[i].mld_unit     = beerocks::DISABLED_MLDUNIT;
    }
    mld_units[0].mld_unit = 2;
    set_configured_ssid(mld_units[0].configured_ssid, "new");
    mld_units[1].mld_unit = 2;
    set_configured_ssid(mld_units[1].configured_ssid, "new");
    mld_units[1].vap_id = mld_units[0].vap_id;

    update_vaps_mld_units(bssids, mld_units.data());
    EXPECT_EQ(bssids[1].mld_id, beerocks::DISABLED_MLDUNIT);
    EXPECT_TRUE(bssids[1].configured_ssid.empty());
    EXPECT_EQ(bssids[0].mld_id, 2);
    EXPECT_EQ(bssids[0].configured_ssid, "new");
    EXPECT_EQ(bssids[2].mld_id, beerocks::DISABLED_MLDUNIT);
    EXPECT_TRUE(bssids[2].configured_ssid.empty());
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, SelectorUsesUpdatedConfiguredSsidForDisabledBsses)
{
    Radio radio("wlan0", "");
    auto &bssids = radio.front.bssids;
    std::array<beerocks_message::sVapMldUnit, beerocks::IFACE_TOTAL_VAPS> mld_units{};
    for (size_t i = 0; i < mld_units.size(); ++i) {
        mld_units[i].vap_id   = int(beerocks::IFACE_VAP_ID_MIN) + int(i);
        mld_units[i].mld_unit = beerocks::DISABLED_MLDUNIT;
    }
    bssids[0].active = true;
    bssids[0].ssid   = "fh";
    set_configured_ssid(mld_units[0].configured_ssid, "fh");
    mld_units[0].mld_unit = 0;
    bssids[1].active      = true;
    set_configured_ssid(mld_units[1].configured_ssid, "guest");
    mld_units[1].mld_unit = 1;
    bssids[2].active      = true;
    bssids[2].ssid        = "bh";
    set_configured_ssid(mld_units[2].configured_ssid, "bh");
    mld_units[2].mld_unit = 2;
    bssids[3].active      = true;
    set_configured_ssid(mld_units[3].configured_ssid, "bh");
    mld_units[3].mld_unit = 2;

    update_vaps_mld_units(bssids, mld_units.data());

    EXPECT_TRUE(bssids[1].ssid.empty());
    EXPECT_TRUE(bssids[3].ssid.empty());
    EXPECT_EQ(select_ap_mld_unit("bh", {{}}, 4, {}, {&radio}), 2);
    EXPECT_EQ(select_ap_mld_unit("guest", {{}}, 4, {}, {&radio}), 1);

    // Re-enabling changes operational SSIDs, but must not change unit ownership.
    bssids[1].ssid = "guest";
    bssids[3].ssid = "bh";
    EXPECT_EQ(select_ap_mld_unit("bh", {{}}, 4, {}, {&radio}), 2);
    EXPECT_EQ(select_ap_mld_unit("guest", {{}}, 4, {}, {&radio}), 1);
}

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, DisabledRenamedBssDoesNotRetainOldSsidOwner)
{
    Radio radio("wlan0", "");
    auto &bssids              = radio.front.bssids;
    bssids[1].configured_ssid = "old";
    bssids[1].mld_id          = 1;
    std::array<beerocks_message::sVapMldUnit, beerocks::IFACE_TOTAL_VAPS> mld_units{};
    for (size_t i = 0; i < mld_units.size(); ++i) {
        mld_units[i].vap_id   = int(beerocks::IFACE_VAP_ID_MIN) + int(i);
        mld_units[i].mld_unit = beerocks::DISABLED_MLDUNIT;
    }
    mld_units[1].mld_unit = 1;
    set_configured_ssid(mld_units[1].configured_ssid, "new");

    update_vaps_mld_units(bssids, mld_units.data());
    EXPECT_EQ(bssids[1].configured_ssid, "new");
    EXPECT_TRUE(bssids[1].ssid.empty());
    EXPECT_EQ(select_ap_mld_unit("new", {{}}, 4, {}, {&radio}), 1);
    EXPECT_EQ(select_ap_mld_unit("old", {{}}, 4, {}, {&radio}), 0);
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
        const auto configured_ssid =
            (i == 1) ? std::string(beerocks::message::WIFI_SSID_MAX_LENGTH - 1, 'x') : "";
        set_configured_ssid(outgoing->vap_mld_unit_list().vap_mld_units[i].configured_ssid,
                            configured_ssid);
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
        const auto configured_ssid =
            (i == 1) ? std::string(beerocks::message::WIFI_SSID_MAX_LENGTH - 1, 'x') : "";
        EXPECT_STREQ(incoming->vap_mld_unit_list().vap_mld_units[i].configured_ssid,
                     configured_ssid.c_str());
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

// cppcheck-suppress syntaxError
TEST(MldUnitUtilsTest, ApEnabledNotificationPreservesConfiguredSsid)
{
    const auto configured_ssid = std::string(beerocks::message::WIFI_SSID_MAX_LENGTH - 1, 'x');
    std::array<uint8_t, 4096> buffer{};
    ClassList writer(buffer.data(), buffer.size());
    auto outgoing =
        writer.addClass<beerocks_message::cACTION_APMANAGER_HOSTAP_AP_ENABLED_NOTIFICATION>();
    ASSERT_NE(outgoing, nullptr);
    outgoing->vap_id()   = beerocks::IFACE_VAP_ID_MIN;
    outgoing->mld_unit() = 2;
    ASSERT_TRUE(outgoing->set_configured_ssid(configured_ssid));
    ASSERT_TRUE(writer.finalize());

    ClassList reader(buffer.data(), writer.getMessageLength(), true);
    auto incoming =
        reader.addClass<beerocks_message::cACTION_APMANAGER_HOSTAP_AP_ENABLED_NOTIFICATION>();
    ASSERT_NE(incoming, nullptr);
    EXPECT_EQ(incoming->configured_ssid_str(), configured_ssid);
}

} // namespace
