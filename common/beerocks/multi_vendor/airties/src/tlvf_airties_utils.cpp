/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_airties_utils.h"
#include "agent_db.h"

#include <ambiorix_variant.h>
#include <bcl/beerocks_config_file.h>
#include <bcl/beerocks_string_utils.h>
#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <bpl/bpl_cfg.h>
#include <bpl/common/utils/utils.h>
#include <easylogging++.h>
#include <linux/if_bridge.h>
#include <mapf/common/utils.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <tlvf/airties/eAirtiesTLVId.h>
#include <tlvf/airties/supported_features.h>
#include <tlvf/airties/tlvAirtiesDeviceInfo.h>
#include <tlvf/airties/tlvAirtiesDeviceMetrics.h>
#include <tlvf/airties/tlvAirtiesEthernetInterface.h>
#include <tlvf/airties/tlvAirtiesEthernetStats.h>
#include <tlvf/airties/tlvAirtiesMsgType.h>
#include <tlvf/airties/tlvAirtiesRadioCapability.h>
#include <tlvf/airties/tlvVersionReporting.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

using namespace airties;
using namespace beerocks;

/*
 * This Macro is to enable or disable
 * a bit in an octet. This is used for setting 
 * Gateway/Extender information in Device Info TLV.
 */
#define TLV_BIT_ENABLE 0x1
#define TLV_BIT_DISABLE 0x0

#define BOOT_ID_FILE "/proc/sys/kernel/random/boot_id"
#define CPU_TEMP_FILE "/sys/devices/virtual/thermal/thermal_zone0/temp"
#define MEMINFO_FILE "/proc/meminfo"
#define STAT_FILE "/proc/stat"
#define STAT_CPU_TXT "cpu "
#define MEMCACHED_TXT "Cached:"
#define MEMTOTAL_TXT "MemTotal:"
#define MEMFREE_TXT "MemFree:"
#define MEMBUFFER_TXT "Buffers:"
#define STAT_IDLE_IND 3

/*
 * Enum contains the different Link Types
 */
enum link_types_enum {
    UNDEFINED = 0,
    TYPE_10MBPS,
    TYPE_100MBPS,
    TYPE_1GBPS,
    TYPE_2_5GBPS,
    TYPE_5GBPS,
    TYPE_10GBPS
};

// Macros for different Link Speed Types
#define BITRATE_10 10
#define BITRATE_100 100
#define BITRATE_1K 1000
#define BITRATE_2_5K 2500
#define BITRATE_5K 5000
#define BITRATE_10K 10000

/*
 * Optional Ethernet counters.
 * 1 = supported, 0 = not supported.
 *
 * Some counters cannot currently be retrieved from the platform (/sys, ethtool, etc.),
 * so they remain disabled. If we later find a valid source for any of them,
 * simply switch the corresponding value to 1.
 */
enum supported_stats_enum {
    BCAST_BYTES_SENT  = 0,
    BCAST_BYTES_RECVD = 0,
    BCAST_PKTS_SENT   = 0,
    BCAST_PKTS_RECVD  = 0,
    MCAST_BYTES_SENT  = 0,
    MCAST_BYTES_RECVD = 0,
    MCAST_PKTS_SENT   = 0,
    MCAST_PKTS_RECVD  = 1
};

#define BYTES_IN_KB 1024
#define COUNTERS_SIZE 6 // Size of the counter is 6 octets.

#define BCAST_BYTES_SUPPORTED (BCAST_BYTES_SENT && BCAST_BYTES_RECVD)
#define MCAST_BYTES_SUPPORTED (MCAST_BYTES_SENT && MCAST_BYTES_RECVD)

/*
 * Following function returns the link type.
 * This is based on the requirement,
 * Bit 7:4 Supported Link Type Link type (0=undefined, 1=10Mbps, 2=100Mbps,
                                          3=1Gbps, 4=2,5Gbps, 5=5Gpbs, 6=10Gbps)
 * Bit 3:0 Current Link Type Link type (0=undefined, 1=10Mbps, 2=100Mbps,
                                          3=1Gbps, 4=2,5Gbps, 5=5Gpbs, 6=10Gbps
 */
static uint8_t bitrate_to_link_type(uint32_t bit_rate)
{
    uint8_t value;
    switch (bit_rate) {
    case BITRATE_10:
        value = TYPE_10MBPS;
        break;
    case BITRATE_100:
        value = TYPE_100MBPS;
        break;
    case BITRATE_1K:
        value = TYPE_1GBPS;
        break;
    case BITRATE_2_5K:
        value = TYPE_2_5GBPS;
        break;
    case BITRATE_5K:
        value = TYPE_5GBPS;
        break;
    case BITRATE_10K:
        value = TYPE_10GBPS;
        break;
    default:
        value = UNDEFINED;
        LOG(INFO) << "Invalid value in Current Link Type " << bit_rate;
        break;
    }
    return value;
}

