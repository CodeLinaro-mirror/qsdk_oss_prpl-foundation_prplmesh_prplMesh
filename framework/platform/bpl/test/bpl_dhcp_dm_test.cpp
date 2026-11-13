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
    wbapi::AmbiorixVariantMap clients, hosts, ipv6_clients, ipv6_addresses;
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

    bpl::ipv6_lease_map_t parse_ipv6()
    {
        return bpl::dhcp::parse_ipv6_leases(ipv6_clients, hosts, ipv6_addresses, now);
    }

    void add_host_ipv6(unsigned host, const std::string &ip, unsigned index = 1,
                       const std::string &status = "")
    {
        auto &address = ipv6_addresses["Hosts.Host." + std::to_string(host) + ".IPv6Address." +
                                       std::to_string(index) + "."];
        address.set_type(AMXC_VAR_ID_HTABLE);
        address.add_child("IPAddress", ip);
        if (!status.empty()) {
            address.add_child("Status", status);
        }
    }

    std::string add_ipv6_client(unsigned index, bool active = true, unsigned pool = 1)
    {
        const auto path =
            "DHCPv6Server.Pool." + std::to_string(pool) + ".Client." + std::to_string(index) + ".";
        auto &client = ipv6_clients[path];
        client.set_type(AMXC_VAR_ID_HTABLE);
        client.add_child("Active", active);
        return path;
    }

    void set_dhcp_ref(unsigned host, const std::string &ref)
    {
        hosts["Hosts.Host." + std::to_string(host) + "."].add_child("DHCPClient", ref);
    }

    void add_dhcp_ipv6(const std::string &path, const std::string &ip,
                       const std::string &valid = "2026-10-23T09:00:00Z", unsigned index = 1,
                       const std::string &preferred = "0001-01-01T00:00:00Z")
    {
        auto &address = ipv6_clients[path + "IPv6Address." + std::to_string(index) + "."];
        address.set_type(AMXC_VAR_ID_HTABLE);
        address.add_child("IPAddress", ip);
        address.add_child("ValidLifetime", valid);
        address.add_child("PreferredLifetime", preferred);
    }
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

TEST_F(DhcpDmTest, Ipv6DiscoveryMatchesHostMacWithoutAnIpv4Lease)
{
    add_host(3, "04:11:19:20:D4:02", "ipv6-only");
    add_host_ipv6(3, "FE80:0000:0000:0000:8524:A3A1:7E73:39B0");
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    const auto &lease = leases.at("04:11:19:20:d4:02");
    EXPECT_EQ(tlvf::mac_from_string("04:11:19:20:d4:02"), lease.mac);
    EXPECT_EQ("fe80::8524:a3a1:7e73:39b0", lease.ip_address);
    EXPECT_EQ("ipv6-only", lease.host_name);
}

TEST_F(DhcpDmTest, Ipv6PrefersGlobalThenUlaThenLinkLocal)
{
    add_host(1, "04:11:19:20:d4:02", "global");
    add_host(2, "04:11:19:20:d4:06", "ula");
    add_host(3, "04:11:19:20:d4:0a", "link-local");
    for (unsigned host = 1; host <= 3; ++host) {
        add_host_ipv6(host, "fe80::" + std::to_string(host));
        if (host <= 2) {
            add_host_ipv6(host, "fd00::" + std::to_string(host), 2);
        }
    }
    add_host_ipv6(1, "2001:db8::1", 3);
    const auto leases = parse_ipv6();
    ASSERT_EQ(3U, leases.size());
    EXPECT_EQ("2001:db8::1", leases.at("04:11:19:20:d4:02").ip_address);
    EXPECT_EQ("fd00::2", leases.at("04:11:19:20:d4:06").ip_address);
    EXPECT_EQ("fe80::3", leases.at("04:11:19:20:d4:0a").ip_address);
}

