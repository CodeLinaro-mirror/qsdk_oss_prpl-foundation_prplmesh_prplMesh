/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "../backhaul_utils.h"

#include <gtest/gtest.h>

namespace {

using prplmesh::controller::db::Agent;
using prplmesh::controller::db::resolve_ethernet_parent_interface;
using prplmesh::controller::db::resolve_wireless_backhaul_radio;

constexpr sMacAddr parent_mac{{0x02, 0, 0, 0, 0, 1}};
constexpr sMacAddr other_parent_mac{{0x02, 0, 0, 0, 0, 2}};
constexpr sMacAddr port_mac{{0x02, 0, 0, 0, 0, 3}};
constexpr sMacAddr new_port_mac{{0x02, 0, 0, 0, 0, 4}};
constexpr sMacAddr zero_mac{};

class BackhaulParentInterface : public ::testing::Test {
protected:
    std::shared_ptr<Agent> parent = std::make_shared<Agent>(parent_mac);
    Agent::sBackhaul previous{};

    void SetUp() override
    {
        previous.backhaul_iface_type = beerocks::IFACE_TYPE_ETHERNET;
        previous.parent_agent        = parent;
        previous.parent_interface    = port_mac;
    }
};

TEST_F(BackhaulParentInterface, preserve_port_for_same_ethernet_parent)
{
    EXPECT_EQ(port_mac, resolve_ethernet_parent_interface(previous, parent_mac, zero_mac));
}

TEST_F(BackhaulParentInterface, prefer_freshly_reported_port)
{
    EXPECT_EQ(new_port_mac, resolve_ethernet_parent_interface(previous, parent_mac, new_port_mac));
}

TEST_F(BackhaulParentInterface, do_not_reuse_wireless_bssid_for_ethernet)
{
    previous.backhaul_iface_type = beerocks::IFACE_TYPE_WIFI_UNSPECIFIED;
    EXPECT_EQ(zero_mac, resolve_ethernet_parent_interface(previous, parent_mac, zero_mac));
}

TEST_F(BackhaulParentInterface, do_not_reuse_port_after_parent_change)
{
    EXPECT_EQ(zero_mac, resolve_ethernet_parent_interface(previous, other_parent_mac, zero_mac));
}

TEST_F(BackhaulParentInterface, do_not_reuse_port_without_previous_parent)
{
    previous.parent_agent.reset();
    EXPECT_EQ(zero_mac, resolve_ethernet_parent_interface(previous, parent_mac, zero_mac));
}

TEST_F(BackhaulParentInterface, do_not_reuse_port_of_expired_parent)
{
    parent.reset();
    EXPECT_EQ(zero_mac, resolve_ethernet_parent_interface(previous, parent_mac, zero_mac));
}

TEST_F(BackhaulParentInterface, keep_unknown_port_unknown)
{
    previous.parent_interface = zero_mac;
    EXPECT_EQ(zero_mac, resolve_ethernet_parent_interface(previous, parent_mac, zero_mac));
}

TEST_F(BackhaulParentInterface, reporter_snapshot_survives_reset_of_current_backhaul)
{
    auto current = previous;
    const Agent::sBackhaul snapshot(current);
    current.parent_agent.reset();
    current.parent_interface = zero_mac;
    EXPECT_EQ(port_mac, resolve_ethernet_parent_interface(snapshot, parent_mac, zero_mac));
    EXPECT_EQ(zero_mac, current.parent_interface);
}

constexpr sMacAddr agent_mac{{0x02, 0, 0, 0, 0, 5}};
constexpr sMacAddr radio_mac{{0x02, 0, 0, 0, 0, 6}};
constexpr sMacAddr other_radio_mac{{0x02, 0, 0, 0, 0, 7}};
constexpr sMacAddr bsta_mac{{0x02, 0, 0, 0, 0, 8}};
constexpr sMacAddr other_bsta_mac{{0x02, 0, 0, 0, 0, 9}};
constexpr sMacAddr bsta_mld_mac{{0x02, 0, 0, 0, 0, 10}};

class WirelessBackhaulRadio : public ::testing::Test {
protected:
    Agent agent{agent_mac};
    Agent::sBackhaul previous{};
    std::shared_ptr<Agent::sRadio> radio;

