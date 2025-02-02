/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_airties_utils.h"
#include "agent_db.h"
#include <bcl/beerocks_config_file.h>
#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <bpl/common/utils/utils.h>
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
#include <tlvf/airties/tlvAirtiesMsgType.h>
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

static AmbiorixVariantSmartPtr get_eth_intf_object(const std::string &path)
{
    return (tlvf_air_utils.m_ambiorix_cl.get_object(path));
}

/*
 * Check if its WAN or LAN interface.
 * If its WAN, then dont add it to the TLV.
 */
bool check_wan_interface(AmbiorixVariantSmartPtr &eth_interface)
{
    uint8_t upstream_val = 0;
    if (!eth_interface->read_child(upstream_val, "Upstream")) {
        LOG(INFO) << "Failed to read Upstream value from DM";
    }
    return upstream_val;
}

/*
 * Utility function to read string from DM.
 */
std::string get_string_from_dm(AmbiorixVariantSmartPtr &eth_interface, const std::string &param)
{
    std::string read_str = "";
    if (!eth_interface->read_child<>(read_str, param.c_str())) {
        LOG(INFO) << "Failed to read string from DM for " << param;
    }
    return read_str;
}

/*
 * Utility function read uint8_t integer from DM.
 */
uint8_t get_uint8_from_dm(AmbiorixVariantSmartPtr &eth_interface, const std::string &param)
{
    uint8_t value = 0;
    if (!eth_interface->read_child<>(value, param.c_str())) {
        LOG(INFO) << "Failed to read integer from DM for " << param;
    }
    return value;
}

/*
 * Utility function read uint32_t integer from DM.
 */