/*
 * Function to set the 2 byte supported_stats_val field in
 * Ethernet Stats TLV. Based on the counter support available in the data model,
 * this 2 octet field is set.
 * Bit15: BcastBytesSentPresent
 * Bit14: BcastBytesReceivedPresent
 * Bit13: BcastPacketsSentPresent
 * Bit12: BcastPacketsReceivedPresent
 * Bit11: McastBytesSentPresent
 * Bit10: McastBytesReceivedPresent
 * Bit9 : McastPacketsSentPresent
 * Bit8 : McastPacketsReceivedPresent
 * Bit 7:0 FutureStatsPresent.
 */
uint16_t set_supp_stats_val()
{
    uint16_t var = 0x0000;

    std::vector<supported_stats_enum> macros = {
        BCAST_BYTES_SENT, BCAST_BYTES_RECVD, BCAST_PKTS_SENT, BCAST_PKTS_RECVD,
        MCAST_BYTES_SENT, MCAST_BYTES_RECVD, MCAST_PKTS_SENT, MCAST_PKTS_RECVD};

    for (size_t i = 0; i < macros.size(); ++i) {
        var |= (macros[i] << (15 - i));
    }

    return var;
}

/*
 * Check if its WAN or LAN interface.
 * If its WAN, then dont add it to the TLV.
 */
bool check_wan_interface(wbapi::AmbiorixVariantSmartPtr &eth_interface)
{
    uint8_t upstream_val = 0;
    if (!eth_interface->read_child(upstream_val, "Upstream")) {
        LOG(INFO) << "Failed to read Upstream value from DM";
    }
    return upstream_val;
}

/*
 * Utility function to read any data type from DM.
 */
template <typename T>
bool get_data_from_dm(wbapi::AmbiorixVariantSmartPtr &eth_interface, const std::string &param,
                      T &data)
{
    if (!eth_interface->read_child(data, param.c_str())) {
        return false;
    }
    return true;
}

/*
 * Function to update the Ethernet Interface TLV
 */
bool tlvf_airties_utils::add_airties_ethernet_interface_tlv(ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    auto tlvAirtiesEthIntf = m_cmdu_tx.addClass<airties::tlvAirtiesEthernetInterface>();
    if (!tlvAirtiesEthIntf) {
        LOG(ERROR) << "addClass wfa_map::tlvDeviceEthernetInterface failed";
        return false;
    }

    tlvAirtiesEthIntf->vendor_oui() =
        (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));
    tlvAirtiesEthIntf->tlv_id() =
        static_cast<int>(airties::eAirtiesTlVId::AIRTIES_ETHERNET_INTERFACE);

    auto lan_ifaces = beerocks::net::network_utils::linux_get_lan_interfaces();
    for (const auto &lan_iface : lan_ifaces) {
        std::string iface_mac;

        if (!beerocks::net::network_utils::linux_iface_get_mac(lan_iface, iface_mac)) {
            continue;
        }

        uint32_t link_speed;
        uint32_t max_speed;
        bool is_full_duplex;

        if (!net::network_utils::linux_iface_get_link_settings(lan_iface, link_speed, max_speed,
                                                               is_full_duplex)) {
            LOG(WARNING) << "Failed to get link settings for " << lan_iface;
            continue;
        }

        auto interface_list = tlvAirtiesEthIntf->create_interface_list();
        if (!interface_list) {
            LOG(ERROR) << "Failed to create interface list entry for " << lan_iface;
            continue;
        }

        interface_list->port_id() = airties::tlvf_airties_utils::assign_unique_port_id(lan_iface);
        interface_list->eth_mac() = tlvf::mac_from_string(iface_mac);
        if (!interface_list->set_eth_intf_name(lan_iface)) {
            LOG(ERROR) << "Failed to set eth interface name for " << lan_iface;
            return false;
        }

        interface_list->flags1().eth_port_admin_state =
            beerocks::net::network_utils::linux_iface_is_up(lan_iface);
        interface_list->flags1().eth_port_link_state =
            beerocks::net::network_utils::linux_iface_is_up_and_running(lan_iface);
        interface_list->flags1().eth_port_duplex_mode = is_full_duplex ? 1 : 0;
        interface_list->flags2().supported_link_type  = bitrate_to_link_type(max_speed);
        interface_list->flags2().current_link_type    = bitrate_to_link_type(link_speed);

        if (!tlvAirtiesEthIntf->add_interface_list(std::move(interface_list))) {
            LOG(ERROR) << "Failed to add interface list entry for " << lan_iface;
            return false;
        }
    }

    return true;
}

