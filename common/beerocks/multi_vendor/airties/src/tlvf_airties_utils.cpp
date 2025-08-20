/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_airties_utils.h"
#include "agent_db.h"
#include <atomic>
#include <bcl/beerocks_config_file.h>
#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <bpl/common/utils/utils.h>
#include <cmath>
#include <cstring>
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

using namespace airties;
using namespace beerocks;
using namespace wbapi;

/*
 * This Macro is to enable or disable
 * a bit in an octet. This is used for setting 
 * Gateway/Extender information in Device Info TLV.
 */
#define TLV_BIT_ENABLE 0x1
#define TLV_BIT_DISABLE 0x0

#define CPU_TEMP_FILE "/sys/devices/virtual/thermal/thermal_zone0/temp"
#define MEMINFO_FILE "/proc/meminfo"
#define STAT_FILE "/proc/stat"
#define STAT_CPU_TXT "cpu "
#define MEMCACHED_TXT "Cached:"
#define MEMTOTAL_TXT "MemTotal:"
#define MEMFREE_TXT "MemFree:"
#define MEMBUFFER_TXT "Buffers:"
#define STAT_IDLE_IND 3
#define COUNTERS_SIZE 6 /* Size of the counter is 6 octets */
#define BYTES_IN_KB 1024
#define CACHE_PERIOD 1000 /* in milliseconds */

namespace {
/* Global declarations for keeping previous cpu values */
uint64_t g_cpu_idle_prev  = 0;
uint64_t g_cpu_total_prev = 1;

/* Device metrics parameters to be cached */
struct sDeviceMetrics {
    uint32_t uptime;
    uint8_t cpu_load;
    uint8_t cpu_temp;
    int32_t memtotal;
    int32_t memfree;
    int32_t memcached;
    /* key: radio_id, val: radio_temp */
    std::vector<std::pair<sMacAddr, uint8_t>> radio_info;
};

/* Ethernet stats parameters to be cached */
struct sEthernetPortStats {
    uint64_t bytes_sent;
    uint64_t bytes_recvd;
    uint64_t packets_sent;
    uint64_t packets_recvd;
    uint64_t tx_pkt_errors;
    uint64_t rx_pkt_errors;
    uint64_t bcast_pkts_sent;
    uint64_t bcast_pkts_recvd;
    uint64_t mcast_pkts_sent;
    uint64_t mcast_pkts_recvd;
    uint64_t bcast_bytes_sent;
    uint64_t bcast_bytes_recvd;
    uint64_t mcast_bytes_sent;
    uint64_t mcast_bytes_recvd;
};

/*
 * Holds cached device and ethernet metrics with synchronized access.
 * Provides snapshot methods for thread-safe read access to cached values.
 */
struct sCachedMetrics {
    sDeviceMetrics device_metrics;
    std::map<uint8_t, sEthernetPortStats> ethernet_stats;
    mutable std::mutex cache_mutex;

    sDeviceMetrics snapshotDeviceMetrics() const
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        return device_metrics;
    }

    std::map<uint8_t, sEthernetPortStats> snapshotEthernetStats() const
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        return ethernet_stats;
    }
} g_cached_metrics;

void device_metrics_read_cpu_temp(sDeviceMetrics &device_metrics)
{
    std::ifstream file(CPU_TEMP_FILE);
    int temp_milli = 0;
    if (file >> temp_milli) {
        device_metrics.cpu_temp = static_cast<uint8_t>(temp_milli / 1000);
    } else {
        LOG(ERROR) << "Failed to read/parse " << CPU_TEMP_FILE;
    }
}