uint32_t get_uint32_from_dm(AmbiorixVariantSmartPtr &eth_interface, const std::string &param)
{
    uint32_t value = 0;
    if (!eth_interface->read_child<>(value, param.c_str())) {
        LOG(INFO) << "Failed to read integer from DM for " << param;
    }
    return value;
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

    std::string dm_path   = "Device.Ethernet.";
    std::string intf_path = "Interface.";
    std::string link_path = "Link.";
    std::string int_details_path, link_details_path;
    uint8_t num_ports = 0;

    //Get the Ambiorix object
    auto eth_intf = get_eth_intf_object(dm_path);
    if (!eth_intf) {
        LOG(ERROR) << "Failed to get the ambiorix object for path " << dm_path;
        return false;
    }

    //Number of Ethernet Interfaces present
    num_ports = get_uint8_from_dm(eth_intf, "LinkNumberOfEntries");
    if (!num_ports) {
        LOG(ERROR) << "Failed to populate Ethernet Interface TLV as "
                      "LinkNumberOfEntries is not valid";
        return false;
    }

    for (uint8_t port_id = 1; port_id <= num_ports; port_id++) {

        int_details_path = dm_path + intf_path + std::to_string(port_id) + ".";

        auto eth_interface = get_eth_intf_object(int_details_path);
        if (!eth_interface) {
            LOG(ERROR) << "Failed to get the ambiorix object for path " << int_details_path;
            continue;
        }

        //Check if its a wan interface.
        if (check_wan_interface(eth_interface)) {
            continue;
        }

        auto intf_list = tlvAirtiesEthIntf->create_interface_list();

        /*
         * As of now, store the i value itself to the port id.
         * Once the community concludes the method to fetch
         * the port id, the implementation will be done here.
         */
        intf_list->port_id() = port_id;

        intf_list->eth_mac() =
            tlvf::mac_from_string(get_string_from_dm(eth_interface, "MACAddress"));

        intf_list->set_eth_intf_name(get_string_from_dm(eth_interface, "Name"));

        /*
         * Port State Bitwise
         * Bit 7 - eth_port_admin_state - Device.Ethernet.Interface.1.Status
         * Bit 6 - eth_port_link_state - Device.Ethernet.Link.1.Status
         * Bit 5 - eth_port_duplex_mode - Device.Ethernet.Interface.1.DuplexMode
         * Bit 4 - 0 - Reserved
         */

        std::string port_admin_state             = "";
        port_admin_state                         = get_string_from_dm(eth_interface, "Status");
        intf_list->flags1().eth_port_admin_state = (port_admin_state == "Up" ? 1 : 0);

        std::string port_dup_mode = "";
        port_dup_mode             = get_string_from_dm(eth_interface, "DuplexMode");
        intf_list->flags1().eth_port_duplex_mode =
            (((port_dup_mode == "Auto") || (port_dup_mode == "Full")) ? 1 : 0);

        /*
         * Next octet updation for Link Type
         * Bits 7 - 4 : Supported Link Type
         * Bits 3 - 0 : Current Link Type
         */
        uint32_t supp_link_type = 0, cur_link_type = 0;
        supp_link_type                          = get_uint32_from_dm(eth_interface, "MaxBitRate");
        intf_list->flags2().supported_link_type = get_bitvalue(supp_link_type);

        cur_link_type                         = get_uint32_from_dm(eth_interface, "CurrentBitRate");
        intf_list->flags2().current_link_type = get_bitvalue(cur_link_type);

        /*
         * Link state need to be fetched from Link path.
         * So, change the ambiorix path.
         */
        link_details_path = dm_path + link_path + std::to_string(port_id) + ".";

        std::string port_link_state = "";
        auto eth_link               = get_eth_intf_object(link_details_path);
        if (eth_link) {
            port_link_state = get_string_from_dm(eth_link, "Status");
        } else {
            LOG(INFO) << "Unable to get the Ambiorix object for " << link_details_path;
        }
        intf_list->flags1().eth_port_link_state = (port_link_state == "Up" ? 1 : 0);

        tlvAirtiesEthIntf->add_interface_list(intf_list);
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

    // Set the feature info by combining the feature ID and version
    // EM+ features supported shall be reported as a big endian 4-octet value, where the 2 lowest
    // octets shall represent the version (iteration) of a feature and where the
    // 2 highest octets shall represent the ID of a feature
    // Below is the value for 2 highest octets of EM+ features supported feature ID.
    // shifting 16 bits(2 octets) and combining with the feature version.
    version_members->feature_info() =
        (static_cast<int>(feature_id) << 16) | airties::eAirtiesFeatureVersion::feature_version;

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
        (airties::eMasterVersion::master_version << 16) | airties::eSubVersion::sub_version;

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
        case airties::eAirtiesFeatureIDs::AIRTIES_FEATURE_LED:
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

    auto cli_det = tlvf_air_utils.m_ambiorix_cl.get_object(dm_path);
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
    std::string dm_path      = "Device.WiFi.Radio.";
    std::string stats_string = "Stats.";
    std::string rad_details_path;

    auto db           = AgentDB::get();
    int num_of_radios = 0;

    num_of_radios = db->get_radios_list().size();
    LOG(INFO) << "Device Metrics TLV: Number of radios  " << num_of_radios;

    for (int radio_index = 1; radio_index <= num_of_radios; radio_index++) {
        auto rad_list = tlvDevMetrics->create_radio_list();

        //Radio ID
        rad_details_path = dm_path + std::to_string(radio_index) + ".";

        auto dev = tlvf_air_utils.m_ambiorix_cl.get_object(rad_details_path);
        if (!dev) {
            LOG(ERROR) << "Failed to get the ambiorix object for path " << rad_details_path;
            return false;
        }

        std::string radio_id = "";
        dev->read_child<>(radio_id, "BaseMACAddress");
        rad_list->radio_id() = tlvf::mac_from_string(radio_id);

        //Temperature
        rad_details_path = dm_path + std::to_string(radio_index) + "." + stats_string;

        auto temp_obj = tlvf_air_utils.m_ambiorix_cl.get_object(rad_details_path);
        if (!temp_obj) {
            LOG(ERROR) << "Failed to get the ambiorix object for path for temp "
                       << rad_details_path;
            return false;
        }

        uint8_t radio_temp = 0;
        temp_obj->read_child<>(radio_temp, "Temperature");
        rad_list->radio_temperature() = radio_temp;

        tlvDevMetrics->add_radio_list(rad_list);
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