/*
 * Function to convert the counter to
 * Big Endian format, so that when sent over the transport
 * layer, the counters wont get swapped.
 * This workaround is needed because, non-native integers are not supported
 * in TLVF framework as of now. Hence, a list of 6 integers is used to
 * to store 48-bit integer. This will get swapped when sent over
 * transport layer. To avoid this, we are converting and sending the values.
 * This will be a temporaray workaround untill complete support
 * for non-native integer is implemented in TLVF.
 */
uint64_t swap_and_convert_counter(uint64_t val)
{
    val &= 0xFFFFFFFFFFFF;

    uint64_t byte1 = (val & 0x0000000000FF) << 40;
    uint64_t byte2 = (val & 0x00000000FF00) << 24;
    uint64_t byte3 = (val & 0x000000FF0000) << 8;
    uint64_t byte4 = (val & 0x0000FF000000) >> 8;
    uint64_t byte5 = (val & 0x00FF00000000) >> 24;
    uint64_t byte6 = (val & 0xFF0000000000) >> 40;

    uint64_t swapped = (byte1 | byte2 | byte3 | byte4 | byte5 | byte6);

    return swapped;
}

/*
 * Function to return byte value converted
 * to KB value.
 */
uint64_t convertBytes_to_Kb(uint64_t bytes_val) { return (bytes_val / BYTES_IN_KB); }

inline bool insert_ethernet_stats_item(std::shared_ptr<airties::cPortList> &port_list,
                                       uint64_t counter)
{
    auto statsItem = port_list->create_statsItem();
    if (!statsItem) {
        LOG(ERROR) << "Failed to create stats item!";
        return false;
    }

    uint64_t swapped = swap_and_convert_counter(counter);

    if (!statsItem->set_item(&swapped, COUNTERS_SIZE)) {
        LOG(ERROR) << "Failed to set stats item!";
        return false;
    }

    if (!port_list->add_statsItem(std::move(statsItem))) {
        LOG(ERROR) << "Failed to add stats item!";
        return false;
    }

    return true;
}

/*
 * Function to add the Ethernet Statistics TLV
 * to AP Metrics Response Message
 */
bool tlvf_airties_utils::add_airties_ethernet_stats_tlv(ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    auto tlvAirtiesEthStats = m_cmdu_tx.addClass<airties::tlvAirtiesEthernetStats>();
    if (!tlvAirtiesEthStats) {
        LOG(ERROR) << "addClass wfa_map::tlvAirtiesEthStats failed";
        return false;
    }

    tlvAirtiesEthStats->vendor_oui() =
        (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));
    tlvAirtiesEthStats->tlv_id() = static_cast<int>(airties::eAirtiesTlVId::AIRTIES_ETHERNET_STATS);
    tlvAirtiesEthStats->supported_extra_stats() = set_supp_stats_val();

    auto lan_ifaces = beerocks::net::network_utils::linux_get_lan_interfaces();
    for (auto &lan_iface : lan_ifaces) {
        beerocks::net::network_utils::sNetDevStats netDevStats = {0};
        if (!beerocks::net::network_utils::linux_get_net_dev_stats(lan_iface, netDevStats)) {
            LOG(WARNING) << "Failed to get net dev stats for " << lan_iface;
            continue;
        }

        auto port_list = tlvAirtiesEthStats->create_port_list();
        if (!port_list) {
            LOG(ERROR) << "Failed to create port list entry for " << lan_iface;
            continue;
        }

        port_list->port_id() = airties::tlvf_airties_utils::assign_unique_port_id(lan_iface);

        /*
         * As per the requirement, only byte counters need
         * to be be converted to KiloBytes.
         *
         * The commented out items below correspond to counters that we currently
         * cannot obtain from the platform (e.g. multicast/broadcast bytes).
         * These fields are optional, and we keep them commented out for now.
         * If we later find a reliable source for these counters under /sys or
         * elsewhere, they can simply be enabled. The insertion order must remain
         * unchanged.
         */
        if (!insert_ethernet_stats_item(port_list, convertBytes_to_Kb(netDevStats.tx_bytes))) {
            LOG(ERROR) << "Failed to insert ethernet stats item 'tx_bytes' for " << lan_iface;
            return false;
        }
        if (!insert_ethernet_stats_item(port_list, convertBytes_to_Kb(netDevStats.rx_bytes))) {
            LOG(ERROR) << "Failed to insert ethernet stats item 'rx_bytes' for " << lan_iface;
            return false;
        }
        if (!insert_ethernet_stats_item(port_list, netDevStats.tx_packets)) {
            LOG(ERROR) << "Failed to insert ethernet stats item 'tx_packets' for " << lan_iface;
            return false;
        }
        if (!insert_ethernet_stats_item(port_list, netDevStats.rx_packets)) {
            LOG(ERROR) << "Failed to insert ethernet stats item 'rx_packets' for " << lan_iface;
            return false;
        }
        if (!insert_ethernet_stats_item(port_list, netDevStats.tx_errs)) {
            LOG(ERROR) << "Failed to insert ethernet stats item 'tx_errs' for " << lan_iface;
            return false;
        }
        if (!insert_ethernet_stats_item(port_list, netDevStats.rx_errs)) {
            LOG(ERROR) << "Failed to insert ethernet stats item 'rx_errs' for " << lan_iface;
            return false;
        }
        //insert_ethernet_stats_item(port_list, multicast_bytes_sent);
        //insert_ethernet_stats_item(port_list, multicast_bytes_received);
        //insert_ethernet_stats_item(port_list, multicast_packets_sent);
        if (!insert_ethernet_stats_item(port_list, netDevStats.rx_multicast)) {
            LOG(ERROR) << "Failed to insert ethernet stats item 'rx_multicast' for " << lan_iface;
            return false;
        }
        //insert_ethernet_stats_item(port_list, broadcast_bytes_sent);
        //insert_ethernet_stats_item(port_list, broadcast_bytes_received);
        //insert_ethernet_stats_item(port_list, broadcast_packets_sent);
        //insert_ethernet_stats_item(port_list, broadcast_packets_received);

        if (!tlvAirtiesEthStats->add_port_list(std::move(port_list))) {
            LOG(ERROR) << "Failed to add port list entry for " << lan_iface;
            return false;
        }
    }

    return true;
}

