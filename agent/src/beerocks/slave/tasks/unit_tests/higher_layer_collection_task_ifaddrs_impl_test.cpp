/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "higher_layer_collection_task_ifaddrs_impl.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <bcl/network/network_utils.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <unordered_map>

namespace {

/**
 * @brief Executes a system command and redirects output to /dev/null.
 * @param cmd The shell command to execute.
 * @return int The exit status of the command.
 */
int run_cmd(const std::string &cmd) { return std::system((cmd + " >/dev/null 2>&1").c_str()); }

/**
 * @brief RAII helper class to ensure a network interface is deleted when the test ends.
 */
class ScopedInterfaceCleanup {
public:
    /**
     * @brief Construct a new Scoped Interface Cleanup object.
     * @param ifname The name of the network interface to clean up.
     */
    explicit ScopedInterfaceCleanup(std::string ifname) : m_ifname(std::move(ifname)) {}

    /**
     * @brief Destroy the Scoped Interface Cleanup object and delete the interface.
     */
    ~ScopedInterfaceCleanup()
    {
        if (!m_ifname.empty()) {
            std::ignore = run_cmd("ip link del " + m_ifname);
        }
    }

private:
    std::string m_ifname; ///< Name of the interface to be removed.
};

/**
 * @brief Checks if an IPv6 address consists only of zeros.
 * @param addr Array of 16 bytes representing the IPv6 address.
 * @return true if the address is all zeros (::), false otherwise.
 */
bool is_zero_ipv6(const uint8_t addr[16])
{
    return std::all_of(addr, addr + 16, [](uint8_t oct) { return oct == 0; });
}

/**
 * @brief Checks if the provided interface status contains a specific IPv4 address.
 * @param status The network status structure to search in.
 * @param ipv4 The IPv4 address in string format (e.g., "192.168.1.1").
 * @return true if the address is found in the status list, false otherwise.
 */
bool has_ipv4(const beerocks::HigherLayerCollectionTask::sInterfaceNetworkStatus &status,
              const std::string &ipv4)
{
    struct in_addr expected = {};
    if (inet_pton(AF_INET, ipv4.c_str(), &expected) != 1) {
        return false;
    }

    return std::any_of(
        status.ipv4_list.begin(), status.ipv4_list.end(),
        [&](const beerocks::HigherLayerCollectionTask::sInterfaceNetworkStatus::sIpv4Entry &entry) {
            // ipv4_address is in host byte order; inet_pton gives network byte order.
            return entry.ipv4_address == ntohl(expected.s_addr);
        });
}

/**
 * @brief Checks if the provided interface status contains a specific IPv6 address.
 * @param status The network status structure to search in.
 * @param ipv6 The IPv6 address in string format (e.g., "2001:db8::10").
 * @return true if the address is found in the status list, false otherwise.
 */
bool has_ipv6(const beerocks::HigherLayerCollectionTask::sInterfaceNetworkStatus &status,
              const std::string &ipv6)
{
    uint8_t expected[16] = {};
    if (inet_pton(AF_INET6, ipv6.c_str(), expected) != 1) {
        return false;
    }

    return std::any_of(
        status.ipv6_list.begin(), status.ipv6_list.end(),
        [&](const beerocks::HigherLayerCollectionTask::sInterfaceNetworkStatus::sIpv6Entry &entry) {
            return std::memcmp(entry.ipv6_address, expected, sizeof(expected)) == 0;
        });
}

/**
 * @test
 * @brief Integration test for refresh() method.
 * This test creates a dummy network interface, assigns IPv4/IPv6 addresses using the 'ip' command,
 * and verifies that HigherLayerCollectionTaskIfAddrsImpl::refresh() correctly retrieves
 * these addresses from the kernel via getifaddrs().
 */
TEST(HigherLayerCollectionTaskIfAddrsImplIntegrationTest, refresh_reads_kernel_addresses)
{
    if (run_cmd("command -v ip") != 0) {
        GTEST_SKIP() << "'ip' command is not available";
    }

    const std::string ifname = "hlc" + std::to_string(getpid() % 100000);
    std::ignore              = run_cmd("ip link del " + ifname);
    ScopedInterfaceCleanup cleanup(ifname);

    if (run_cmd("ip link add " + ifname + " type dummy") != 0) {
        GTEST_SKIP() << "No permission to create test interface (requires NET_ADMIN)";
    }

    ASSERT_EQ(0, run_cmd("ip link set dev " + ifname + " up"));
    ASSERT_EQ(0, run_cmd("ip addr add 192.0.2.10/24 dev " + ifname));
    ASSERT_EQ(0, run_cmd("ip -6 addr add 2001:db8::10/64 dev " + ifname));

    beerocks::HigherLayerCollectionTaskIfAddrsImpl provider;
    std::unordered_map<std::string, beerocks::HigherLayerCollectionTask::sInterfaceNetworkStatus>
        interfaces_network_status;

    ASSERT_TRUE(provider.refresh(interfaces_network_status));

    // Debug output to verify collected data
    for (const auto &iface : interfaces_network_status) {
        const auto &ifname = iface.first;
        const auto &status = iface.second;

        std::cout << "\n[DEBUG] Interface: " << ifname << std::endl;
        std::cout << "  MAC: " << status.mac_address << std::endl;

        for (const auto &ipv4 : status.ipv4_list) {
            // ipv4_address is in host byte order; inet_ntop expects network byte order.
            char ipv4_str[INET_ADDRSTRLEN];
            struct in_addr ipv4_addr = {};
            ipv4_addr.s_addr         = htonl(ipv4.ipv4_address);
            inet_ntop(AF_INET, &ipv4_addr, ipv4_str, sizeof(ipv4_str));
            struct in_addr dhcp_addr = {};
            dhcp_addr.s_addr         = htonl(ipv4.ipv4_dhcp_server);
            char dhcp_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &dhcp_addr, dhcp_str, sizeof(dhcp_str));
            std::cout << "  IPv4: " << ipv4_str << " (Type: " << (int)ipv4.ipv4_address_type
                      << ", DHCP Srv: " << dhcp_str << ")" << std::endl;
        }

        char ipv6_str[INET6_ADDRSTRLEN];
        if (inet_ntop(AF_INET6, status.ipv6_link_local, ipv6_str, sizeof(ipv6_str))) {
            std::cout << "  IPv6 Link-Local: " << ipv6_str << std::endl;
        }

        for (const auto &ipv6 : status.ipv6_list) {
            if (inet_ntop(AF_INET6, ipv6.ipv6_address, ipv6_str, sizeof(ipv6_str))) {
                std::cout << "  IPv6: " << ipv6_str << " (Type: " << (int)ipv6.ipv6_address_type
                          << ")" << std::endl;
            }
        }
    }

    auto it = interfaces_network_status.find(ifname);
    ASSERT_NE(it, interfaces_network_status.end());

    const auto &status = it->second;
    EXPECT_TRUE(has_ipv4(status, "192.0.2.10"));
    EXPECT_TRUE(has_ipv6(status, "2001:db8::10"));
    EXPECT_FALSE(is_zero_ipv6(status.ipv6_link_local));
}

} // namespace