void device_metrics_read_cpu_load(sDeviceMetrics &device_metrics)
{
    std::ifstream file(STAT_FILE);
    if (!file.is_open()) {
        LOG(ERROR) << "Cannot open file: " << STAT_FILE;
        return;
    }

    std::string line;
    if (!std::getline(file, line) ||
        line.compare(0, std::strlen(STAT_CPU_TXT), STAT_CPU_TXT) != 0) {
        LOG(ERROR) << "Invalid or unreadable format in: " << STAT_FILE;
        return;
    }

    std::istringstream iss(line.substr(std::strlen(STAT_CPU_TXT)));

    std::vector<uint64_t> vals;
    uint64_t v;
    while (iss >> v) {
        vals.push_back(v);
    }

    /*
     * indices according to kernel docs (may be missing or extra)
     * 0=user,1=nice,2=system,3=idle,4=iowait,5=irq,6=softirq,7=steal,8=guest,9=guest_nice
     */
    auto get = [&](size_t idx) -> uint64_t {
        if (idx < vals.size()) {
            return vals[idx];
        } else {
            LOG(WARNING) << "CPU stats parse: missing value at index " << idx
                         << ", defaulting to 0";
            return 0ULL;
        }
    };

    uint64_t user    = get(0);
    uint64_t nice    = get(1);
    uint64_t system  = get(2);
    uint64_t idle    = get(3);
    uint64_t iowait  = get(4);
    uint64_t irq     = get(5);
    uint64_t softirq = get(6);
    uint64_t steal   = get(7);
    //uint64_t guest     = get(8);
    //uint64_t guest_nice= get(9);

    uint64_t total_now = user + nice + system + idle + iowait + irq + softirq + steal;
    uint64_t idle_now  = idle + steal;

    uint64_t prev_total = g_cpu_total_prev;
    uint64_t prev_idle  = g_cpu_idle_prev;

    uint8_t cpu_load = 0;
    if (total_now > prev_total && idle_now > prev_idle) {
        double usage = 1.0 - double(idle_now - prev_idle) / double(total_now - prev_total);
        cpu_load     = static_cast<uint8_t>(std::round(usage * 100.0));
    }

    g_cpu_total_prev = total_now;
    g_cpu_idle_prev  = idle_now;

    device_metrics.cpu_load = cpu_load;
}

void device_metrics_read_mem_info(sDeviceMetrics &device_metrics)
{
    std::ifstream file(MEMINFO_FILE);
    if (!file.is_open()) {
        LOG(ERROR) << "Cannot open file: " << MEMINFO_FILE;
        return;
    }

    int32_t memtotal = -1, memfree = -1, memcached = -1, membufs = -1;
    std::string line;

    while (std::getline(file, line)) {
        if (line.find(MEMTOTAL_TXT) != std::string::npos) {
            sscanf(line.c_str(), "%*s %d", &memtotal);
        } else if (line.find(MEMFREE_TXT) != std::string::npos) {
            sscanf(line.c_str(), "%*s %d", &memfree);
        } else if (line.find(MEMCACHED_TXT) != std::string::npos) {
            sscanf(line.c_str(), "%*s %d", &memcached);
        } else if (line.find(MEMBUFFER_TXT) != std::string::npos) {
            sscanf(line.c_str(), "%*s %d", &membufs);
        }
    }

    if (memcached >= 0 && membufs >= 0) {
        memcached += membufs;
    }

    device_metrics.memtotal  = memtotal;
    device_metrics.memfree   = memfree;
    device_metrics.memcached = memcached;
}

void device_metrics_read_uptime(sDeviceMetrics &device_metrics)
{
    auto now  = std::chrono::steady_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    device_metrics.uptime = static_cast<uint32_t>(secs > 0 ? secs : 0);
}

void device_metrics_read_radio_info(sDeviceMetrics &device_metrics)
{
    auto db           = AgentDB::get();
    int num_of_radios = db->get_radios_list().size();

    for (int radio_index = 1; radio_index <= num_of_radios; radio_index++) {
        std::string curr_radio_path =
            wbapi_utils::search_path_radio() + std::to_string(radio_index) + ".";

        auto dev = beerocks::bpl::m_ambiorix_cl.get_object(curr_radio_path);
        if (!dev) {
            LOG(ERROR) << "Failed to get the ambiorix object for path " << curr_radio_path;
            return;
        }

        std::string radio_name;
        if (!dev->read_child<>(radio_name, "Name")) {
            LOG(ERROR) << "Failed to read Name from " << curr_radio_path;
            return;
        }

        std::string radio_mac;
        if (!beerocks::net::network_utils::linux_iface_get_mac(radio_name, radio_mac)) {
            LOG(ERROR) << "Failed to get radio mac from ifname " << radio_name;
            return;
        }

        /* Temperature */
        std::string curr_radio_stats_path =
            wbapi_utils::search_path_radio() + std::to_string(radio_index) + "." + "Stats.";

        auto temp_obj = beerocks::bpl::m_ambiorix_cl.get_object(curr_radio_stats_path);
        if (!temp_obj) {
            LOG(ERROR) << "Failed to get the ambiorix object for temperature path "
                       << curr_radio_stats_path;
            return;
        }

        uint8_t radio_temp = 0;
        if (!temp_obj->read_child<>(radio_temp, "Temperature")) {
            LOG(ERROR) << "Failed to read Temperature from " << curr_radio_stats_path;
            return;
        }

        device_metrics.radio_info.emplace_back(tlvf::mac_from_string(radio_mac), radio_temp);
    }
}