/**
 * @brief Check if the Spanning Tree Protocol (STP) is enabled.
 *
 * This function opens a raw socket to communicate with the network device,
 * then queries the bridge information to determine if STP is enabled.
 *
 * @return Returns 1 if STP is enabled, 0 otherwise.
 */
bool tlvf_airties_utils::is_airties_platform_common_stp_enabled() const
{
    std::string slave_config_file_path =
        CONF_FILES_WRITABLE_PATH + std::string(BEEROCKS_AGENT) +
        ".conf"; //search first in platform-specific default directory
    beerocks::config_file::sConfigSlave beerocks_slave_conf;
    if (!beerocks::config_file::read_slave_config_file(slave_config_file_path,
                                                       beerocks_slave_conf)) {
        slave_config_file_path = mapf::utils::get_install_path() + "config/" +
                                 std::string(BEEROCKS_AGENT) +
                                 ".conf"; // if not found, search in beerocks path
        if (!beerocks::config_file::read_slave_config_file(slave_config_file_path,
                                                           beerocks_slave_conf)) {
            std::cout << "config file '" << slave_config_file_path << "' args error." << std::endl;
            return 0;
        }
    }
    return beerocks::bpl::utils::is_stp_enabled(beerocks_slave_conf.bridge_iface) ? true : false;
}

constexpr airties::eAirtiesFeatureVersion get_feature_version(eAirtiesFeatureIDs feature_id)
{
    switch (feature_id) {
    /* Eth stats gets version 2 since the introduction of the ETH_STATS_V2 TLV */
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_ETH_STATS:
        return airties::eAirtiesFeatureVersion::FEATURE_VERSION_2;
    default:
        return airties::eAirtiesFeatureVersion::FEATURE_VERSION_1;
    }
}

/**
 * @brief Create a feature list entry and add it to the TLV.
 *
 * This function creates a new feature list entry with the specified feature ID,
 * sets its version, and adds it to the TLV.
 *
 * @param tlvVersionReporting The TLV to which the feature will be added.
 * @param featureId The ID of the feature to add.
 * @return void
 */
inline void
create_and_add_feature_to_list(std::shared_ptr<airties::tlvVersionReporting> tlv_version_reporting,
                               airties::eAirtiesFeatureIDs feature_id)
{

    // Create a new feature list entry
    auto version_members = tlv_version_reporting->create_em_agent_feature_list();
    if (!version_members) {
        LOG(ERROR) << "Failed to create feature list entry for feature id " << feature_id;
        return;
    }

    uint16_t version = get_feature_version(feature_id);

    // Set the feature info by combining the feature ID and version
    // EM+ features supported shall be reported as a big endian 4-octet value, where the 2 lowest
    // octets shall represent the version (iteration) of a feature and where the
    // 2 highest octets shall represent the ID of a feature
    // Below is the value for 2 highest octets of EM+ features supported feature ID.
    // shifting 16 bits(2 octets) and combining with the feature version.
    version_members->feature_info() = (static_cast<uint32_t>(feature_id) << 16) | version;

    // Add the created feature list entry to the TLV
    if (!tlv_version_reporting->add_em_agent_feature_list(std::move(version_members))) {
        LOG(ERROR) << "Failed to add feature list entry for feature id " << feature_id;
        return;
    }
}

/**
 * @brief Add Airties Version Reporting TLV to the CMDU message.
 *
 * This function constructs a Version Reporting TLV, populates it with
 * supported features, and adds it to the outgoing CMDU message.
 *
 * @param cmdu_tx The CMDU message to which the TLV will be added.
 * @return Returns true if the TLV was successfully added, false otherwise.
 */
