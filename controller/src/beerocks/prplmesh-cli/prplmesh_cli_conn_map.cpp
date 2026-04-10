/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "prplmesh_cli.h"

#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <iostream>
#include <map>
#include <string>

namespace beerocks {
namespace prplmesh_api {

namespace {

static prplmesh_cli::conn_map_t conn_map;
static std::string space;

struct conn_map_device_t {
    std::string dm_path;
    std::string id;
    std::string parent_id;
    std::string link_type;
    std::string backhaul_mac;
};

struct selected_radio_profile_t {
    uint32_t primary_channel           = 0;
    beerocks::eWiFiBandwidth bandwidth = beerocks::BANDWIDTH_UNKNOWN;
    beerocks::eFreqType freq_type      = beerocks::FREQ_UNKNOWN;
};

int bandwidth_rank(beerocks::eWiFiBandwidth bandwidth)
{
    switch (bandwidth) {
    case beerocks::BANDWIDTH_20:
        return 1;
    case beerocks::BANDWIDTH_40:
        return 2;
    case beerocks::BANDWIDTH_80:
        return 3;
    case beerocks::BANDWIDTH_160:
        return 4;
    case beerocks::BANDWIDTH_320:
    case beerocks::BANDWIDTH_320_1:
    case beerocks::BANDWIDTH_320_2:
        return 5;
    default:
        return 0;
    }
}

selected_radio_profile_t select_radio_profile(beerocks::prplmesh_amx::AmxClient &amx_client,
                                              const std::string &radio_path)
{
    selected_radio_profile_t selected_profile;
    std::string curr_op_class_path = radio_path + "CurrentOperatingClassProfile.*.";
    const amxc_htable_t *ht_op     = amx_client.get_htable_object(curr_op_class_path);

    uint32_t primary_op_class                  = 0;
    beerocks::eWiFiBandwidth highest_bandwidth = beerocks::BANDWIDTH_UNKNOWN;
    beerocks::eFreqType highest_freq_type      = beerocks::FREQ_UNKNOWN;

    amxc_htable_iterate(op_it, ht_op)
    {
        amxc_var_t *op_obj = amxc_var_from_htable_it(op_it);
        auto channel       = GET_UINT32(op_obj, "Channel");
        auto op_class      = GET_UINT32(op_obj, "Class");

        if (channel == 0 || op_class == 0) {
            continue;
        }

        auto bandwidth =
            son::wireless_utils::get_bandwidth_from_channel_and_op_class(channel, op_class);
        auto freq_type = son::wireless_utils::which_freq_op_cls(op_class);
        if (bandwidth == beerocks::BANDWIDTH_UNKNOWN || freq_type == beerocks::FREQ_UNKNOWN) {
            continue;
        }

        if (selected_profile.primary_channel == 0 ||
            bandwidth_rank(bandwidth) <
                bandwidth_rank(son::wireless_utils::get_bandwidth_from_channel_and_op_class(
                    selected_profile.primary_channel, primary_op_class))) {
            selected_profile.primary_channel = channel;
            primary_op_class                 = op_class;
        }

        if (bandwidth_rank(bandwidth) > bandwidth_rank(highest_bandwidth)) {
            highest_bandwidth = bandwidth;
            highest_freq_type = freq_type;
        }
    }

    selected_profile.bandwidth = highest_bandwidth;
    selected_profile.freq_type = highest_freq_type;
    return selected_profile;
}

void print_conn_map_subtree(prplmesh_cli &cli,
                            const std::map<std::string, conn_map_device_t> &devices_by_id,
                            const std::multimap<std::string, std::string> &children_by_parent,
                            const std::string &parent_id, const std::string &indent)
{
    auto range = children_by_parent.equal_range(parent_id);
    for (auto child_it = range.first; child_it != range.second; ++child_it) {
        auto device_it = devices_by_id.find(child_it->second);
        if (device_it == devices_by_id.end()) {
            continue;
        }

        const auto &device = device_it->second;

        if (device.link_type == "Ethernet" && !device.backhaul_mac.empty()) {
            std::cout << indent << "Eth_BACKHAUL: mac: " << device.backhaul_mac << std::endl;
        } else if (device.link_type == "Wi-Fi" && !device.backhaul_mac.empty()) {
            std::cout << indent << "WiFi_BACKHAUL: mac: " << device.backhaul_mac << std::endl;
        }

        conn_map.device_index++;
        std::cout << indent << "Device[" << conn_map.device_index
                  << "]: name: Agent, mac: " << device.id << " LinkType: " << device.link_type
                  << std::endl;

        space = indent;
        cli.print_radio(device.dm_path);

        print_conn_map_subtree(cli, devices_by_id, children_by_parent, device.id, indent + "\t");
    }
}

} // namespace

bool prplmesh_cli::get_ip_from_iface(const std::string &iface, std::string &ip)
{
    int fd;
    struct ifreq ifr;

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        LOG(ERROR) << "Can't open SOCK_DGRAM socket.";
        return false;
    }