    void SetUp() override
    {
        radio                              = agent.radios.add(radio_mac);
        agent.backhaul                     = {};
        agent.backhaul.backhaul_iface_type = beerocks::IFACE_TYPE_WIFI_UNSPECIFIED;
        agent.backhaul.backhaul_interface  = bsta_mac;
        agent.backhaul.parent_interface    = port_mac;
        previous                           = agent.backhaul;
        previous.wireless_backhaul_radio   = radio;
    }
};

TEST_F(WirelessBackhaulRadio, resolve_from_backhaul_capability_without_previous_radio)
{
    radio->backhaul_station_mac = bsta_mac;
    previous.wireless_backhaul_radio.reset();
    EXPECT_EQ(radio, resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, prefer_capability_mapping_to_previous_radio)
{
    const auto new_radio            = agent.radios.add(other_radio_mac);
    new_radio->backhaul_station_mac = bsta_mac;
    EXPECT_EQ(new_radio, resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, preserve_radio_when_capability_is_missing_for_unchanged_connection)
{
    EXPECT_EQ(radio, resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, do_not_preserve_radio_after_bsta_change)
{
    agent.backhaul.backhaul_interface = other_bsta_mac;
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, do_not_preserve_radio_after_upstream_address_change)
{
    agent.backhaul.parent_interface = new_port_mac;
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, do_not_preserve_radio_when_upstream_address_is_unknown)
{
    agent.backhaul.parent_interface = zero_mac;
    previous.parent_interface       = zero_mac;
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, do_not_preserve_radio_from_previous_wired_connection)
{
    previous.backhaul_iface_type = beerocks::IFACE_TYPE_ETHERNET;
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, wired_backhaul_has_no_wireless_radio_even_with_capability_match)
{
    radio->backhaul_station_mac        = bsta_mac;
    agent.backhaul.backhaul_iface_type = beerocks::IFACE_TYPE_ETHERNET;
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, do_not_resolve_zero_bsta_from_unreported_capability)
{
    agent.backhaul.backhaul_interface = zero_mac;
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, do_not_preserve_removed_radio)
{
    agent.radios.erase(radio_mac);
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, use_current_radio_object_when_same_ruid_is_recreated)
{
    agent.radios.erase(radio_mac);
    const auto new_radio = agent.radios.add(radio_mac);
    ASSERT_NE(radio, new_radio);
    EXPECT_EQ(new_radio, resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, unresolved_radio_does_not_change_wireless_parent)
{
    previous.wireless_backhaul_radio.reset();
    const auto parent           = std::make_shared<Agent>(parent_mac);
    agent.backhaul.parent_agent = parent;
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
    EXPECT_EQ(parent, agent.backhaul.parent_agent.lock());
    EXPECT_EQ(beerocks::IFACE_TYPE_WIFI_UNSPECIFIED, agent.backhaul.backhaul_iface_type);
    EXPECT_EQ(port_mac, agent.backhaul.parent_interface);
}

TEST_F(WirelessBackhaulRadio, resolve_from_affiliated_bsta_ruid_without_capability)
{
    previous.wireless_backhaul_radio.reset();
    agent.bsta_mld.affiliated_bstas[radio_mac] = {radio_mac, bsta_mac};
    EXPECT_EQ(radio, resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, do_not_resolve_affiliated_bsta_to_unknown_local_ruid)
{
    agent.bsta_mld.affiliated_bstas[other_radio_mac] = {other_radio_mac, bsta_mac};
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

TEST_F(WirelessBackhaulRadio, do_not_choose_arbitrary_affiliated_radio_for_mld_mac)
{
    agent.backhaul.backhaul_interface                = bsta_mld_mac;
    agent.bsta_mld.mld_info.mld_mac                  = bsta_mld_mac;
    agent.bsta_mld.affiliated_bstas[radio_mac]       = {radio_mac, bsta_mac};
    agent.bsta_mld.affiliated_bstas[other_radio_mac] = {other_radio_mac, other_bsta_mac};
    agent.radios.add(other_radio_mac);
    EXPECT_FALSE(resolve_wireless_backhaul_radio(agent, previous));
}

} // namespace