bool tlvf_airties_utils::add_airties_version_reporting_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    // Instance to utilize platform-specific utilities
    airties::tlvf_airties_utils utils_instance;

    // Attempt to create a TLV for version reporting
    auto tlv_version_reporting = cmdu_tx.addClass<airties::tlvVersionReporting>();

    // Check if the TLV creation failed
    if (!tlv_version_reporting) {
        LOG(ERROR) << "Failed to create Airties Feature Profile TLV";
        return false;
    }

    // Set the vendor OUI and TLV ID for Airties
    tlv_version_reporting->vendor_oui() =
        (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));

    tlv_version_reporting->tlv_id() =
        static_cast<int>(airties::eAirtiesTlVId::AIRTIES_FEATURE_PROFILE);

    // Set the em agent version
    // EM+ features supported shall be reported as a big endian 4-octet value, where the 2 lowest
    // octets shall represent the version (iteration) of a feature and where the
    // 2 highest octets shall represent the ID of a feature
    // Below is the value for 2 lowest octets of EM+ features supported version.
    // shifting 16 bits(2 octets) and combining with the subversion.
    tlv_version_reporting->em_agent_version() =
        (airties::eMasterVersion::MASTER_VERSION << 16) | airties::eSubVersion::SUB_VERSION;

    // The first feature ID we want to process
    int count = static_cast<int>(airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_DEVICE_METRICS);

    // Loop through all features until we reach the last one (AIRTIES_FEATURE_END)
    for (auto feature_id = count;
         feature_id < static_cast<int>(airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_END);) {

        // Convert integer ID to enum
        airties::eAirtiesFeatureIDs feature_id_enum =
            static_cast<airties::eAirtiesFeatureIDs>(feature_id);

        // Handle each feature based on its ID
        switch (feature_id_enum) {

        // Standard features - always added
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_DEVICE_METRICS:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_IEEE1905_1_14:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_DEVICE_INFO:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_ETH_STATS:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_REBOOT_RESET:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_WIFI6_CAP:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_DPP_ONBOARD:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_SERVICE_STATUS_WIFI_ON_OFF:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_HIDDEN_SSID:
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_RADIO_CAPABILITY:
            // Create and add the feature to the TLV
            create_and_add_feature_to_list(tlv_version_reporting, feature_id_enum);
            break;

        // Special case: STP feature is added only if STP is enabled on the platform
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_STP: {
            if (utils_instance.is_airties_platform_common_stp_enabled()) {
                LOG(INFO) << "Airties Feature STP is enabled";
                create_and_add_feature_to_list(tlv_version_reporting, feature_id_enum);
            }
            break;
        }
        // Not supported
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_LED:
            break;
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_END: {
            LOG(INFO) << "Airties Feature END is reached, "
                      << "Nothing to add in airties-specific version reporting TLV";
            break;
        }
        }
        feature_id++;
    }
    LOG(INFO) << "Added the airties-specific version reporting TLV";
    return true;
}

/** Populate AirTies cloud client details. */
bool update_client_details(std::shared_ptr<airties::tlvAirtiesDeviceInfo> &tlvDevinfo)
{
    std::string client_id     = beerocks::bpl::DEFAULT_AIRTIES_CLOUD_CLIENT_ID;
    std::string client_secret = beerocks::bpl::DEFAULT_AIRTIES_CLOUD_CLIENT_SECRET;

    auto management_mode = AgentDB::get()->device_conf.management_mode;
    if (management_mode == BPL_MGMT_MODE_NONPRPL_CONTROLLER_AGENT &&
        !beerocks::bpl::bpl_cfg_get_airties_cloud_credentials(client_id, client_secret)) {
        LOG(ERROR) << "Failed to read AirTies cloud credentials from BPL";
    }

    if (!tlvDevinfo->set_client_id(client_id)) {
        LOG(ERROR) << "Failed to set client id";
        return false;
    }
    if (!tlvDevinfo->set_client_secret(client_secret)) {
        LOG(ERROR) << "Failed to set client secret";
        return false;
    }

    return true;
}

/**
 * Derive the boot id from the kernel's per-boot UUID, which is regenerated only on reboot.
 * Read once and cached, so the value stays fixed for the lifetime of the boot.
 * Folded into 32 bits with FNV-1a hash function since the TLV field is uint32_t.
 */
static uint32_t get_boot_id()
{
    static const uint32_t boot_id = []() -> uint32_t {
        std::ifstream boot_id_file(BOOT_ID_FILE);
        std::string uuid;
        if (!boot_id_file || !std::getline(boot_id_file, uuid) || uuid.empty()) {
            LOG(ERROR) << "cannot read file " << BOOT_ID_FILE << ", boot id set to 0";
            return 0;
        }

        uint32_t hash = 2166136261u;
        for (unsigned char c : uuid) {
            if (std::isxdigit(c)) {
                hash = (hash ^ c) * 16777619u;
            }
        }
        return hash;
    }();

    return boot_id;
}

