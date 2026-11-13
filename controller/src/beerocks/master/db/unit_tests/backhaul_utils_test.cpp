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
using prplmesh::controller::db::resolve_wireless_backhaul_connection_bssid;
using prplmesh::controller::db::resolve_wireless_backhaul_parent;
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

constexpr sMacAddr ap_mld_mac{{0x02, 0, 0, 0, 0, 11}};
constexpr sMacAddr link_bssid{{0x02, 0, 0, 0, 0, 12}};
constexpr sMacAddr other_link_bssid{{0x02, 0, 0, 0, 0, 13}};

class WirelessBackhaulEndpoint : public ::testing::Test {
protected:
    beerocks::mac_map<Agent> agents;
    std::shared_ptr<Agent> parent;
    std::shared_ptr<Agent::sRadio> radio;
    Agent child{agent_mac};

    void SetUp() override
    {
        parent = agents.add(parent_mac);
        radio  = parent->radios.add(radio_mac);
        radio->bsses.add(link_bssid, *radio);
        child.backhaul                     = {};
        child.backhaul.backhaul_iface_type = beerocks::IFACE_TYPE_WIFI_UNSPECIFIED;
        child.backhaul.backhaul_interface  = bsta_mac;
        child.backhaul.parent_interface    = link_bssid;
        child.backhaul.parent_agent        = parent;
    }

    Agent::sAPMLD &add_ap_mld()
    {
        auto &mld                           = parent->ap_mlds[ap_mld_mac];
        mld.mld_info.mld_mac                = ap_mld_mac;
        mld.affiliated_aps[radio_mac]       = {"", radio_mac, link_bssid, 0};
        mld.affiliated_aps[other_radio_mac] = {"", other_radio_mac, other_link_bssid, 1};
        return mld;
    }
};

TEST_F(WirelessBackhaulEndpoint, resolve_parent_by_bssid)
{
    EXPECT_EQ(parent, resolve_wireless_backhaul_parent(agents, agent_mac, link_bssid));
}

TEST_F(WirelessBackhaulEndpoint, resolve_parent_by_distinct_ap_mld_address)
{
    add_ap_mld();
    EXPECT_EQ(parent, resolve_wireless_backhaul_parent(agents, agent_mac, ap_mld_mac));
}

TEST_F(WirelessBackhaulEndpoint, resolve_affiliated_ap_before_bss_inventory_is_available)
{
    add_ap_mld();
    radio->bsses.clear();
    EXPECT_EQ(parent, resolve_wireless_backhaul_parent(agents, agent_mac, link_bssid));
}

TEST_F(WirelessBackhaulEndpoint, do_not_resolve_unknown_or_zero_endpoint)
{
    EXPECT_FALSE(resolve_wireless_backhaul_parent(agents, agent_mac, zero_mac));
    EXPECT_FALSE(resolve_wireless_backhaul_parent(agents, agent_mac, other_link_bssid));
}

TEST_F(WirelessBackhaulEndpoint, do_not_assign_reporting_agent_as_its_own_parent)
{
    EXPECT_FALSE(resolve_wireless_backhaul_parent(agents, parent_mac, link_bssid));
    add_ap_mld();
    EXPECT_FALSE(resolve_wireless_backhaul_parent(agents, parent_mac, ap_mld_mac));
}

TEST_F(WirelessBackhaulEndpoint, bssid_endpoint_identifies_selected_link)
{
    EXPECT_EQ(link_bssid, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, affiliated_bssid_identifies_link_without_bss_inventory)
{
    add_ap_mld();
    radio->bsses.clear();
    EXPECT_EQ(link_bssid, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, mld_endpoint_alone_does_not_identify_selected_link)
{
    add_ap_mld();
    child.backhaul.parent_interface = ap_mld_mac;
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, address_shared_by_mld_and_bss_is_not_proof_of_selected_link)
{
    radio->bsses.add(ap_mld_mac, *radio);
    add_ap_mld().affiliated_aps[radio_mac].bssid = ap_mld_mac;
    child.backhaul.parent_interface              = ap_mld_mac;
    EXPECT_EQ(parent, resolve_wireless_backhaul_parent(agents, agent_mac, ap_mld_mac));
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, exact_affiliated_bsta_identifies_selected_link_on_mld)
{
    auto &mld                                           = add_ap_mld();
    auto station                                        = mld.sta_mlds.add(bsta_mld_mac);
    station->affiliated_stas.add(other_bsta_mac)->bssid = link_bssid;
    station->affiliated_stas.add(bsta_mac)->bssid       = other_link_bssid;
    child.backhaul.parent_interface                     = ap_mld_mac;
    EXPECT_EQ(other_link_bssid, resolve_wireless_backhaul_connection_bssid(child, 1));
    EXPECT_EQ(ap_mld_mac, child.backhaul.parent_interface);
}

TEST_F(WirelessBackhaulEndpoint, logical_sta_mld_does_not_select_one_of_its_affiliated_links)
{
    auto station                                        = add_ap_mld().sta_mlds.add(bsta_mld_mac);
    station->affiliated_stas.add(bsta_mac)->bssid       = link_bssid;
    station->affiliated_stas.add(other_bsta_mac)->bssid = other_link_bssid;
    child.backhaul.backhaul_interface                   = bsta_mld_mac;
    child.backhaul.parent_interface                     = ap_mld_mac;
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, logical_sta_address_matching_affiliate_remains_ambiguous)
{
    auto station                                  = add_ap_mld().sta_mlds.add(bsta_mac);
    station->affiliated_stas.add(bsta_mac)->bssid = link_bssid;
    child.backhaul.parent_interface               = ap_mld_mac;
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, unknown_upstream_endpoint_does_not_become_selected_bssid)
{
    child.backhaul.parent_interface = other_link_bssid;
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, wired_endpoint_does_not_become_selected_bssid)
{
    child.backhaul.backhaul_iface_type = beerocks::IFACE_TYPE_ETHERNET;
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, unresolved_parent_does_not_supply_selected_bssid)
{
    child.backhaul.parent_agent.reset();
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 1));
}

TEST_F(WirelessBackhaulEndpoint, logical_mld_endpoint_does_not_prove_previous_radio_is_selected)
{
    add_ap_mld();
    child.backhaul.parent_interface  = ap_mld_mac;
    const auto local_radio           = child.radios.add(other_radio_mac);
    auto previous                    = child.backhaul;
    previous.wireless_backhaul_radio = local_radio;
    EXPECT_FALSE(resolve_wireless_backhaul_radio(child, previous));
}

TEST_F(WirelessBackhaulEndpoint, multiple_reported_sta_interfaces_do_not_establish_primary_link)
{
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 2));
}

TEST_F(WirelessBackhaulEndpoint, no_reported_sta_interface_does_not_establish_primary_link)
{
    EXPECT_EQ(zero_mac, resolve_wireless_backhaul_connection_bssid(child, 0));
}

} // namespace
