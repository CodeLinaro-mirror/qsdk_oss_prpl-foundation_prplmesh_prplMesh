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

} // namespace
