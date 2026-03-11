/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "tlvf_utils.h"

#include <bcl/beerocks_utils.h>
#include <bcl/son/son_wireless_utils.h>
#include <easylogging++.h>

#include <tlvf/wfa_map/tlvApRadioBasicCapabilities.h>
#include <tlvf/wfa_map/tlvChannelScanCapabilities.h>
#include <tlvf/wfa_map/tlvOperatingChannelReport.h>
#include <tlvf/wfa_map/tlvSpatialReuseReport.h>

using namespace beerocks;

constexpr uint8_t MINIMUM_SCAN_INTERVAL_SEC = 2;

/**
 * @brief Get the maximum transmit power of operating class.
 *
 * @param channels_list List of supported channels.
 * @param operating_class Operating class to find max tx for.
 * @return Max tx power for requested operating class.
 */
static int8_t get_operating_class_max_tx_power(
    const std::unordered_map<uint8_t, beerocks::AgentDB::sRadio::sChannelInfo> &channels_list,
    uint8_t operating_class)
{
    int8_t max_tx_power = 0;
    auto oper_class_it  = son::wireless_utils::operating_classes_list.find(operating_class);
    if (oper_class_it == son::wireless_utils::operating_classes_list.end()) {
        LOG(ERROR) << "Operating class does not exist: " << operating_class;
        return beerocks::eGlobals::RSSI_INVALID;
    }

    const auto &oper_class = oper_class_it->second;

    for (const auto &channel_info_element : channels_list) {
        auto channel       = channel_info_element.first;
        auto &channel_info = channel_info_element.second;
        for (const auto &bw_info : channel_info.supported_bw_list) {
            if (son::wireless_utils::has_operating_class_5g_channel(oper_class, channel,
                                                                    bw_info.bandwidth)) {
                max_tx_power = std::max(max_tx_power, channel_info.tx_power_dbm);
            }
        }
    }
    return max_tx_power;
}

/**
 * @brief Get a list of permanent non operable channels for operating class.
 *
 * @param channels_list List of supported channels.
 * @param operating_class Operating class to find non operable channels on.
 * @return std::vector<uint8_t> A vector of non operable channels.
 */
