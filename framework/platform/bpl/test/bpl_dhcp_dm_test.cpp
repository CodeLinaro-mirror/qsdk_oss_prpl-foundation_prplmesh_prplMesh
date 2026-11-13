/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_dhcp_utils.h"
#include <gtest/gtest.h>
#include <tlvf/tlvftypes.h>

using namespace beerocks;

namespace {
class DhcpDmTest : public ::testing::Test {
protected:
    wbapi::AmbiorixVariantMap clients, hosts;
    // 2026-10-23T08:00:00Z, independent of the machine running the tests.
    const std::chrono::system_clock::time_point now{std::chrono::seconds(1792742400)};

    std::string add_client(unsigned index, const std::string &mac, bool active = true,
                           unsigned pool = 1)
    {
        const auto path =
            "DHCPv4Server.Pool." + std::to_string(pool) + ".Client." + std::to_string(index) + ".";
        auto &client = clients[path];
        client.set_type(AMXC_VAR_ID_HTABLE);
        client.add_child("Chaddr", mac);
        client.add_child("Active", active);
        return path;
    }

    void add_address(const std::string &path, const std::string &ip,
                     const std::string &expiry = "2026-10-23T09:00:00Z", unsigned index = 1)
    {
        auto &address = clients[path + "IPv4Address." + std::to_string(index) + "."];
        address.set_type(AMXC_VAR_ID_HTABLE);
        address.add_child("IPAddress", ip);
        address.add_child("LeaseTimeRemaining", expiry);
    }

    void add_host(unsigned index, const std::string &mac, const std::string &name)
    {
        auto &host = hosts["Hosts.Host." + std::to_string(index) + "."];
        host.set_type(AMXC_VAR_ID_HTABLE);
        host.add_child("PhysAddress", mac);
        host.add_child("HostName", name);
        host.add_child("IPAddress", "192.168.1.254");
        host.add_child("InterfaceType", "Other");
    }

    bpl::ipv4_lease_map_t parse() { return bpl::dhcp::parse_ipv4_leases(clients, hosts, now); }
};

TEST_F(DhcpDmTest, MatchesNamesByMacAndIgnoresPollutedHostsAddresses)
{
    for (unsigned i = 1; i <= 5; ++i) {
        const auto mac = "04:11:19:20:d4:0" + std::to_string(i);
        add_address(add_client(i, mac), "192.168.1." + std::to_string(i + 1));
        add_host(10 - i, "04:11:19:20:D4:0" + std::to_string(i), "client-" + std::to_string(i));
        for (unsigned j = 1; j <= 5; ++j) {
            auto &address = hosts["Hosts.Host." + std::to_string(10 - i) + ".IPv4Address." +
                                  std::to_string(j) + "."];
            address.set_type(AMXC_VAR_ID_HTABLE);
            address.add_child("IPAddress", "192.168.1." + std::to_string(j + 1));
        }
    }
    const auto leases = parse();
    ASSERT_EQ(5U, leases.size());
    for (unsigned i = 1; i <= 5; ++i) {
        const auto &lease =
            leases.at(tlvf::mac_from_string("04:11:19:20:d4:0" + std::to_string(i)));
        EXPECT_EQ("192.168.1." + std::to_string(i + 1), lease.ip_address);
        EXPECT_EQ("client-" + std::to_string(i), lease.host_name);
        EXPECT_EQ(std::chrono::hours(1), lease.validity);
    }
}

TEST_F(DhcpDmTest, AcceptsDevicePrefixAndMultiplePools)
{
    add_address("Device." + add_client(1, "04:11:19:20:d4:02", true, 2), "192.168.2.2");
    add_address(add_client(1, "04:11:19:20:d4:06"), "192.168.1.2");
    EXPECT_EQ(2U, parse().size());
}

TEST_F(DhcpDmTest, IgnoresInactiveMissingAndInvalidClientMacs)
{
    add_address(add_client(1, "04:11:19:20:d4:02", false), "192.168.1.2");
    add_address(add_client(2, "not-a-mac"), "192.168.1.3");
    add_address(add_client(3, "00:00:00:00:00:00"), "192.168.1.4");
    add_address(add_client(4, "ff:ff:ff:ff:ff:ff"), "192.168.1.5");
    add_address("DHCPv4Server.Pool.1.Client.99.", "192.168.1.6");
    EXPECT_TRUE(parse().empty());
}

TEST_F(DhcpDmTest, IgnoresInvalidAndExpiredAddresses)
{
    const auto path                        = add_client(1, "04:11:19:20:d4:02");
    const std::vector<std::string> invalid = {
        "", "0", "0.0.0.0", "255.255.255.255", "224.0.0.1", "127.0.0.1", "::1", "192.168.1.999"};
    unsigned index = 0;
    for (const auto &ip : invalid) {
        add_address(path, ip, "2026-10-23T09:00:00Z", ++index);
    }
    add_address(path, "192.168.1.2", "2026-10-23T07:59:59Z", ++index);
    add_address(path, "192.168.1.3", "2026-10-23T08:00:00Z", ++index);
    EXPECT_TRUE(parse().empty());
}

TEST_F(DhcpDmTest, SelectsLatestExpiryThenLowestNumericAddress)
{
    const auto path = add_client(1, "04:11:19:20:d4:02");
    add_address(path, "192.168.1.2", "2026-10-23T08:30:00Z", 1);
    add_address(path, "192.168.1.10", "2026-10-23T09:00:00Z", 2);
    add_address(path, "192.168.1.9", "2026-10-23T09:00:00Z", 10);
    add_address(add_client(2, "04:11:19:20:d4:02", true, 2), "192.168.2.1");
    const auto leases = parse();
    ASSERT_EQ(1U, leases.size());
    EXPECT_EQ("192.168.1.9", leases.begin()->second.ip_address);
}

TEST_F(DhcpDmTest, AcceptsUnknownAndInfiniteExpiry)
{
    add_address(add_client(1, "04:11:19:20:d4:02"), "192.168.1.2", "0001-01-01T00:00:00Z");
    add_address(add_client(2, "04:11:19:20:d4:06"), "192.168.1.3", "9999-12-31T23:59:59Z");
    const auto leases = parse();
    ASSERT_EQ(2U, leases.size());
    EXPECT_EQ(std::chrono::milliseconds(0),
              leases.at(tlvf::mac_from_string("04:11:19:20:d4:02")).validity);
    EXPECT_GT(leases.at(tlvf::mac_from_string("04:11:19:20:d4:06")).validity,
              std::chrono::hours(24));
}

TEST_F(DhcpDmTest, MissingHostnameDoesNotSuppressLease)
{
    add_address(add_client(1, "04:11:19:20:d4:02"), "192.168.1.2");
    const auto leases = parse();
    ASSERT_EQ(1U, leases.size());
    EXPECT_TRUE(leases.begin()->second.host_name.empty());
}

TEST_F(DhcpDmTest, EmptySnapshotHasNoLeases)
{
    add_host(1, "04:11:19:20:d4:02", "stale-host");
    EXPECT_TRUE(parse().empty());
}
} // namespace