/*
 * Checks if the given Ethernet interface is a WAN interface.
 * WAN interfaces will not be included in the TLV.
 */
bool is_wan_interface(AmbiorixVariantSmartPtr &eth_interface)
{
    bool upstream_val = false;
    if (!eth_interface->read_child(upstream_val, "Upstream")) {
        LOG(INFO) << "Failed to read Upstream value from DM";
    }
    return upstream_val;
}

/*
 * Checks if the given Ethernet interface is invalid.
 * Some interfaces may have entries in the data model but might not actually exist,
 * for example, interfaces that do not appear in ifconfig but are present in the data model.
 * To distinguish such invalid interfaces, we check the MAC address in the data model.
 * If the MAC address is empty, the interface is considered invalid and will not be added to the TLV.
 */
bool is_invalid_ethernet_interface(AmbiorixVariantSmartPtr &eth_interface)
{
    std::string mac_addr;
    if (!eth_interface->read_child(mac_addr, "MACAddress")) {
        LOG(INFO) << "Failed to read MACAddress value from DM";
    }
    return mac_addr.empty();
}

void ethernet_stats_read_stats(std::map<uint8_t, sEthernetPortStats> &ethernet_stats)
{
    auto eth_obj = beerocks::bpl::m_ambiorix_cl.get_object(wbapi_utils::search_path_ethernet());
    if (!eth_obj) {
        LOG(ERROR) << "Failed to get object: " << wbapi_utils::search_path_ethernet();
        return;
    }

    uint8_t num_ports = 0;
    if (!eth_obj->read_child(num_ports, "InterfaceNumberOfEntries") || !num_ports) {
        LOG(ERROR) << "No Ethernet ports found";
        return;
    }

    for (uint8_t i = 1; i <= num_ports; i++) {
        std::string dev_eth_iface_path =
            wbapi_utils::search_path_ethernet() + "Interface." + std::to_string(i) + ".";
        auto eth_interface_obj = beerocks::bpl::m_ambiorix_cl.get_object(dev_eth_iface_path);
        if (!eth_interface_obj) {
            LOG(ERROR) << "Failed to get object: " << dev_eth_iface_path;
            return;
        }

        /* Skip WAN interface */
        if (is_wan_interface(eth_interface_obj)) {
            continue;
        }

        /* Skip invalid interface */
        if (is_invalid_ethernet_interface(eth_interface_obj)) {
            continue;
        }

        std::string interface_alias;
        if (!eth_interface_obj->read_child(interface_alias, "Alias") || interface_alias.empty()) {
            LOG(ERROR) << "Failed to read Alias value from DM";
            return;
        }
        uint8_t port_id = airties::tlvf_airties_utils::assign_unique_port_id(interface_alias);

        std::string dev_eth_iface_stats_path = dev_eth_iface_path + "Stats.";
        auto stats_obj = beerocks::bpl::m_ambiorix_cl.get_object(dev_eth_iface_stats_path);
        if (!stats_obj) {
            LOG(ERROR) << "Stats obj missing for port " << i;
            continue;
        }

        auto read_ctr = [&](std::string param) {
            uint64_t value = 0;
            if (!stats_obj->read_child(value, param)) {
                LOG(ERROR) << "Failed to read " << param << " for port " << i;
            } else if ((param == "BytesSent") || (param == "BytesReceived")) {
                /*
                 * As per the requirement, only bytes counter need
                 * to be be converted to KiloByte. As of now, only
                 * BytesSent and Received are supported in DM.
                 */
                value = value / BYTES_IN_KB;
            }

            return value;
        };

        sEthernetPortStats s{};

        /* Base stats */
        s.bytes_sent    = read_ctr("BytesSent");
        s.bytes_recvd   = read_ctr("BytesReceived");
        s.packets_sent  = read_ctr("PacketsSent");
        s.packets_recvd = read_ctr("PacketsReceived");
        s.tx_pkt_errors = read_ctr("ErrorsSent");
        s.rx_pkt_errors = read_ctr("ErrorsReceived");

        /*
         * Optional stats.
         * Fields set to zero don't have a dm entry yet.
         * They'll be filled when entries become available.
         */
        s.mcast_bytes_sent  = 0;
        s.mcast_bytes_recvd = 0;
        s.mcast_pkts_sent   = read_ctr("MulticastPacketsSent");
        s.mcast_pkts_recvd  = read_ctr("MulticastPacketsReceived");
        s.bcast_bytes_sent  = 0;
        s.bcast_bytes_recvd = 0;
        s.bcast_pkts_sent   = read_ctr("BroadcastPacketsSent");
        s.bcast_pkts_recvd  = read_ctr("BroadcastPacketsReceived");

        ethernet_stats[port_id] = s;
    }
}