TEST_F(DhcpDmTest, Ipv6SelectionUsesNumericAddressNotInstanceOrTextOrder)
{
    add_host(1, "04:11:19:20:d4:02", "");
    add_host_ipv6(1, "2001:db8::10", 1);
    add_host_ipv6(1, "2001:db8::9", 10);
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    EXPECT_EQ("2001:db8::9", leases.begin()->second.ip_address);
    EXPECT_TRUE(leases.begin()->second.host_name.empty());
}

TEST_F(DhcpDmTest, Ipv6RejectsInvalidAndNonUnicastAddresses)
{
    add_host(1, "04:11:19:20:d4:02", "");
    const std::vector<std::string> invalid = {"",
                                              "0",
                                              "192.168.1.2",
                                              "::",
                                              "::1",
                                              "ff02::1",
                                              "::ffff:192.168.1.2",
                                              "::192.168.1.2",
                                              "fec0::1",
                                              "2001:db8::wrong",
                                              "fe80::1%br-lan"};
    unsigned index                         = 0;
    for (const auto &ip : invalid) {
        add_host_ipv6(1, ip, ++index);
    }
    EXPECT_TRUE(parse_ipv6().empty());
}

TEST_F(DhcpDmTest, Ipv6RejectsConflictingDiscoveryOwnershipAcrossCanonicalAddresses)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    add_host(2, "04:11:19:20:d4:06", "two");
    add_host_ipv6(1, "2001:db8::1");
    add_host_ipv6(2, "2001:0DB8:0:0:0:0:0:1");
    add_host_ipv6(1, "fe80::2", 2);
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    EXPECT_EQ("fe80::2", leases.at("04:11:19:20:d4:02").ip_address);
}

TEST_F(DhcpDmTest, Ipv6RepeatedDiscoveryForSameMacIsNotAConflict)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    add_host(2, "04:11:19:20:D4:02", "one");
    add_host_ipv6(1, "fe80::1");
    add_host_ipv6(2, "fe80::1");
    EXPECT_EQ(1U, parse_ipv6().size());
}

TEST_F(DhcpDmTest, Ipv6HonorsUnusableDiscoveryStatusWhenExposed)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    add_host_ipv6(1, "2001:db8::1", 1, "error");
    add_host_ipv6(1, "fd00::1", 2, "not reachable");
    add_host_ipv6(1, "fe80::1", 3, "reachable");
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    EXPECT_EQ("fe80::1", leases.begin()->second.ip_address);
}

TEST_F(DhcpDmTest, Ipv6DhcpUsesCsvReferenceAcrossPoolsWithoutNeedingDuid)
{
    add_host(7, "04:11:19:20:D4:02", "dhcp6");
    set_dhcp_ref(7, "Device.DHCPv4.Server.Pool.1.Client.1, "
                    "Device.DHCPv6.Server.Pool.2.Client.3");
    add_dhcp_ipv6("Device." + add_ipv6_client(3, true, 2), "2001:DB8::5");
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    const auto &lease = leases.at("04:11:19:20:d4:02");
    EXPECT_EQ("2001:db8::5", lease.ip_address);
    EXPECT_EQ("dhcp6", lease.host_name);
    EXPECT_EQ(std::chrono::hours(1), lease.validity);
}

TEST_F(DhcpDmTest, Ipv6DhcpDoesNotGuessMacFromDuidOrSourceAddress)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    const auto path = add_ipv6_client(1);
    ipv6_clients[path].add_child("DUID", "0003000104111920d402");
    ipv6_clients[path].add_child("SourceAddress", "fe80::611:19ff:fe20:d402");
    add_dhcp_ipv6(path, "2001:db8::1");
    EXPECT_TRUE(parse_ipv6().empty());
}

TEST_F(DhcpDmTest, Ipv6DhcpRejectsInactiveAndAmbiguouslyReferencedClients)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    add_host(2, "04:11:19:20:d4:06", "two");
    const auto path = add_ipv6_client(1, false);
    set_dhcp_ref(1, path);
    add_dhcp_ipv6(path, "2001:db8::1");
    EXPECT_TRUE(parse_ipv6().empty());
    ipv6_clients[path].find_child("Active")->set(true);
    set_dhcp_ref(2, path);
    EXPECT_TRUE(parse_ipv6().empty());
}