bool tlvf_airties_utils::add_airties_deviceinfo_tlv(ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    auto tlvAirtiesDeviceInfo = m_cmdu_tx.addClass<airties::tlvAirtiesDeviceInfo>();
    if (!tlvAirtiesDeviceInfo) {
        LOG(ERROR) << "addClass wfa_map::tlvDeviceInfo failed";
        return false;
    }
    tlvAirtiesDeviceInfo->vendor_oui() =
        (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));
    tlvAirtiesDeviceInfo->tlv_id()  = static_cast<int>(airties::eAirtiesTlVId::AIRTIES_DEVICE_INFO);
    tlvAirtiesDeviceInfo->boot_id() = get_boot_id();

    if (!update_client_details(tlvAirtiesDeviceInfo)) {
        LOG(ERROR) << "Failed to update client details in Device Info TLV";
        return false;
    }

    bool local_gw = false;
    {
        local_gw = beerocks::AgentDB::get()->device_conf.local_gw;
    }

    if (local_gw) { //it's a controller
        tlvAirtiesDeviceInfo->flags1().gateway_product_class  = TLV_BIT_ENABLE;
        tlvAirtiesDeviceInfo->flags2().device_role_indication = TLV_BIT_ENABLE;
    } else {
        tlvAirtiesDeviceInfo->flags1().extender_product_class = TLV_BIT_ENABLE;
        tlvAirtiesDeviceInfo->flags2().device_role_indication = TLV_BIT_DISABLE;
    }
    LOG(INFO) << "Added Device Info TLV";
    return true;
}

//Global declarations for keeping previous cpu values and meminfo
uint32_t cpu_idle_prev = 0, cpu_total_prev = 1;

//Function to get the CPU temperature
bool devicemetrics_get_cpu_temp(uint8_t &cpu_temp)
{
    char buf[32] = {0};
    /* Open ACPI thermal zone sysfs file to read temperature. */
    FILE *fd = fopen(CPU_TEMP_FILE, "r");
    if (fd == NULL) {
        LOG(ERROR) << "cannot open file" << CPU_TEMP_FILE;
        return false;
    }

    if (fgets(buf, sizeof(buf), fd)) {
        /* Temperature resides as milidegree Celcius as denoted in Linux ACPI docs. */
        cpu_temp = atoi(buf) / 1000;
    } else {
        cpu_temp = 0;
    }
    fclose(fd);
    return true;
}

//Function to get the CPU Load
bool devicemetrics_get_cpu_load(uint8_t &cpu_load)
{
    uint8_t cpu_total = 0, cpu_idle = 0;
    char buf[256] = {0};

    FILE *fp = fopen(STAT_FILE, "r");
    if (fp == NULL) {
        LOG(ERROR) << "cannot open file" << STAT_FILE;
        return false;
    }

    /* Get the first line of CPU stats. */
    if (fgets(buf, sizeof(buf), fp)) {
        int i = 0;
        char *token, *ctx;

        /* Check if we have expected string in read buffer. */
        if (strncmp(buf, STAT_CPU_TXT, strlen(STAT_CPU_TXT)) != 0) {
            LOG(ERROR) << "Incorrect string read in CPU stats.";
            fclose(fp);
            return false;
        }
        /* Point empty spaces and parse to get CPU stats: user nice system idle .. */
        token = strtok_r(buf, " ", &ctx);
        while (token != NULL) {
            token = strtok_r(NULL, " ", &ctx);
            if (token != NULL) {
                cpu_total += atoi(token);
                /* IDLE ticks are stored in 4th column according to Linux documentation. */
                if (i == STAT_IDLE_IND) {
                    cpu_idle = atoi(token);
                }
                i++;
            }
        }
    }

    LOG(INFO) << "cpu_idle_prev " << cpu_idle_prev << "cpu_idle " << cpu_idle << "cpu_total_prev "
              << cpu_total_prev << "cpu_total " << cpu_total;

    if (cpu_total > 0 && cpu_idle > 0 && cpu_total != cpu_total_prev) {
        cpu_load = (1 - ((double)(cpu_idle - cpu_idle_prev) / (cpu_total - cpu_total_prev))) * 100;
    }
    /* Keep previous data to get delta between cpu load changes. */
    cpu_idle_prev  = cpu_idle;
    cpu_total_prev = cpu_total;
    fclose(fp);
    return true;
}

