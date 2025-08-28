/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/beerocks_utils.h>
#include <bcl/network/network_utils.h>
#include <btl/broker_client_factory_factory.h>

#include <bcl/network/network_utils.h>

#include "agent_db.h"
#include "traffic_separation.h"

constexpr char DOT_PVID_SUFFIX[] = ".pvid";
#define PVID_SUFFIX &DOT_PVID_SUFFIX[1]

int beerocks::net::TrafficSeparation::m_profile_x_disallow_override_unsupported_configuration = 0;
namespace beerocks {
namespace net {

TrafficSeparation::TrafficSeparation(std::shared_ptr<btl::BrokerClient> broker_client)
    : m_broker_client(broker_client)
{
}

void TrafficSeparation::clear_configuration()
{
    LOG(DEBUG) << "Clearing traffic separation policy!";

    auto db = AgentDB::get();

    for (auto &radio : db->get_radios_list()) {
        for (uint8_t bss_id = 0; bss_id < radio->front.bssids.size(); bss_id++) {
            auto &bss      = radio->front.bssids[bss_id];
            auto &ssid_map = db->traffic_separation.ssid_vid_mapping;
            auto found_it  = ssid_map.find(bss.ssid);
            if (found_it == ssid_map.end()) {
                continue;
            }

            // consider vap_id shift when main vap_id is greater than 0
            // (case of glinet where main vap is wlan0-1)
            auto iface_ids = utils::get_ids_from_iface_string(radio->front.iface_name);
            if (iface_ids.vap_id >= IFACE_VAP_ID_MIN) {
                bss_id += iface_ids.vap_id;
            }

            // TODO: Save the bss iface name on the database instead of using bss ID.
            auto bss_iface_name =
                utils::get_iface_string_from_iface_vap_ids(radio->front.iface_name, bss_id);
            auto vid = found_it->second;

            // Remove VLAN packet filter.
            network_utils::set_vlan_packet_filter(false, bss_iface_name, vid);
        }
    }

    for (auto &eth_port : db->ethernet.lan) {
        network_utils::set_vlan_packet_filter(false, eth_port.iface_name);
    }
    network_utils::set_vlan_packet_filter(false, db->ethernet.wan.iface_name);

    db->traffic_separation.primary_vlan_id = 0;
    db->traffic_separation.secondary_vlans_ids.clear();
    db->traffic_separation.ssid_vid_mapping.clear();
    network_utils::set_vlan_filtering(db->bridge.iface_name, 0);

    // Remove the Primary Vlan configuration in Transport process
    if (!m_broker_client->configure_primary_vlan_id(0, false)) {
        LOG(ERROR) << "Failed configuring transport process!";
    }

    // Reset the transport monitoring on bridge interfaces
    if (!m_broker_client->configure_interfaces(db->bridge.iface_name, {}, true, true)) {
        LOG(ERROR) << "Failed configuring transport process!";
    }
}

void TrafficSeparation::apply_policy(const std::string &radio_iface)
{
    const std::string BH = "wlan1", VAP_1 = "wlan0.1", VAP_2 = "wlan0.2";

    const std::vector<std::string> cmds = {
        R"(ip link set wlan0.1 down 2>/dev/null || true)",
        R"(ip link set wlan0.2 down 2>/dev/null || true)",
        // R"(ip addr flush dev " + BH + " 2>/dev/null || true)",

        R"(brctl addbr br-lan   2>/dev/null || true)",
        R"(brctl addbr br-guest 2>/dev/null || true)", R"(brctl stp br-lan off)",
        R"(brctl stp br-guest off)",

        R"(ip link add link " + BH + " name " + BH + ".10 type vlan id 10 2>/dev/null || true)",
        R"(ip link add link " + BH + " name " + BH + ".20 type vlan id 20 2>/dev/null || true)",

        R"(ip link set " + BH + ".10 up)", R"(ip link set " + BH + ".20 up)",

        R"(brctl delif br-lan " + BH + "      || true)",
        R"(brctl addif br-lan " + BH + ".10      || true)",
        R"(brctl addif br-guest " + BH + ".20 || true)",

        R"(ip link set " + VAP_1 + " up)", R"(ip link set " + VAP_2 + " up)",
        R"(ip link set br-lan up)", R"(ip link set br-guest up)"};

    for (const auto &cmd : cmds) {
        beerocks::os_utils::system_call(cmd);
    }

    // // Since the following call is locking the database, thread safety is promised on this function.
    // auto db = AgentDB::get();

    // network_utils::set_vlan_filtering(db->bridge.iface_name,
    //                                   db->traffic_separation.primary_vlan_id);

    // // If the primary VID has changed to zero, vlan filtering is disabled, so there is no point
    // // modifying the VLAN policy on the platform interfaces.
    // if (db->traffic_separation.primary_vlan_id == 0) {
    //     return;
    // }

    // LOG(DEBUG) << "Apply traffic separation policy";

    // // Configure the Primary VLAN in Transport Process
    // if (!m_broker_client->configure_primary_vlan_id(db->traffic_separation.primary_vlan_id, true)) {
    //     LOG(ERROR) << "Failed configuring transport process!";
    // }

    // // The Bridge, the WAN ports and the LAN ports should all have "Tagged Port" policy.
    // // Update the Bridge Policy
    // bool is_bridge = true;
    // set_vlan_policy(db->bridge.iface_name, ePortMode::TAGGED_PORT_PRIMARY_UNTAGGED, is_bridge);

    // // Since we already set the bridge, and there are no more bridge interfaces, the 'bridge_iface'
    // // is set to 'false' from now on.
    // is_bridge = false;

    // // Update WAN and LAN Ports.
    // if (!db->device_conf.local_gw && !db->ethernet.wan.iface_name.empty()) {
    //     set_vlan_policy(db->ethernet.wan.iface_name, ePortMode::TAGGED_PORT_PRIMARY_UNTAGGED,
    //                     is_bridge);
    // }
    // for (const auto &lan_iface_info : db->ethernet.lan) {
    //     set_vlan_policy(lan_iface_info.iface_name, ePortMode::TAGGED_PORT_PRIMARY_UNTAGGED,
    //                     is_bridge);
    // }

    // // Wireless Backhaul
    // if (!db->device_conf.local_gw && !db->backhaul.selected_iface_name.empty() &&
    //     db->backhaul.connection_type == AgentDB::sBackhaul::eConnectionType::Wireless) {

    //     auto radio = db->radio(db->backhaul.selected_iface_name);
    //     if (!radio) {
    //         LOG(ERROR) << "Could not find Backhaul Radio interface!";
    //         return;
    //     }

    //     if (db->backhaul.bssid_multi_ap_profile > 1) {
    //         set_vlan_policy(radio->back.iface_name, ePortMode::TAGGED_PORT_PRIMARY_TAGGED,
    //                         is_bridge);
    //     } else {
    //         set_vlan_policy(radio->back.iface_name, ePortMode::UNTAGGED_PORT, is_bridge,
    //                         db->traffic_separation.primary_vlan_id);
    //     }
    // }

    // // If radio interface has not been given, then stop configuring the VLAN policy after finished
    // // to configure the bridge, ethernet ports and wireless backhaul interface.
    // // This should happen whenever the backhaul connects, and we need to update the Primary VLAN
    // // of the platform so we would be able to get messages from the Controller.
    // if (radio_iface.empty()) {
    //     return;
    // }

    // // Update Policy given Radio interface.
    // auto radio = db->radio(radio_iface);
    // if (!radio) {
    //     return;
    // }

    // for (auto &bss : radio->front.bssids) {
    //     // Skip unconfigured BSS.
    //     if (bss.ssid.empty()) {
    //         continue;
    //     }

    //     LOG(DEBUG) << "BSS " << bss.mac << ", ssid:" << bss.ssid << ", fBSS: " << bss.fronthaul_bss
    //                << ", bBSS: " << bss.backhaul_bss
    //                << ", p1_dis: " << bss.backhaul_bss_disallow_profile1_agent_association
    //                << ", p2_dis: " << bss.backhaul_bss_disallow_profile2_agent_association;

    //     std::string bss_iface;

    //     if (!network_utils::linux_iface_get_name(bss.mac, bss_iface)) {
    //         LOG(WARNING) << "Interface with MAC " << bss.mac << " does not exist";
    //         continue;
    //     }

    //     // Fronthaul-only BSS: set untagged VID based on SSID->VID mapping.
    //     if (bss.fronthaul_bss && !bss.backhaul_bss) {
    //         auto it = db->traffic_separation.ssid_vid_mapping.find(bss.ssid);
    //         if (it == db->traffic_separation.ssid_vid_mapping.end()) {
    //             LOG(INFO) << "SSID '" << bss.ssid << "' not found in SSID->VID map, skip.";
    //             continue;
    //         }
    //         set_vlan_policy(bss_iface, ePortMode::UNTAGGED_PORT, is_bridge, it->second);
    //     }
    //     // Backhaul-only BSS.
    //     else if (!bss.fronthaul_bss && bss.backhaul_bss) {
    //         if (bss.backhaul_bss_disallow_profile1_agent_association ==
    //             bss.backhaul_bss_disallow_profile2_agent_association) {
    //             LOG(WARNING) << "bBSS invalid configuration - "
    //                          << "profile1_disallow == profile2_disallow == "
    //                          << bss.backhaul_bss_disallow_profile1_agent_association;

    //             if (m_profile_x_disallow_override_unsupported_configuration == 0) {
    //                 continue;
    //             }
    //             LOG(DEBUG) << "profile_x_disallow_override is set on profile "
    //                        << m_profile_x_disallow_override_unsupported_configuration;

    //             //Overriding bBSS profile disallow configuration if m_profile_x_disallow_override_unsupported_configuration > 0
    //             bss.backhaul_bss_disallow_profile1_agent_association =
    //                 (m_profile_x_disallow_override_unsupported_configuration == 1);
    //             bss.backhaul_bss_disallow_profile2_agent_association =
    //                 (m_profile_x_disallow_override_unsupported_configuration == 2);
    //         }
    //         auto bss_iface_netdevs =
    //             network_utils::get_bss_ifaces(bss_iface, db->bridge.iface_name);

    //         for (const auto &iface_name : bss_iface_netdevs) {
    //             // Profile-2 backhaul BSS -> tagged primary + tagged secondary.
    //             if (bss.backhaul_bss_disallow_profile1_agent_association ||
    //                 m_profile_x_disallow_override_unsupported_configuration == 1) {
    //                 set_vlan_policy(iface_name, ePortMode::TAGGED_PORT_PRIMARY_TAGGED, is_bridge);
    //             }
    //             // Profile-1 backhaul BSS -> untagged primary VID.
    //             else {
    //                 set_vlan_policy(iface_name, ePortMode::UNTAGGED_PORT, is_bridge,
    //                                 db->traffic_separation.primary_vlan_id);
    //             }
    //         }
    //     }
    //     // Combined fBSS & bBSS - Currently Support only Profile-1 (PPM-1418)
    //     else {
    //         if (!bss.backhaul_bss_disallow_profile2_agent_association) {

    //             // Note: If Combined mode with profile 2 will be supported, need to create a VLAN
    //             // interface for it to support tagging on multicast messages.
    //             LOG(WARNING) << "bBSS invalid configuration! "
    //                          << "Combined BSS not supported with Profile-2 bBSS - Skip";
    //             continue;
    //         }
    //         if (bss.backhaul_bss_disallow_profile1_agent_association) {
    //             LOG(ERROR) << "bBSS invalid configuration! "
    //                        << "Profile-1 and Profile-2 Backhaul connection are both disallowed - "
    //                           "Skip";
    //             continue;
    //         }

    //         set_vlan_policy(bss_iface, ePortMode::UNTAGGED_PORT, is_bridge,
    //                         db->traffic_separation.primary_vlan_id);

    //         auto bss_iface_netdevs =
    //             network_utils::get_bss_ifaces(bss_iface, db->bridge.iface_name);

    //         for (const auto &iface_name : bss_iface_netdevs) {
    //             set_vlan_policy(iface_name, ePortMode::UNTAGGED_PORT, is_bridge,
    //                             db->traffic_separation.primary_vlan_id);
    //         }
    //     }
    // }

    // NOTE:
    // - No DHCP/IP configuration here. Main and guest networks br-lan and br-guest are handled by prplos
    // - No creation of bridge subinterfaces (e.g., br-lan.20).
}

void TrafficSeparation::apply_policy_for_new_interface(const std::string &bss_iface)
{
    auto db        = AgentDB::get();
    bool is_bridge = false;

    // If the primary VID has changed to zero, vlan filtering is disabled, so there is no point
    // modifying the VLAN policy on the platform interfaces.
    if (db->traffic_separation.primary_vlan_id == 0) {
        return;
    }

    auto bss_iface_netdevs = network_utils::get_bss_ifaces(bss_iface, db->bridge.iface_name);

    for (const auto &bss_iface_netdev : bss_iface_netdevs) {
        // Apply rules only to the new interface
        if (bss_iface_netdev == bss_iface) {
            continue;
        }
        if (!beerocks::net::network_utils::linux_add_iface_to_bridge(db->bridge.iface_name,
                                                                     bss_iface_netdev)) {
            LOG(INFO) << "The wireless interface " << bss_iface_netdev
                      << " is already in the bridge";
        }
        // Profile-2 Backhaul BSS
        if (m_profile_x_disallow_override_unsupported_configuration == 1) {
            set_vlan_policy(bss_iface_netdev, ePortMode::TAGGED_PORT_PRIMARY_TAGGED, is_bridge);
        }
        // Profile-1 Backhaul BSS
        else {
            set_vlan_policy(bss_iface_netdev, ePortMode::UNTAGGED_PORT, is_bridge,
                            db->traffic_separation.primary_vlan_id);
        }
    }
}

void TrafficSeparation::set_vlan_policy(const std::string &iface, ePortMode port_mode,
                                        bool is_bridge, uint16_t untagged_port_vid)
{
    if (iface.empty()) {
        LOG(ERROR) << "iface is empty!";
        return;
    }

    // Helper variables to make the code more readable.
    bool del = true; // First, remove all VIDs (vid=0).
    bool pvid;
    bool untagged;

    network_utils::set_iface_vid_policy(iface, del, 0, is_bridge);

    del = false;

    if (port_mode == ePortMode::TAGGED_PORT_PRIMARY_UNTAGGED ||
        port_mode == ePortMode::TAGGED_PORT_PRIMARY_TAGGED) {
        if (port_mode == ePortMode::TAGGED_PORT_PRIMARY_UNTAGGED) {
            // Set the new Primary VLAN with "PVID" and "Egress Untagged" policy.
            pvid     = true;
            untagged = true;
        } else {
            // Set the new Primary VLAN as Not "PVID" and Not "Egress Untagged" policy.
            pvid     = false;
            untagged = false;
        }
        auto db = AgentDB::get();
        network_utils::set_iface_vid_policy(iface, del, db->traffic_separation.primary_vlan_id,
                                            is_bridge, pvid, untagged);

        // Add secondary VIDs.
        pvid     = false;
        untagged = false;
        for (const auto sec_vid : db->traffic_separation.secondary_vlans_ids) {
            network_utils::set_iface_vid_policy(iface, del, sec_vid, is_bridge, pvid, untagged);
        }

        // Double tagged packets with S-Tag must be filtered on tagged ports.
        if (!is_bridge) {
            network_utils::set_vlan_packet_filter(true, iface);
        }
    }
    // port_mode == UNTAGGED_PORT
    else {
        if (!untagged_port_vid) {
            LOG(ERROR) << "Untagged Port VID was not set on port_mode of UNTAGGED_PORT";
            return;
        }
        // Set the new Primary VLAN with "PVID" and "Egress Untagged" policy.
        pvid      = true;
        untagged  = true;
        is_bridge = false; // Untagged Port cannot be a bridge interface.
        network_utils::set_iface_vid_policy(iface, del, untagged_port_vid, is_bridge, pvid,
                                            untagged);

        // Filter packets containing the VID of the Untagged Port.
        network_utils::set_vlan_packet_filter(true, iface, untagged_port_vid);
    }
}
} // namespace net
} // namespace beerocks