    ifr.ifr_addr.sa_family = AF_INET;
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface.c_str());

    if (ioctl(fd, SIOCGIFADDR, &ifr) == -1) {
        LOG(ERROR) << "SIOCGIFADDR";
        close(fd);
        return false;
    }

    close(fd);
    ip = std::string(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

    return true;
}

bool prplmesh_cli::print_radio(std::string device_path)
{
    std::string radio_ht_path     = device_path + "Radio.*.";
    const amxc_htable_t *ht_radio = m_amx_client->get_htable_object(radio_ht_path);
    int radio_index               = 1;

    amxc_htable_iterate(radio_it, ht_radio)
    {
        const char *radio_key    = amxc_htable_it_get_key(radio_it);
        std::string radio_path_i = std::string(radio_key);
        amxc_var_t *radio_obj    = amxc_var_from_htable_it(radio_it);
        std::string radio_name   = GET_CHAR(radio_obj, "X_PRPLWARE-COM_Name");
        conn_map.radio_id        = GET_CHAR(radio_obj, "ID");

        auto selected_profile = select_radio_profile(*m_amx_client, radio_path_i);
        conn_map.channel      = selected_profile.primary_channel;
        uint16_t freq         = 0;
        if (selected_profile.primary_channel != 0 &&
            selected_profile.freq_type != beerocks::FREQ_UNKNOWN) {
            freq = son::wireless_utils::channel_to_freq(int(selected_profile.primary_channel),
                                                        selected_profile.freq_type);
        }

        const bool has_radio_name = !radio_name.empty() && radio_name != "N/A";
        const std::string radio_label =
            !has_radio_name ? "RADIO[" + std::to_string(radio_index) + "]" : "RADIO: " + radio_name;

        std::cout << space << "\t" << radio_label << " mac: " << conn_map.radio_id
                  << ", ch: " << conn_map.channel;
        if (selected_profile.bandwidth != beerocks::BANDWIDTH_UNKNOWN) {
            std::cout << ", bw: "
                      << beerocks::utils::convert_bandwidth_to_string(selected_profile.bandwidth);
        }
        std::cout << ", freq: " << freq << "MHz" << std::endl;

        std::string bss_ht_path     = radio_path_i + "BSS.*.";
        const amxc_htable_t *ht_bss = m_amx_client->get_htable_object(bss_ht_path);
        int vap_index               = 0; // fallback display index for VAP[n]

        amxc_htable_iterate(bss_it, ht_bss)
        {
            const char *bss_key    = amxc_htable_it_get_key(bss_it);
            std::string bss_path_i = std::string(bss_key);
            amxc_var_t *bss_obj    = amxc_var_from_htable_it(bss_it);
            conn_map.bss_id        = GET_CHAR(bss_obj, "BSSID");
            conn_map.ssid          = GET_CHAR(bss_obj, "SSID");
            const auto vap_id      = GET_INT32(bss_obj, "X_PRPLWARE-COM_VAPID");

            std::string vap_label = "VAP[" + std::to_string(vap_index) + "]";
            if (has_radio_name && vap_id >= 0) {
                vap_label = radio_name + "." + std::to_string(vap_id);
            }

            std::cout << space << "\t\t" << vap_label << ": bssid: " << conn_map.bss_id
                      << ", ssid: " << conn_map.ssid << std::endl;

            std::string sta_ht_path     = bss_path_i + "STA.*.";
            const amxc_htable_t *ht_sta = m_amx_client->get_htable_object(sta_ht_path);
            int sta_index               = 1;
            amxc_htable_iterate(sta_it, ht_sta)
            {
                amxc_var_t *sta_obj      = amxc_var_from_htable_it(sta_it);
                std::string sta_mac      = GET_CHAR(sta_obj, "MACAddress");
                std::string sta_hostname = GET_CHAR(sta_obj, "Hostname");
                std::string sta_ipv4     = GET_CHAR(sta_obj, "IPV4Address");

                std::cout << space << "\t\t\tCLIENT[" << sta_index << "]: name: " << sta_hostname
                          << " mac: " << sta_mac << " ipv4: " << sta_ipv4 << std::endl;
                sta_index++;
            }
            vap_index++;
        }
        radio_index++;
    }
    return true;
}

bool prplmesh_cli::prpl_conn_map()
{
    conn_map.device_index = 1;
    space.clear();

    std::cout << "Start conn map" << std::endl;

    std::string network_path = DATAELEMENTS_ROOT_DM ".Network.";
    amxc_var_t *network_obj  = m_amx_client->get_object(network_path);
    conn_map.controller_id   = GET_CHAR(network_obj, "ControllerID");
    conn_map.device_number   = GET_UINT32(network_obj, "DeviceNumberOfEntries");

    std::cout << "Found " << conn_map.device_number << " devices" << std::endl;

    if (!prplmesh_cli::get_ip_from_iface(BRIDGE_IFACE, conn_map.bridge_ip_v4)) {
        LOG(ERROR) << "Can't get bridge ip.";
    }

    std::cout << "Device[1]: name: GW_MASTER, mac: " << conn_map.controller_id
              << ", ipv4: " << conn_map.bridge_ip_v4 << std::endl;

    const amxc_htable_t *devices = m_amx_client->get_htable_object(conn_map.device_ht_path);
    std::map<std::string, conn_map_device_t> devices_by_id;
    std::multimap<std::string, std::string> children_by_parent;

    amxc_htable_iterate(device_it, devices)
    {
        const char *key         = amxc_htable_it_get_key(device_it);
        std::string device_path = std::string(key);
        amxc_var_t *device_obj  = amxc_var_from_htable_it(device_it);
        std::string device_id   = GET_CHAR(device_obj, "ID");

        conn_map_device_t device;
        device.dm_path = device_path;
        device.id      = device_id;

        auto backhaul_path       = device_path + "MultiAPDevice.Backhaul.";
        auto backhaul_obj        = m_amx_client->get_object(backhaul_path);
        device.parent_id         = GET_CHAR(backhaul_obj, "BackhaulDeviceID");
        device.link_type         = GET_CHAR(backhaul_obj, "LinkType");
        device.backhaul_mac      = GET_CHAR(backhaul_obj, "BackhaulMACAddress");
        devices_by_id[device.id] = device;

        if (!device.parent_id.empty()) {
            children_by_parent.emplace(device.parent_id, device.id);
        }
    }

    auto controller_it = devices_by_id.find(conn_map.controller_id);
    if (controller_it != devices_by_id.end()) {
        print_radio(controller_it->second.dm_path);
    }

    print_conn_map_subtree(*this, devices_by_id, children_by_parent, conn_map.controller_id, "\t");

    return true;
}

} // namespace prplmesh_api
} // namespace beerocks
