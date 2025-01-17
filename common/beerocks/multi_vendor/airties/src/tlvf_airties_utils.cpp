/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_airties_utils.h"
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
#include <tlvf/airties/tlvAirtiesMsgType.h>
#include <tlvf/airties/tlvVersionReporting.h>

using namespace airties;

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