std::atomic<bool> g_keep_running{true};

/*
 * Thread callback that periodically reads device metrics and ethernet stats
 * and updates the shared cache.
 */
void background_metrics_updater_cb()
{
    while (g_keep_running.load()) {
        sDeviceMetrics device_metrics;
        std::map<uint8_t, sEthernetPortStats> ethernet_stats;

        device_metrics_read_cpu_temp(device_metrics);
        device_metrics_read_cpu_load(device_metrics);
        device_metrics_read_mem_info(device_metrics);
        device_metrics_read_uptime(device_metrics);
        device_metrics_read_radio_info(device_metrics);
        ethernet_stats_read_stats(ethernet_stats);

        {
            std::lock_guard<std::mutex> lock(g_cached_metrics.cache_mutex);
            g_cached_metrics.device_metrics = std::move(device_metrics);
            g_cached_metrics.ethernet_stats = std::move(ethernet_stats);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(CACHE_PERIOD));
    }
}

std::thread g_metrics_thread;

struct sMetricsThreadManager {
    sMetricsThreadManager()
    {
        g_keep_running   = true;
        g_metrics_thread = std::thread(background_metrics_updater_cb);
    }
    ~sMetricsThreadManager()
    {
        g_keep_running = false;
        if (g_metrics_thread.joinable()) {
            g_metrics_thread.join();
        }
    }
} static g_metrics_manager;
} // namespace

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

//Macros for different Link Speed Types
#define BITRATE_10 10
#define BITRATE_100 100
#define BITRATE_1K 1000
#define BITRATE_2K 2000
#define BITRATE_2_5K 2500
#define BITRATE_5K 5000
#define BITRATE_10K 10000

/*
 * This enum contains all the optional counters.
 * Values are assigned based on whether the corresponding
 * counter support is present in Ethernet.Interface.Stats. data model
 *
 * If the DM has this counter, the value is assigned 1.
 * If the DM doesnt have this counter, the value is assigned 0.
 */
enum supported_stats_enum {
    BCAST_BYTES_SENT  = 0,
    BCAST_BYTES_RECVD = 0,
    BCAST_PKTS_SENT   = 1,
    BCAST_PKTS_RECVD  = 1,
    MCAST_BYTES_SENT  = 0,
    MCAST_BYTES_RECVD = 0,
    MCAST_PKTS_SENT   = 1,
    MCAST_PKTS_RECVD  = 1
};

/*
 * Following function returns the link speed.
 * This is based on the requirement,
 * Bit 7:4 Supported Link Type Link type (0=undefined, 1=10Mbps, 2=100Mbps,
                                          3=1Gbps, 4=2,5Gbps, 5=5Gpbs, 6=10Gbps)
 * Bit 3:0 Current Link Type Link type (0=undefined, 1=10Mbps, 2=100Mbps,
                                          3=1Gbps, 4=2,5Gbps, 5=5Gpbs, 6=10Gbps
 */
uint8_t get_bitvalue(uint32_t bit_rate)
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
    case BITRATE_2K:
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

static AmbiorixVariantSmartPtr get_eth_interface_object(const std::string &path)
{
    return (beerocks::bpl::m_ambiorix_cl.get_object(path));
}

/*
 * Utility function to read any data type from DM.
 */
template <typename T>
bool get_data_from_dm(AmbiorixVariantSmartPtr &eth_interface, const std::string &param, T &data)
{
    if (!eth_interface->read_child<>(data, param.c_str())) {
        return false;
    }
    return true;
}