TEST_F(DhcpDmTest, Ipv6DhcpOwnershipOverridesConflictingHostsDiscovery)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    add_host(2, "04:11:19:20:d4:06", "two");
    const auto path = add_ipv6_client(1);
    set_dhcp_ref(1, path);
    add_dhcp_ipv6(path, "2001:db8::5");
    add_host_ipv6(1, "2001:db8::1");
    add_host_ipv6(2, "2001:db8::5");
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    EXPECT_EQ("2001:db8::5", leases.at("04:11:19:20:d4:02").ip_address);
}

TEST_F(DhcpDmTest, Ipv6DhcpRejectsDuplicateLeaseOwnership)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    add_host(2, "04:11:19:20:d4:06", "two");
    for (unsigned host = 1; host <= 2; ++host) {
        const auto path = add_ipv6_client(host);
        set_dhcp_ref(host, path);
        add_dhcp_ipv6(path, "2001:db8::1");
        add_host_ipv6(host, "2001:db8::1");
    }
    EXPECT_TRUE(parse_ipv6().empty());
}

TEST_F(DhcpDmTest, Ipv6ExpiredDhcpAddressCannotReturnThroughDiscovery)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    const auto path = add_ipv6_client(1);
    set_dhcp_ref(1, path);
    add_dhcp_ipv6(path, "2001:db8::1", "2026-10-23T08:00:00Z");
    add_host_ipv6(1, "2001:db8::1");
    EXPECT_TRUE(parse_ipv6().empty());
}

TEST_F(DhcpDmTest, Ipv6PrefersNonDeprecatedAddressAndThenLatestValidity)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    const auto path = add_ipv6_client(1);
    set_dhcp_ref(1, path);
    add_dhcp_ipv6(path, "2001:db8::1", "2026-10-23T09:00:00Z", 1, "2026-10-23T07:59:59Z");
    add_dhcp_ipv6(path, "fd00::2", "2026-10-23T08:30:00Z", 2);
    add_dhcp_ipv6(path, "fd00::3", "2026-10-23T09:00:00Z", 3);
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    EXPECT_EQ("fd00::3", leases.begin()->second.ip_address);
}

TEST_F(DhcpDmTest, Ipv6UnknownAndInfiniteLifetimesAreSupported)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    const auto path = add_ipv6_client(1);
    set_dhcp_ref(1, path);
    add_dhcp_ipv6(path, "fd00::1", "0001-01-01T00:00:00Z");
    const auto unknown = parse_ipv6();
    ASSERT_EQ(1U, unknown.size());
    EXPECT_EQ(std::chrono::milliseconds(0), unknown.begin()->second.validity);
    add_dhcp_ipv6(path, "fd00::2", "9999-12-31T23:59:59Z", 2);
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    EXPECT_EQ("fd00::2", leases.begin()->second.ip_address);
    EXPECT_GT(leases.begin()->second.validity, std::chrono::hours(24));
}

TEST_F(DhcpDmTest, Ipv6LifetimeSupportsTypedTimestampsAndTimezoneOffsets)
{
    add_host(1, "04:11:19:20:d4:02", "one");
    const auto path = add_ipv6_client(1);
    set_dhcp_ref(1, path);
    add_dhcp_ipv6(path, "fd00::1");
    const std::string expiry = "2026-10-23T11:00:00+02:00";
    amxc_ts_t timestamp{};
    ASSERT_EQ(0, amxc_ts_parse(&timestamp, expiry.c_str(), expiry.size()));
    amxc_var_t value;
    amxc_var_init(&value);
    amxc_var_set_amxc_ts_t(&value, &timestamp);
    ipv6_clients[path + "IPv6Address.1."].find_child("ValidLifetime")->set(&value);
    amxc_var_clean(&value);
    const auto leases = parse_ipv6();
    ASSERT_EQ(1U, leases.size());
    EXPECT_EQ(std::chrono::hours(1), leases.begin()->second.validity);
}
} // namespace