bool devicemetrics_get_meminfo(int32_t &memtotal, int32_t &memfree, int32_t &memcached)
{
    FILE *fp        = NULL;
    char buf[32]    = {0};
    int32_t membufs = -1;

    fp = fopen(MEMINFO_FILE, "r");
    if (fp == NULL) {
        LOG(ERROR) << "cannot open file" << MEMINFO_FILE;
        goto out;
    }
    /* Read meminfo file line by line to fetch free, cached and total sizes. */
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        char *ctx = NULL, *fld = NULL, *val = NULL;
        fld = strtok_r(buf, " ", &ctx);
        if (fld == NULL) {
            continue;
        }
        /* Point empty spaces and parse to get mem info. */
        val = strtok_r(NULL, " ", &ctx);
        if (val == NULL) {
            continue;
        }
        if (strcmp(fld, MEMCACHED_TXT) == 0) {
            memcached = atoi(val);
        } else if (strcmp(fld, MEMFREE_TXT) == 0) {
            memfree = atoi(val);
        } else if (strcmp(fld, MEMTOTAL_TXT) == 0) {
            memtotal = atoi(val);
        } else if (strcmp(fld, MEMBUFFER_TXT) == 0) {
            membufs = atoi(val);
        }
        if (memcached >= 0 && memfree >= 0 && memtotal >= 0 && membufs >= 0) {
            /* We got all we need, break the loop. */
            break;
        }
    }
    if ((memcached < 0) || (memtotal < 0) || (memfree < 0) || (membufs < 0)) {
        LOG(INFO) << "Failed to read meminfo fields memcache: " << memcached
                  << "memtotal:  " << memtotal << "memfree: " << memfree << "membuffs: " << membufs;
        goto out;
    }
    memcached = memcached + membufs;
    fclose(fp);
    return true;
out:
    if (fp != NULL) {
        fclose(fp);
    }
    return false;
}

//Function to get the Device Uptime
bool devicemetrics_get_uptime(struct timespec &ts)
{
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return false;
    }
    return true;
}

bool devicemetrics_get_radio_info(std::shared_ptr<airties::tlvAirtiesDeviceMetrics> &tlvDevMetrics)
{
    const auto &radios = AgentDB::get()->get_radios_list();

    LOG(INFO) << "Device Metrics TLV: Number of radios  " << radios.size();

    for (const auto &radio : radios) {
        if (radio->front.iface_mac == beerocks::net::network_utils::ZERO_MAC) {
            continue;
        }

        auto rad_list = tlvDevMetrics->create_radio_list();
        if (!rad_list) {
            LOG(ERROR) << "Failed to create radio list entry for " << radio->front.iface_name;
            return false;
        }

        rad_list->radio_id() = radio->front.iface_mac;

        uint8_t radio_temp = 0;
        if (!beerocks::bpl::bpl_cfg_get_wifi_radio_temperature(radio->front.iface_name,
                                                               radio_temp)) {
            LOG(ERROR) << "Failed to read radio temperature for " << radio->front.iface_name;
            return false;
        }
        rad_list->radio_temperature() = radio_temp;

        if (!tlvDevMetrics->add_radio_list(std::move(rad_list))) {
            LOG(ERROR) << "Failed to add radio list entry for " << radio->front.iface_name;
            return false;
        }
    }
    return true;
}

/*
 * Function to add Device Metrics TLV.
 */
bool tlvf_airties_utils::add_device_metrics(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 0};
    int32_t memcached = -1, memtotal = -1, memfree = -1;
    uint8_t cpu_load = 0, cpu_temp = 0;

    auto tlvAirtiesDeviceMetrics = cmdu_tx.addClass<airties::tlvAirtiesDeviceMetrics>();
    if (!tlvAirtiesDeviceMetrics) {
        LOG(ERROR) << "Failed adding tlvAirtiesDeviceMetrics";
        return false;
    }

    tlvAirtiesDeviceMetrics->vendor_oui() =
        sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES);
    tlvAirtiesDeviceMetrics->tlv_id() =
        static_cast<int>(airties::eAirtiesTlVId::AIRTIES_DEVICE_METRICS);

    /*
     * If any of the following fields like cpu lod, meminfo returns
     * error, the TLV will still be added except the respective values
     * which threw error.
     */
    if (devicemetrics_get_uptime(ts)) {
        tlvAirtiesDeviceMetrics->uptime_to_boot() = ts.tv_sec;
    } else {
        LOG(INFO) << "Unable to fetch the clock time for"
                  << "updating the Device Metrics TLV";
        tlvAirtiesDeviceMetrics->uptime_to_boot() = 0;
    }

    if (devicemetrics_get_cpu_load(cpu_load)) {
        tlvAirtiesDeviceMetrics->cpu_loadtime_platform() = cpu_load;
    } else {
        LOG(INFO) << "Unable to fetch the CPU load for"
                  << "updating the Device Metrics TLV";
        tlvAirtiesDeviceMetrics->cpu_loadtime_platform() = 0;
    }

    if (devicemetrics_get_cpu_temp(cpu_temp)) {
        tlvAirtiesDeviceMetrics->cpu_temperature() = cpu_temp;
    } else {
        LOG(INFO) << "Unable to fetch the CPU Temp for"
                  << "updating the Device Metrics TLV";
        tlvAirtiesDeviceMetrics->cpu_temperature() = 0;
    }

    if (devicemetrics_get_meminfo(memtotal, memfree, memcached)) {
        tlvAirtiesDeviceMetrics->platform_totalmemory()  = memtotal;
        tlvAirtiesDeviceMetrics->platform_freememory()   = memfree;
        tlvAirtiesDeviceMetrics->platform_cachedmemory() = memcached;
    } else {
        LOG(INFO) << "Unable to fetch the Memory Info for"
                  << "updating the Device Metrics TLV";
        tlvAirtiesDeviceMetrics->platform_totalmemory()  = 0;
        tlvAirtiesDeviceMetrics->platform_freememory()   = 0;
        tlvAirtiesDeviceMetrics->platform_cachedmemory() = 0;
    }
    if (!devicemetrics_get_radio_info(tlvAirtiesDeviceMetrics)) {
        LOG(INFO) << "Unable to fetch the radio Info for"
                  << "updating the Device Metrics TLV";
    }

    return true;
}

