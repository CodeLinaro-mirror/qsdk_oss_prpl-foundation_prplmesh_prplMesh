/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "prplmesh_cli.h"

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
};

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

float prplmesh_cli::get_freq_from_class(const uint32_t oper_class)
{
    float freq;

    if ((oper_class >= 1 && oper_class <= 5)) {
        freq = 0.902;
    } else if (oper_class == 6 || oper_class == 17 || oper_class == 19 ||
               (oper_class >= 66 && oper_class <= 67)) {
        freq = 0.863;
    } else if (oper_class == 8 || oper_class == 73) {
        freq = 0.9165;
    } else if ((oper_class >= 14 && oper_class <= 16) || (oper_class >= 73 && oper_class <= 76)) {
        freq = 0.9175;
    } else if (oper_class == 18 || (oper_class >= 20 && oper_class <= 29) ||
               (oper_class >= 68 && oper_class <= 72)) {
        freq = 0.902;
    } else if (oper_class == 30 || oper_class == 77) {
        freq = 0.9014;
    } else if (oper_class == 81 || oper_class == 83 || oper_class == 84) {
        freq = 2.407;
    } else if (oper_class == 82) {
        freq = 2.414;
    } else if ((oper_class >= 94 && oper_class <= 95) || (oper_class >= 109 && oper_class <= 110)) {
        freq = 3.00;
    } else if (oper_class == 96) {
        freq = 3.0025;
    } else if (oper_class == 101) {
        freq = 4.85;
    } else if (oper_class == 102) {
        freq = 4.89;
    } else if (oper_class == 103) {
        freq = 4.9375;
    } else if (oper_class >= 104 && oper_class <= 107) {
        freq = 4.00;
    } else if (oper_class == 108 || oper_class == 111) {
        freq = 4.0025;
    } else if (oper_class >= 115 && oper_class <= 130) {
        freq = 5.00;
    } else if (oper_class >= 131 && oper_class <= 136) {
        freq = 6.00;
    } else if (oper_class >= 180 && oper_class <= 181) {
        freq = 56.16;
    } else if (oper_class == 182) {
        freq = 56.70;
    } else if (oper_class == 183) {
        freq = 42.66;
    } else if (oper_class == 184) {
        freq = 47.52;
    } else if (oper_class == 185) {
        freq = 42.93;
    } else if (oper_class == 186) {
        freq = 47.79;
    } else {
        freq = 0.00;
    }

    return freq;
}

bool prplmesh_cli::print_radio(std::string device_path)
{
    std::string radio_ht_path     = device_path + "Radio.*.";
    const amxc_htable_t *ht_radio = m_amx_client->get_htable_object(radio_ht_path);
    int radio_index               = 1;

    amxc_htable_iterate(radio_it, ht_radio)
    {
        const char *radio_key     = amxc_htable_it_get_key(radio_it);
        std::string radio_path_i  = std::string(radio_key);
        amxc_var_t *radio_obj     = amxc_var_from_htable_it(radio_it);
        std::string radio_name    = GET_CHAR(radio_obj, "X_PRPLWARE-COM_Name");
        std::string curr_op_class = radio_path_i + "CurrentOperatingClassProfile." + "*.";
        amxc_var_t *op_class_obj  = m_amx_client->get_object(curr_op_class);
        conn_map.radio_id         = GET_CHAR(radio_obj, "ID");
        conn_map.channel          = GET_UINT32(op_class_obj, "Channel");
        conn_map.oper_class       = GET_UINT32(op_class_obj, "Class");
        float freq                = get_freq_from_class(conn_map.oper_class);

        const std::string radio_label = radio_name.empty()
                                            ? "RADIO[" + std::to_string(radio_index) + "]"
                                            : "RADIO: " + radio_name;

        std::cout << space << "\t" << radio_label << " mac: " << conn_map.radio_id
                  << ", ch: " << conn_map.channel << ", freq: " << freq << "GHz" << std::endl;

        std::string bss_ht_path     = radio_path_i + "BSS.*.";
        const amxc_htable_t *ht_bss = m_amx_client->get_htable_object(bss_ht_path);
        int vap_index               = 0;

        amxc_htable_iterate(bss_it, ht_bss)
        {
            const char *bss_key    = amxc_htable_it_get_key(bss_it);
            std::string bss_path_i = std::string(bss_key);
            amxc_var_t *bss_obj    = amxc_var_from_htable_it(bss_it);
            conn_map.bss_id        = GET_CHAR(bss_obj, "BSSID");
            conn_map.ssid          = GET_CHAR(bss_obj, "SSID");

            std::cout << space << "\t\tVAP[" << vap_index << "]: bssid: " << conn_map.bss_id
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
