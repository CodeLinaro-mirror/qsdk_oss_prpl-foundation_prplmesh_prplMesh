/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef PUSH_BUTTON_CONFIG_ATTACH_H
#define PUSH_BUTTON_CONFIG_ATTACH_H

#include "../tasks/pbc_onboarding_task.h"

#include <tlvf/WSC/eWscVendorExt.h>

#include <easylogging++.h>

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace beerocks {

/**
 * @brief Attaches the Push-Button Configs of an AP-Autoconfiguration WSC message to its M2s.
 *
 * A config is attached only to the single M2 with its BSS_Index, and only when that M2 is for a
 * fronthaul-only BSS and does not tear the BSSs down. A BSS_Index of more than one Push-Button
 * Config TLV of the message selects no M2. Each config that is not attached is logged at ERROR.
 *
 * @tparam BssConfig BSS settings of one M2, with the members m2_config.bss_index,
 * payload_config.bss_type, payload_config.ssid, wps_push_button and pbc_listen_duration.
 * @param radio_iface Interface name of the radio, for the log.
 * @param configs The readable Push-Button Configs of the message, in the order of the message.
 * @param tlvs_per_bss_index Number of Push-Button Config TLVs of the message for each BSS_Index,
 * also the TLVs that cannot be read.
 * @param[in,out] bss_infos BSS settings of the M2s of the message. An M2 that gets a config gets
 * its WPS push-button settings and its Listen Duration.
 * @return SSID and Listen Duration of each BSS that gets push-button settings.
 */
template <class BssConfig>
std::vector<std::pair<std::string, uint16_t>>
attach_push_button_configs_to_m2s(const std::string &radio_iface,
                                  const std::vector<PBCOnboardingTask::sPushButtonConfig> &configs,
                                  const std::map<uint8_t, size_t> &tlvs_per_bss_index,
                                  std::vector<BssConfig> &bss_infos)
{
    std::vector<std::pair<std::string, uint16_t>> listen_durations;
    for (const auto &config : configs) {
        std::stringstream tlv_name;
        tlv_name << "Push-Button Config TLV (radio " << radio_iface << ", BSS_Index "
                 << int(config.bss_index) << ")";

        const auto count_it   = tlvs_per_bss_index.find(config.bss_index);
        const auto same_index = (count_it == tlvs_per_bss_index.end()) ? 0 : count_it->second;
        if (same_index > 1) {
            LOG(ERROR) << tlv_name.str() << " not attached: " << same_index
                       << " Push-Button Config TLVs have this BSS_Index";
            continue;
        }

        std::vector<BssConfig *> matches;
        for (auto &info : bss_infos) {
            if (info.m2_config.bss_index == config.bss_index) {
                matches.push_back(&info);
            }
        }
        if (matches.size() != 1) {
            LOG(ERROR) << tlv_name.str() << " not attached: the BSS_Index selects "
                       << matches.size() << " M2s of the message, expected 1";
            continue;
        }

        auto &info          = *matches.front();
        const auto bss_type = info.payload_config.bss_type;
        if ((bss_type & WSC::eWscVendorExtSubelementBssType::TEARDOWN) != 0) {
            LOG(ERROR) << tlv_name.str() << " not attached: the M2 tears the BSSs down";
            continue;
        }
        if ((bss_type & WSC::eWscVendorExtSubelementBssType::FRONTHAUL_BSS) == 0 ||
            (bss_type & WSC::eWscVendorExtSubelementBssType::BACKHAUL_BSS) != 0) {
            LOG(ERROR) << tlv_name.str() << " not attached: the M2 of SSID '"
                       << info.payload_config.ssid << "' is not for a fronthaul-only BSS";
            continue;
        }

        info.wps_push_button.armed              = true;
        info.wps_push_button.uuid_e             = config.uuid_e;
        info.wps_push_button.device_password_id = config.device_password_id;
        info.wps_push_button.device_password    = config.device_password;
        info.pbc_listen_duration                = config.listen_duration;
        listen_durations.emplace_back(info.payload_config.ssid, config.listen_duration);

        LOG(INFO) << tlv_name.str() << " attached to the M2 of SSID '" << info.payload_config.ssid
                  << "': UUID-E " << config.uuid_e << ", Listen Duration " << config.listen_duration
                  << " s, Device Password ID " << std::hex << config.device_password_id << std::dec
                  << ", Device Password "
                  << (config.device_password.empty() ? "absent" : "present");
    }
    return listen_durations;
}

} // namespace beerocks

#endif