/**
 * @brief Prototype function to add an Airties Message Type TLV to the CMDU.
 *
 * This function demonstrates how to add another TLV (in this case, a Message Type TLV)
 * to the CMDU message.
 *
 * @param cmdu_tx The CMDU message to which the TLV will be added.
 * @return Returns true if the TLV was successfully added, false otherwise.
 */
bool tlvf_airties_utils::add_airties_msgtype_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    // Attempt to create a TLV for Airties message type
    auto tlv_airties_msg_type = cmdu_tx.addClass<airties::tlvAirtiesMsgType>();

    // Check if the TLV creation failed
    if (!tlv_airties_msg_type) {
        LOG(ERROR) << "addClass wfa_map::tlvMsgType failed";
        return false;
    }

    // Set the vendor OUI for Airties
    tlv_airties_msg_type->vendor_oui() =
        (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));
    LOG(INFO) << "Added Airties Msg Type TLV";
    return true;
}

/**
 * @brief Add Airties Radio Capability TLV to the CMDU message.
 *
 * This function constructs a Radio Capability TLV, populates it with
 * supported standards, and adds it to the outgoing CMDU message.
 *
 * @param cmdu_tx The CMDU message to which the TLV will be added.
 * @return Returns true if the TLV was successfully added, false otherwise.
 */
bool tlvf_airties_utils::add_radio_capability(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    auto db = beerocks::AgentDB::get();
    for (const auto &radio : db->get_radios_list()) {
        if (radio->front.iface_mac == beerocks::net::network_utils::ZERO_MAC) {
            continue;
        }

        auto tlvAirtiesRadioCapability = cmdu_tx.addClass<airties::tlvAirtiesRadioCapability>();
        if (!tlvAirtiesRadioCapability) {
            LOG(ERROR) << "addClass airties::tlvAirtiesRadioCapability failed";
            return false;
        }

        tlvAirtiesRadioCapability->vendor_oui() =
            (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));
        tlvAirtiesRadioCapability->tlv_id() =
            static_cast<int>(airties::eAirtiesTlVId::AIRTIES_RADIO_CAPABILITY);

        if (radio->supported_standards.empty()) {
            LOG(ERROR) << "Supported standards are unavailable for " << radio->front.iface_name;
            return false;
        }

        const auto standards = beerocks::string_utils::str_split(radio->supported_standards, ',');
        const auto supports  = [&standards](const char *standard) {
            return std::find(standards.begin(), standards.end(), standard) != standards.end();
        };

        tlvAirtiesRadioCapability->radio_id()            = radio->front.iface_mac;
        tlvAirtiesRadioCapability->standards().s_80211a  = supports("a");
        tlvAirtiesRadioCapability->standards().s_80211b  = supports("b");
        tlvAirtiesRadioCapability->standards().s_80211g  = supports("g");
        tlvAirtiesRadioCapability->standards().s_80211n  = supports("n");
        tlvAirtiesRadioCapability->standards().s_80211ac = supports("ac");
        tlvAirtiesRadioCapability->standards().s_80211ax = supports("ax");
        tlvAirtiesRadioCapability->standards().s_80211be = supports("be");
    }

    return true;
}

uint8_t tlvf_airties_utils::assign_unique_port_id(const std::string &interface_name)
{
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    static std::map<std::string, uint8_t> iface_to_id;

    // If already assigned, return existing ID
    auto it = iface_to_id.find(interface_name);
    if (it != iface_to_id.end()) {
        return it->second;
    }

    // Assign next sequential ID starting from 1
    uint8_t new_id              = static_cast<uint8_t>(iface_to_id.size() + 1);
    iface_to_id[interface_name] = new_id;

    return new_id;
}