std::vector<uint8_t> get_operating_class_non_oper_channels(
    const std::unordered_map<uint8_t, beerocks::AgentDB::sRadio::sChannelInfo> &channels_list,
    uint8_t operating_class)
{
    std::vector<uint8_t> non_oper_channels;
    auto oper_class_it = son::wireless_utils::operating_classes_list.find(operating_class);
    if (oper_class_it == son::wireless_utils::operating_classes_list.end()) {
        LOG(ERROR) << "Operating class does not exist: " << operating_class;
        return {};
    }

    const auto &oper_class = oper_class_it->second;

    for (const auto &op_class_channel : oper_class.channels) {
        bool found = false;
        for (const auto &channel_info_element : channels_list) {
            auto &channel_info = channel_info_element.second;
            for (const auto &bw_info : channel_info.supported_bw_list) {
                auto channel = channel_info_element.first;
                if (oper_class.band != bw_info.bandwidth) {
                    if (!((bw_info.bandwidth == beerocks::eWiFiBandwidth::BANDWIDTH_320_1 ||
                           bw_info.bandwidth == beerocks::eWiFiBandwidth::BANDWIDTH_320_2) &&
                          oper_class.band == beerocks::eWiFiBandwidth::BANDWIDTH_320)) {
                        continue;
                    }
                }

                auto is_there_any_unavailable_overlapping_channel = [&]() -> bool {
                    auto overlapping_beacon_channels =
                        son::wireless_utils::get_overlapping_beacon_channels(
                            channel, son::wireless_utils::which_freq_op_cls(operating_class),
                            bw_info.bandwidth);

                    return std::any_of(
                        overlapping_beacon_channels.cbegin(), overlapping_beacon_channels.cend(),
                        [&channels_list](uint8_t overlap_ch) {
                            return channels_list.find(overlap_ch) == channels_list.end();
                        });
                };

                if ((son::wireless_utils::which_freq_op_cls(operating_class) !=
                     beerocks::eFreqType::FREQ_24G) &&
                    is_there_any_unavailable_overlapping_channel()) {
                    break;
                }

                if (son::wireless_utils::is_operating_class_using_central_channel(
                        operating_class)) {
                    channel = son::wireless_utils::get_center_channel(
                        channel, son::wireless_utils::which_freq_op_cls(operating_class),
                        bw_info.bandwidth);
                }
                if (op_class_channel == channel) {
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (!found) {
            non_oper_channels.push_back(op_class_channel);
        }
    }
    return non_oper_channels;
}

bool tlvf_utils::add_ap_radio_basic_capabilities(ieee1905_1::CmduMessageTx &cmdu_tx,
                                                 const sMacAddr &ruid)
{
    auto radio_basic_caps = cmdu_tx.addClass<wfa_map::tlvApRadioBasicCapabilities>();
    if (!radio_basic_caps) {
        LOG(ERROR) << "Error creating TLV_AP_RADIO_BASIC_CAPABILITIES";
        return false;
    }
    radio_basic_caps->radio_uid() = ruid;

    auto db    = AgentDB::get();
    auto radio = db->get_radio_by_mac(ruid);
    if (!radio) {
        LOG(ERROR) << "ruid not found: " << ruid;
        return false;
    }

    int num_bsses = std::count_if(radio->front.bssids.begin(), radio->front.bssids.end(),
                                  [](beerocks::AgentDB::sRadio::sFront::sBssid b) {
                                      return b.mac != net::network_utils::ZERO_MAC;
                                  });
    LOG(DEBUG) << "Radio reports " << num_bsses << " BSSes.";

    uint8_t radio_max_bss = radio->front.radio_max_bss;

    // when not implemented in bwl, radio_max_bss returns 0; use num_bsses in this case
    radio_basic_caps->maximum_number_of_bsss_supported() =
        (radio_max_bss > 0) ? radio_max_bss : num_bsses;

    LOG(DEBUG) << "Filling Supported operating classes on radio " << radio->front.iface_name
               << " (band type: "
               << beerocks::utils::convert_frequency_type_to_string(
                      radio->wifi_channel.get_freq_type())
               << "):";
    auto operating_classes = son::wireless_utils::get_operating_classes_of_freq_type(
        radio->wifi_channel.get_freq_type());

    for (auto op_class : operating_classes) {
        auto non_oper_channels =
            get_operating_class_non_oper_channels(radio->channels_list, op_class);

        // OPClass won't be added if its non-operable channels size is equal to its channels size
        if (non_oper_channels.size() ==
            son::wireless_utils::operating_classes_list.find(op_class)->second.channels.size()) {
            continue;
        }

        auto operationClassesInfo = radio_basic_caps->create_operating_classes_info_list();
        if (!operationClassesInfo) {
            LOG(ERROR) << "Failed creating operating classes info list";
            return false;
        }

        operationClassesInfo->operating_class() = op_class;
        operationClassesInfo->maximum_transmit_power_dbm() =
            get_operating_class_max_tx_power(radio->channels_list, op_class);

        if (!non_oper_channels.empty()) {
            // Create list of statically non-oper channels

            operationClassesInfo->alloc_statically_non_operable_channels_list(
                non_oper_channels.size());
            uint8_t idx = 0;
            for (auto non_oper : non_oper_channels) {
                *operationClassesInfo->statically_non_operable_channels_list(idx) = non_oper;
                idx++;
            }
        }

        LOG(DEBUG) << "OpClass=" << op_class
                   << ", max_tx_dbm=" << operationClassesInfo->maximum_transmit_power_dbm()
                   << ", non_operable_channels=" << [&]() {
                          if (non_oper_channels.empty()) {
                              return std::string{"None"};
                          }
                          std::string out;
                          for (auto non_oper_ch : non_oper_channels) {
                              out.append(std::to_string(non_oper_ch)).append(",");
                          }
                          out.pop_back();
                          return out;
                      }();

        if (!radio_basic_caps->add_operating_classes_info_list(operationClassesInfo)) {
            LOG(ERROR) << "add_operating_classes_info_list failed";
            return false;
        }
    }

    return true;
}

bool tlvf_utils::create_operating_channel_report(ieee1905_1::CmduMessageTx &cmdu_tx,
                                                 const sMacAddr &radio_mac)
{
    auto radio = AgentDB::get()->get_radio_by_mac(radio_mac);
    if (!radio) {
        LOG(ERROR) << "Radio " << radio_mac << " does not exist on the db";
        return false;
    }

    auto get_operating_list = [&radio]() -> std::vector<std::pair<uint8_t, uint8_t>> {
        std::vector<std::pair<uint8_t, uint8_t>> operating_list = {};

        auto operating_classes = son::wireless_utils::get_operating_classes_of_freq_type(
            radio->wifi_channel.get_freq_type());

        auto operating_class =
            son::wireless_utils::get_operating_class_by_channel(radio->wifi_channel);

        for (auto it = operating_classes.begin();
             it != operating_classes.end() && *it <= operating_class; ++it) {

            if (son::wireless_utils::is_channel_in_operating_class(
                    *it, radio->wifi_channel.get_channel())) {
                operating_list.emplace_back(std::make_pair(*it, radio->wifi_channel.get_channel()));
            }

            if (son::wireless_utils::is_operating_class_using_central_channel(*it)) {
                std::map<uint8_t, std::map<beerocks::eWiFiBandwidth, son::wireless_utils::sChannel>>
                    channels_table;
                if (radio->wifi_channel.get_freq_type() == beerocks::eFreqType::FREQ_5G) {
                    channels_table = son::wireless_utils::channels_table_5g;
                } else if (radio->wifi_channel.get_freq_type() == beerocks::eFreqType::FREQ_6G) {
                    channels_table = son::wireless_utils::channels_table_6g;
                }

                auto channel_it = channels_table.find(radio->wifi_channel.get_channel());
                if (channel_it == channels_table.end()) {
                    LOG(ERROR) << "Failed find " << radio->wifi_channel.get_channel()
                               << " channel in "
                               << beerocks::utils::convert_frequency_type_to_string(
                                      radio->wifi_channel.get_freq_type())
                               << " channels table.";
                    return {};
                }

                auto bandwidth = son::wireless_utils::operating_classes_list.find(*it)->second.band;
                if (bandwidth == beerocks::eWiFiBandwidth::BANDWIDTH_320) {
                    bandwidth = radio->wifi_channel.get_bandwidth();
                }

                auto center_channel_it = channel_it->second.find(bandwidth);
                if (center_channel_it == channel_it->second.end()) {
                    LOG(ERROR) << "Failed find bandwidth of channel "
                               << radio->wifi_channel.get_channel() << " in "
                               << beerocks::utils::convert_frequency_type_to_string(
                                      radio->wifi_channel.get_freq_type())
                               << " channels table.";
                    continue;
                }

                operating_list.emplace_back(
                    std::make_pair(*it, center_channel_it->second.center_channel));
            }
        }

        return operating_list;
    };

    auto operating_list = get_operating_list();
    if (operating_list.empty()) {
        LOG(ERROR) << "Operating list is empty!";
        return false;
    }

    auto operating_channel_report_tlv = cmdu_tx.addClass<wfa_map::tlvOperatingChannelReport>();
    if (!operating_channel_report_tlv) {
        LOG(ERROR) << "addClass ieee1905_1::operating_channel_report_tlv has failed";
        return false;
    }
    operating_channel_report_tlv->radio_uid() = radio_mac;

    for (auto p = std::make_pair(operating_list.begin(), 0); p.first != operating_list.end();
         ++p.first, ++p.second) {
        auto op_classes_list = operating_channel_report_tlv->alloc_operating_classes_list();
        if (!op_classes_list) {
            LOG(ERROR) << "alloc_operating_classes_list() has failed!";
            return false;
        }

        auto operating_class_entry_tuple =
            operating_channel_report_tlv->operating_classes_list(p.second);
        if (!std::get<0>(operating_class_entry_tuple)) {
            LOG(ERROR) << "getting operating class entry has failed!";
            return false;
        }

        auto &operating_class_entry = std::get<1>(operating_class_entry_tuple);

        operating_class_entry.operating_class = p.first->first;
        operating_class_entry.channel_number  = p.first->second;
    }

    operating_channel_report_tlv->current_transmit_power() = radio->tx_power_dB;

    auto wifi6_caps =
        reinterpret_cast<beerocks::net::sWIFI6Capabilities *>(&radio->wifi6_capability);

    if (wifi6_caps->spatial_reuse) {
        auto spatial_reuse_report_tlv = cmdu_tx.addClass<wfa_map::tlvSpatialReuseReport>();
        if (!spatial_reuse_report_tlv) {
            LOG(ERROR) << "addClass wfa_map::tlvSpatialReuseReport has failed";
            return false;
        }

        LOG(DEBUG) << "Adding spatial reuse params to operating channel report for radio : "
                   << radio_mac;

        spatial_reuse_report_tlv->radio_uid() = radio_mac;

        spatial_reuse_report_tlv->flags1().bss_color = radio->spatial_reuse_params.bss_color;
        spatial_reuse_report_tlv->flags1().partial_bss_color =
            radio->spatial_reuse_params.partial_bss_color;
        spatial_reuse_report_tlv->flags2().hesiga_spatial_reuse_value15_allowed =
            (radio->spatial_reuse_params.hesiga_spatial_reuse_value15_allowed ? 1 : 0);
        spatial_reuse_report_tlv->flags2().srg_information_valid =
            (radio->spatial_reuse_params.srg_information_valid ? 1 : 0);
        spatial_reuse_report_tlv->flags2().non_srg_offset_valid =
            (radio->spatial_reuse_params.non_srg_offset_valid ? 1 : 0);
        spatial_reuse_report_tlv->flags2().psr_disallowed =
            (radio->spatial_reuse_params.psr_disallowed ? 1 : 0);
        spatial_reuse_report_tlv->non_srg_obsspd_max_offset() =
            radio->spatial_reuse_params.non_srg_obsspd_max_offset;
        spatial_reuse_report_tlv->srg_obsspd_min_offset() =
            radio->spatial_reuse_params.srg_obsspd_min_offset;
        spatial_reuse_report_tlv->srg_obsspd_max_offset() =
            radio->spatial_reuse_params.srg_obsspd_max_offset;
        spatial_reuse_report_tlv->srg_bss_color_bitmap() =
            radio->spatial_reuse_params.srg_bss_color_bitmap;
        spatial_reuse_report_tlv->srg_partial_bssid_bitmap() =
            radio->spatial_reuse_params.srg_partial_bssid_bitmap;
        spatial_reuse_report_tlv->neighbor_bss_color_in_use_bitmap() =
            radio->spatial_reuse_params.neighbor_bss_color_in_use_bitmap;
    } else {
        LOG(ERROR) << "WiFi 6 capabilities does not support spatial reuse params for radio: "
                   << radio_mac;
    }

    LOG(DEBUG) << "Created Operating Channel Report TLV";
    return true;
}

bool tlvf_utils::add_tlv_channel_scan_capabilities(ieee1905_1::CmduMessageTx &cmdu_tx)
{

    LOG(DEBUG) << "add tlvChannelScanCapabilities to mid:" << std::hex << cmdu_tx.getMessageId();

    auto channel_scan_capabilities_tlv = cmdu_tx.addClass<wfa_map::tlvChannelScanCapabilities>();

    if (!channel_scan_capabilities_tlv) {
        LOG(ERROR) << "Error creating TLV_CHANNEL_SCAN_CAPABILITIES";
        return false;
    }

    std::map<uint8_t, std::vector<uint8_t>> scan_map;

    auto db = AgentDB::get();

    for (auto radio : db->get_radios_list()) {
        if (!radio) {
            LOG(ERROR) << "Error fetching radio";
            continue;
        }

        // prepare radio subsection
        auto radio_channel_scan_capabilities = channel_scan_capabilities_tlv->create_radio_list();
        if (!radio_channel_scan_capabilities) {
            LOG(ERROR) << "create_radio_list() has failed!";
            return false;
        }
        radio_channel_scan_capabilities->radio_uid() = radio->front.iface_mac;
        // On-Boot and On-Request scans are mutually exclusive.
        // certification is the only time on-boot is used, although test case is not mandatory.
        radio_channel_scan_capabilities->capabilities().on_boot_only =
            (db->device_conf.certification_mode && db->device_conf.on_boot_scan > 0);
        // Time slicing impairment (Radio may go off channel for a series of short intervals)
        radio_channel_scan_capabilities->capabilities().scan_impact = wfa_map::
            cRadiosWithScanCapabilities::eScanImpact::SCAN_IMPACT_REDUCED_NUMBER_OF_SPATIAL_STREAM;

        radio_channel_scan_capabilities->minimum_scan_interval() = MINIMUM_SCAN_INTERVAL_SEC;

        get_channel_scan_map(radio, scan_map);
        for (const auto &op_class : scan_map) {
            auto op_class_channels =
                radio_channel_scan_capabilities->create_operating_classes_list();
            if (!op_class_channels) {
                LOG(ERROR) << "create_operating_classes_list() has failed!";
                return false;
            }
            op_class_channels->operating_class() = op_class.first;

            if (op_class.second.size() ==
                son::wireless_utils::operating_classes_list.find(op_class.first)
                    ->second.channels.size()) {
                // if all channels in operating class are supported, set Num_Chan to 0

                op_class_channels->channel_list_length() = 0;
                if (!radio_channel_scan_capabilities->add_operating_classes_list(
                        op_class_channels)) {
                    LOG(ERROR) << "add_operating_classes_list failed";
                }
                continue;
            }

            if (!op_class_channels->set_channel_list(op_class.second.data(),
                                                     op_class.second.size())) {
                LOG(ERROR) << "set_channel_list() fail";
            }
            if (!radio_channel_scan_capabilities->add_operating_classes_list(op_class_channels)) {
                LOG(ERROR) << "add_operating_classes_list failed";
                return false;
            }
        }

        // Push operating class object to the list of operating class objects
        if (!channel_scan_capabilities_tlv->add_radio_list(radio_channel_scan_capabilities)) {
            LOG(ERROR) << "add_radio_list() has failed!";
            return false;
        }
    }

    LOG(DEBUG) << "added tlvChannelScanCapabilities length"
               << channel_scan_capabilities_tlv->length();
    return true;
}

bool tlvf_utils::get_channel_scan_map(beerocks::AgentDB::sRadio *radio,
                                      std::map<uint8_t, std::vector<uint8_t>> &scan_map)
{

    scan_map.clear();

    // prepare supported operating classes
    auto operating_classes = son::wireless_utils::get_operating_classes_of_freq_type(
        radio->wifi_channel.get_freq_type());

    if (operating_classes.empty()) {
        LOG(ERROR) << "Error fetching operating classes for "
                   << tlvf::mac_to_string(radio->front.iface_mac) << " and frequency type "
                   << int(radio->wifi_channel.get_freq_type());
        return false;
    }

    std::vector<uint8_t> channels_available_for_scan;

    for (auto op_class : operating_classes) {
        channels_available_for_scan.clear();
        if (son::wireless_utils::operating_class_to_bandwidth(op_class) !=
            beerocks::eWiFiBandwidth::BANDWIDTH_20) {
            continue;
        }
        auto non_oper_channels =
            get_operating_class_non_oper_channels(radio->channels_list, op_class);
        if (non_oper_channels.size() ==
            son::wireless_utils::operating_classes_list.find(op_class)->second.channels.size()) {
            continue;
        }

        std::vector<uint8_t> op_class_channels_vec(
            son::wireless_utils::operating_classes_list.find(op_class)->second.channels.begin(),
            son::wireless_utils::operating_classes_list.find(op_class)->second.channels.end());

        std::set_difference(
            op_class_channels_vec.begin(), op_class_channels_vec.end(), non_oper_channels.begin(),
            non_oper_channels.end(),
            std::inserter(channels_available_for_scan, channels_available_for_scan.begin()));

        scan_map[op_class] = channels_available_for_scan;
    }
    return true;
}
