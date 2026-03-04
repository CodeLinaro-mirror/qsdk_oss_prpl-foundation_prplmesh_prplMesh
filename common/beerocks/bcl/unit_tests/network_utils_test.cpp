/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/network/network_utils.h>

#include <dirent.h>
#include <net/if.h>

#include <gtest/gtest.h>

namespace {

std::vector<std::string> get_iface_names()
{

    std::vector<std::string> iface_names;

    const char *path = "/sys/class/net";

    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            std::string iface_name = entry->d_name;
            if (iface_name == "." || iface_name == "..") {
                continue;
            }
            iface_names.push_back(iface_name);
        }
        closedir(dir);
    }

    return iface_names;
}

TEST(NetworkUtilsTest, get_iface_stats_should_succeed)
{
    beerocks::net::sInterfaceStats iface_stats;

    for (const auto &iface_name : get_iface_names()) {
        ASSERT_TRUE(beerocks::net::network_utils::get_iface_stats(iface_name, iface_stats));

        std::cout << iface_name << ":" << std::endl;
        std::cout << "\ttx_bytes: " << std::to_string(iface_stats.tx_bytes) << std::endl;
        std::cout << "\ttx_packets: " << std::to_string(iface_stats.tx_packets) << std::endl;
        std::cout << "\ttx_errors: " << std::to_string(iface_stats.tx_errors) << std::endl;
        std::cout << "\trx_bytes: " << std::to_string(iface_stats.rx_bytes) << std::endl;
        std::cout << "\trx_packets: " << std::to_string(iface_stats.rx_packets) << std::endl;
        std::cout << "\trx_errors: " << std::to_string(iface_stats.rx_errors) << std::endl;
    }
}

TEST(NetworkUtilsTest, build_vlan_interface_name_should_shorten_wlan_and_sta_tokens)
{
    const auto name = beerocks::net::network_utils::build_vlan_interface_name("wlan4.8.sta192", 200,
                                                                              std::string{});
    EXPECT_EQ("w4.8.s192.200", name);
}

TEST(NetworkUtilsTest, build_vlan_interface_name_should_use_explicit_suffix)
{
    const auto name =
        beerocks::net::network_utils::build_vlan_interface_name("wlan4.8.sta192", 200, "guest");
    EXPECT_EQ("w4.8.s192.guest", name);
}

TEST(NetworkUtilsTest, build_vlan_interface_name_should_keep_eth_iface_name)
{
    const auto name = beerocks::net::network_utils::build_vlan_interface_name("eth0", 20);
    EXPECT_EQ("eth0.20", name);
}

TEST(NetworkUtilsTest, build_vlan_interface_name_should_keep_lan_iface_name)
{
    const auto name = beerocks::net::network_utils::build_vlan_interface_name("lan1", 20);
    EXPECT_EQ("lan1.20", name);
}

TEST(NetworkUtilsTest, build_vlan_interface_name_should_fail_on_invalid_vid)
{
    const auto name = beerocks::net::network_utils::build_vlan_interface_name("wlan4.8.sta192", 0);
    EXPECT_TRUE(name.empty());
}

TEST(NetworkUtilsTest, build_vlan_interface_name_should_fail_when_exceeding_ifnam_limit)
{
    const std::string iface(IFNAMSIZ, 'a');
    const auto name = beerocks::net::network_utils::build_vlan_interface_name(iface, 200);
    EXPECT_TRUE(name.empty());
}

} // namespace