/*
 * Function to update the Ethernet Interface TLV
 */
bool tlvf_airties_utils::add_airties_ethernet_interface_tlv(ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    std::string dm_path        = wbapi_utils::search_path_ethernet();
    std::string interface_path = "Interface.";
    std::string link_path      = "Link.";
    std::string interface_dm_path, link_dm_path;
    uint8_t num_ports = 0;

    auto tlvAirtiesEthIntf = m_cmdu_tx.addClass<airties::tlvAirtiesEthernetInterface>();
    if (!tlvAirtiesEthIntf) {
        LOG(ERROR) << "addClass wfa_map::tlvDeviceEthernetInterface failed";
        return false;
    }

    tlvAirtiesEthIntf->vendor_oui() =
        (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));
    tlvAirtiesEthIntf->tlv_id() =
        static_cast<int>(airties::eAirtiesTlVId::AIRTIES_ETHERNET_INTERFACE);

    //Get the Ambiorix object
    auto eth_interface = get_eth_interface_object(dm_path);
    if (!eth_interface) {
        LOG(ERROR) << "Failed to get the ambiorix object for path " << dm_path;
        return false;
    }

    //Number of Ethernet Interfaces present
    if (!get_data_from_dm(eth_interface, "LinkNumberOfEntries", num_ports) || (!num_ports)) {
        LOG(ERROR) << "Failed to populate Ethernet Interface TLV as "
                      "LinkNumberOfEntries is not valid";
        return false;
    }

    for (uint8_t i = 1; i <= num_ports; i++) {

        interface_dm_path = dm_path + interface_path + std::to_string(i) + ".";

        eth_interface = get_eth_interface_object(interface_dm_path);
        if (!eth_interface) {
            LOG(ERROR) << "Failed to get the ambiorix object for path " << interface_dm_path;
            continue;
        }

        /* Skip WAN interface */
        if (is_wan_interface(eth_interface)) {
            continue;
        }

        /* Skip invalid interface */
        if (is_invalid_ethernet_interface(eth_interface)) {
            continue;
        }

        auto interface_list = tlvAirtiesEthIntf->create_interface_list();

        std::string interface_alias;
        if (!get_data_from_dm(eth_interface, "Alias", interface_alias) ||
            (interface_alias.empty())) {
            LOG(ERROR) << "Failed to read Alias value from DM";
            return false;
        }
        interface_list->port_id() =
            airties::tlvf_airties_utils::assign_unique_port_id(interface_alias);

        std::string mac_addr;
        if (!get_data_from_dm(eth_interface, "MACAddress", mac_addr)) {
            LOG(ERROR) << "Failed to get the MAC address for port_id " << interface_list->port_id();
        }
        interface_list->eth_mac() = tlvf::mac_from_string(mac_addr);

        std::string eth_interface_name;
        if (!get_data_from_dm(eth_interface, "Name", eth_interface_name)) {
            LOG(ERROR) << "Failed to the Interface Name for port_id " << interface_list->port_id();
        }
        interface_list->set_eth_intf_name(eth_interface_name);

        /*
         * Port State Bitwise
         * Bit 7 - eth_port_admin_state - Device.Ethernet.Interface.1.Status
         * Bit 6 - eth_port_link_state - Device.Ethernet.Link.1.Status
         * Bit 5 - eth_port_duplex_mode - Device.Ethernet.Interface.1.DuplexMode
         * Bit 4 - 0 - Reserved
         */
        std::string port_admin_state;
        if (!get_data_from_dm(eth_interface, "Status", port_admin_state)) {
            LOG(ERROR) << "Failed to get the admin state for the port_id "
                       << interface_list->port_id();
        }
        interface_list->flags1().eth_port_admin_state = (port_admin_state == "Up" ? 1 : 0);

        std::string port_dup_mode;
        if (!get_data_from_dm(eth_interface, "DuplexMode", port_dup_mode)) {
            LOG(ERROR) << "Failed to get the duplex mode for the port_id "
                       << interface_list->port_id();
        }
        interface_list->flags1().eth_port_duplex_mode =
            (((port_dup_mode == "Auto") || (port_dup_mode == "Full")) ? 1 : 0);

        /*
         * Next octet updation for Link Type
         * Bits 7 - 4 : Supported Link Type
         * Bits 3 - 0 : Current Link Type
         */
        uint32_t supp_link_type = 0, cur_link_type = 0;
        if (!get_data_from_dm(eth_interface, "MaxBitRate", supp_link_type)) {
            LOG(ERROR) << "Failed to get the Maximum support bit rate for port_id "
                       << interface_list->port_id();
        }
        interface_list->flags2().supported_link_type = get_bitvalue(supp_link_type);

        if (!get_data_from_dm(eth_interface, "CurrentBitRate", cur_link_type)) {
            LOG(ERROR) << "Failed to get the current bit rate for port_id "
                       << interface_list->port_id();
        }
        interface_list->flags2().current_link_type = get_bitvalue(cur_link_type);

        /*
         * Link state need to be fetched from Link path.
         * So, change the ambiorix path.
         */
        link_dm_path = dm_path + link_path + std::to_string(interface_list->port_id()) + ".";

        std::string port_link_state;
        auto eth_link = get_eth_interface_object(link_dm_path);
        if (eth_link) {
            if (!get_data_from_dm(eth_link, "Status", port_link_state)) {
                LOG(ERROR) << "Failed to the link status for the port id "
                           << interface_list->port_id();
            }
        } else {
            LOG(INFO) << "Unable to get the Ambiorix object for " << link_dm_path;
        }

        interface_list->flags1().eth_port_link_state = (port_link_state == "Up" ? 1 : 0);

        tlvAirtiesEthIntf->add_interface_list(interface_list);
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

inline void insert_ethernet_stats_item(std::shared_ptr<airties::cPortList> &port_list,
                                       uint64_t counter)
{
    auto statsItem = port_list->create_statsItem();
    if (!statsItem) {
        LOG(ERROR) << "Failed to create stats item!";
        return;
    }

    uint64_t swapped = swap_and_convert_counter(counter);

    statsItem->set_item(&swapped, COUNTERS_SIZE);

    port_list->add_statsItem(statsItem);
}

/*
 * Function to add the Ethernet Statistics TLV
 * to AP Metrics Response Message
 */
bool tlvf_airties_utils::add_airties_ethernet_stats_tlv(ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    auto snap_eth = g_cached_metrics.snapshotEthernetStats();
    if (snap_eth.empty()) {
        LOG(ERROR) << "No cached ethernet statistics available";
        return false;
    }

    auto tlvAirtiesEthStats = m_cmdu_tx.addClass<airties::tlvAirtiesEthernetStats>();
    if (!tlvAirtiesEthStats) {
        LOG(ERROR) << "addClass wfa_map::tlvAirtiesEthStats failed";
        return false;
    }

    tlvAirtiesEthStats->vendor_oui() =
        sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES);
    tlvAirtiesEthStats->tlv_id() = static_cast<int>(airties::eAirtiesTlVId::AIRTIES_ETHERNET_STATS);
    tlvAirtiesEthStats->supported_extra_stats() = set_supp_stats_val();

    for (auto &s : snap_eth) {
        auto port_list       = tlvAirtiesEthStats->create_port_list();
        port_list->port_id() = s.first;

        /*
         * The lines commented out below correspond to dm entries that are
         * not yet available. Uncomment them once those entries exist.
         * Do NOT change the insertion order.
         */
        insert_ethernet_stats_item(port_list, s.second.bytes_sent);
        insert_ethernet_stats_item(port_list, s.second.bytes_recvd);
        insert_ethernet_stats_item(port_list, s.second.packets_sent);
        insert_ethernet_stats_item(port_list, s.second.packets_recvd);
        insert_ethernet_stats_item(port_list, s.second.tx_pkt_errors);
        insert_ethernet_stats_item(port_list, s.second.rx_pkt_errors);
        //insert_ethernet_stats_item(port_list, s.second.mcast_bytes_sent);
        //insert_ethernet_stats_item(port_list, s.second.mcast_bytes_recvd);
        insert_ethernet_stats_item(port_list, s.second.mcast_pkts_sent);
        insert_ethernet_stats_item(port_list, s.second.mcast_pkts_recvd);
        //insert_ethernet_stats_item(port_list, s.second.bcast_bytes_sent);
        //insert_ethernet_stats_item(port_list, s.second.bcast_bytes_recvd);
        insert_ethernet_stats_item(port_list, s.second.bcast_pkts_sent);
        insert_ethernet_stats_item(port_list, s.second.bcast_pkts_recvd);

        tlvAirtiesEthStats->add_port_list(port_list);
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

    uint16_t version = get_feature_version(feature_id);

    // Set the feature info by combining the feature ID and version
    // EM+ features supported shall be reported as a big endian 4-octet value, where the 2 lowest
    // octets shall represent the version (iteration) of a feature and where the
    // 2 highest octets shall represent the ID of a feature
    // Below is the value for 2 highest octets of EM+ features supported feature ID.
    // shifting 16 bits(2 octets) and combining with the feature version.
    version_members->feature_info() = (static_cast<uint32_t>(feature_id) << 16) | version;

    // Add the created feature list entry to the TLV
    tlv_version_reporting->add_em_agent_feature_list(version_members);
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

/*
 * Function to update the Client details.
 * If the client details are not present, 
 * then the hard coded values will be saved in TLV.
 */
void update_client_details(std::shared_ptr<airties::tlvAirtiesDeviceInfo> &tlvDevinfo)
{
    std::string client_id     = "";
    std::string client_secret = "";
    std::string dm_path       = "X_AIRTIES_Obj.CloudComm.";

    auto cli_det = beerocks::bpl::m_ambiorix_cl.get_object(dm_path);
    if (!cli_det) {
        LOG(ERROR) << "Failed to get the ambiorix object for path,"
                      " Setting default values "
                   << dm_path;
    }
    //Retrieve the Client ID from DM
    if (cli_det) {
        cli_det->read_child<>(client_id, "ClientID");
        cli_det->read_child<>(client_secret, "ClientPassword");
    }

    tlvDevinfo->set_client_id(client_id);
    tlvDevinfo->set_client_secret(client_secret);
}

bool tlvf_airties_utils::add_airties_deviceinfo_tlv(ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    uint32_t randomBootid;
    auto db = beerocks::AgentDB::get();

    srand((unsigned)time(NULL));
    randomBootid = rand();

    auto tlvAirtiesDeviceInfo = m_cmdu_tx.addClass<airties::tlvAirtiesDeviceInfo>();
    if (!tlvAirtiesDeviceInfo) {
        LOG(ERROR) << "addClass wfa_map::tlvDeviceInfo failed";
        return false;
    }
    tlvAirtiesDeviceInfo->vendor_oui() =
        (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));
    tlvAirtiesDeviceInfo->tlv_id()  = static_cast<int>(airties::eAirtiesTlVId::AIRTIES_DEVICE_INFO);
    tlvAirtiesDeviceInfo->boot_id() = randomBootid;

    update_client_details(tlvAirtiesDeviceInfo);

    if (db->device_conf.local_gw) { //it's a controller
        tlvAirtiesDeviceInfo->flags1().gateway_product_class  = TLV_BIT_ENABLE;
        tlvAirtiesDeviceInfo->flags2().device_role_indication = TLV_BIT_ENABLE;
    } else {
        tlvAirtiesDeviceInfo->flags1().extender_product_class = TLV_BIT_ENABLE;
        tlvAirtiesDeviceInfo->flags2().device_role_indication = TLV_BIT_DISABLE;
    }
    LOG(INFO) << "Added Device Info TLV";

    return true;
}

/*
 * Function to add Device Metrics TLV.
 */
bool tlvf_airties_utils::add_device_metrics(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    auto snap_dev = g_cached_metrics.snapshotDeviceMetrics();

    auto tlv = cmdu_tx.addClass<airties::tlvAirtiesDeviceMetrics>();
    if (!tlv) {
        LOG(ERROR) << "Failed adding tlvAirtiesDeviceMetrics";
        return false;
    }

    tlv->vendor_oui()            = sVendorOUI(airties::tlvAirtiesMsgType::OUI_AIRTIES);
    tlv->tlv_id()                = static_cast<int>(airties::eAirtiesTlVId::AIRTIES_DEVICE_METRICS);
    tlv->uptime_to_boot()        = snap_dev.uptime;
    tlv->cpu_loadtime_platform() = snap_dev.cpu_load;
    tlv->cpu_temperature()       = snap_dev.cpu_temp;
    tlv->platform_totalmemory()  = std::max<int32_t>(0, snap_dev.memtotal);
    tlv->platform_freememory()   = std::max<int32_t>(0, snap_dev.memfree);
    tlv->platform_cachedmemory() = std::max<int32_t>(0, snap_dev.memcached);

    for (auto &s : snap_dev.radio_info) {
        auto radio                 = tlv->create_radio_list();
        radio->radio_id()          = s.first;
        radio->radio_temperature() = s.second;
        tlv->add_radio_list(radio);
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
        auto tlvAirtiesRadioCapability = cmdu_tx.addClass<airties::tlvAirtiesRadioCapability>();
        if (!tlvAirtiesRadioCapability) {
            LOG(ERROR) << "addClass airties::tlvAirtiesRadioCapability failed";
            return false;
        }

        tlvAirtiesRadioCapability->vendor_oui() =
            (sVendorOUI(airties::tlvAirtiesMsgType::airtiesVendorOUI::OUI_AIRTIES));
        tlvAirtiesRadioCapability->tlv_id() =
            static_cast<int>(airties::eAirtiesTlVId::AIRTIES_RADIO_CAPABILITY);

        std::string radio_path;
        beerocks::bpl::m_ambiorix_cl.resolve_path(
            wbapi_utils::search_path_radio_by_iface(radio->front.iface_name), radio_path);

        auto radio_obj = beerocks::bpl::m_ambiorix_cl.get_object(radio_path);
        if (!radio_obj) {
            LOG(ERROR) << "Failed to get the ambiorix object for path " << radio_path;
            return false;
        }

        std::string supported_standards;
        radio_obj->read_child(supported_standards, "SupportedStandards");

        auto str_vec = beerocks::string_utils::str_split(supported_standards, ',');

        tlvAirtiesRadioCapability->radio_id() = radio->front.iface_mac;
        tlvAirtiesRadioCapability->standards().s_80211a =
            std::find(str_vec.begin(), str_vec.end(), "a") != str_vec.end() ? 1 : 0;
        tlvAirtiesRadioCapability->standards().s_80211b =
            std::find(str_vec.begin(), str_vec.end(), "b") != str_vec.end() ? 1 : 0;
        tlvAirtiesRadioCapability->standards().s_80211g =
            std::find(str_vec.begin(), str_vec.end(), "g") != str_vec.end() ? 1 : 0;
        tlvAirtiesRadioCapability->standards().s_80211n =
            std::find(str_vec.begin(), str_vec.end(), "n") != str_vec.end() ? 1 : 0;
        tlvAirtiesRadioCapability->standards().s_80211ac =
            std::find(str_vec.begin(), str_vec.end(), "ac") != str_vec.end() ? 1 : 0;
        tlvAirtiesRadioCapability->standards().s_80211ax =
            std::find(str_vec.begin(), str_vec.end(), "ax") != str_vec.end() ? 1 : 0;
        tlvAirtiesRadioCapability->standards().s_80211be =
            std::find(str_vec.begin(), str_vec.end(), "be") != str_vec.end() ? 1 : 0;
    }

    return true;
}

uint8_t tlvf_airties_utils::assign_unique_port_id(const std::string &interface_name)
{
    static std::mutex port_id_mutex;
    std::lock_guard<std::mutex> lock(port_id_mutex);

    static std::map<std::string, uint8_t> assigned_ids;
    static std::set<uint8_t> used_ids;

    // Return immediately if already assigned
    auto it = assigned_ids.find(interface_name);
    if (it != assigned_ids.end()) {
        return it->second;
    }

    // Extract trailing digit if present
    int i = interface_name.size() - 1;
    while (i >= 0 && isdigit(interface_name[i])) {
        --i;
    }

    uint8_t suffix = 0;
    if (i != (int)interface_name.size() - 1) {
        std::string number_part = interface_name.substr(i + 1);
        suffix                  = static_cast<uint8_t>(std::stoi(number_part));
    }

    // Assign suffix if valid and unused
    if (used_ids.find(suffix) == used_ids.end()) {
        assigned_ids[interface_name] = suffix;
        used_ids.insert(suffix);
        return suffix;
    }

    // Assign next available number
    uint8_t next_id = 1;
    while (used_ids.find(next_id) != used_ids.end()) {
        ++next_id;
    }

    assigned_ids[interface_name] = next_id;
    used_ids.insert(next_id);
    return next_id;
}
