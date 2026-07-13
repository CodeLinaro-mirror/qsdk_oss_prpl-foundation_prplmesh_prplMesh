/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_service_helper.h"

#include "ambiorix.h"
#include "bpl_cfg_status.h"
#include <bpl/bpl_cfg.h>
#include <mapf/common/logger.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

/* ============================================================
 *                        Agent Config
 * ============================================================
 */

namespace beerocks {
namespace bpl {

namespace {

constexpr const char *AGENT_INFO_PATH   = AGENT_ROOT_DM ".Info.";
constexpr const char *AGENT_CONFIG_PATH = AGENT_ROOT_DM ".Configuration";

template <typename T> bool read_agent_info_param(const std::string &name, T &value)
{
    return read_param_via_common_socket(AGENT_INFO_PATH, name, value);
}

/*
 * Keep configuration access local when the process owns the Agent NBAPI root. Do not replace it
 * with a synchronous common WBAPI request: owner reads may run before the event loop starts. A
 * ubus round-trip back into the owner therefore risks timeout or deadlock. Common WBAPI is only
 * for non-owning processes, such as fronthaul. This risk follows from process startup ordering;
 * it is not a tested failure mode.
 */
template <typename T> bool read_agent_config_param(const std::string &name, T &value)
{
    auto dm = BplConfigService::instance().nbapi_dm();
    if (dm) {
        if (dm->read_param(AGENT_CONFIG_PATH, name, &value)) {
            return true;
        }

        MAPF_ERR("read_agent_config_param: " + name + " | local NBAPI DM read failed");
        return false;
    }

    // prplMesh processes that do not own an NBAPI DM read the Agent configuration
    // exported on the common WBAPI bus.
    return read_param_via_common_socket(std::string(AGENT_CONFIG_PATH) + ".", name, value);
}

} // namespace

int cfg_get_management_mode_agent_info(std::string &mode)
{
    return read_agent_info_param("ManagementMode", mode) ? RETURN_OK : RETURN_ERR;
}

bool bpl_cfg_get_agent_mac(std::string &agent_mac)
{
    if (!read_agent_info_param("MACAddress", agent_mac)) {
        LOG(ERROR) << "Failed to read agent MAC address from Agent Info DM";
        return false;
    }

    return true;
}

int cfg_get_stop_on_failure_attempts()
{
    int stop_on_failure_attempts{0};
    read_agent_config_param("StopOnFailureAttempts", stop_on_failure_attempts);
    return stop_on_failure_attempts;
}

bool cfg_get_zwdfs_flag(int &flag) { return read_agent_config_param("ZeroWaitDFSFlag", flag); }

bool cfg_get_best_channel_rank_threshold(uint32_t &threshold)
{
    return read_agent_config_param("BestChannelRankThreshold", threshold);
}

bool cfg_get_multi_chan_bcn_req_duration(uint16_t &duration)
{
    return read_agent_config_param("MultiChanBcnReqDuration", duration);
}

bool bpl_cfg_get_backhaul_wire_iface(std::string &iface)
{
    return read_agent_config_param("BackhaulWireInterface", iface);
}

bool bpl_cfg_get_agent_multi_ap_profile(uint32_t &profile)
{
    return read_agent_config_param("MultiAPProfile", profile);
}

int cfg_get_preferred_radio_band(int *preferred_radio_band)
{
    if (!preferred_radio_band) {
        return RETURN_OK;
    }

    *preferred_radio_band = BPL_RADIO_BAND_AUTO;

    std::string preferred_bh_band;
    if (!read_agent_config_param("BackhaulBand", preferred_bh_band)) {
        return RETURN_ERR;
    }

    if (preferred_bh_band == "2.4GHz") {
        *preferred_radio_band = BPL_RADIO_BAND_2G;
    } else if (preferred_bh_band == "5GHz") {
        *preferred_radio_band = BPL_RADIO_BAND_5G;
    } else if (preferred_bh_band == "6GHz") {
        *preferred_radio_band = BPL_RADIO_BAND_6G;
    } else if (preferred_bh_band != "auto") {
        MAPF_ERR("cfg_get_preferred_radio_band: unknown BackhaulBand value\n");
        return RETURN_ERR;
    }

    return RETURN_OK;
}

bool cfg_get_clients_measurement_mode(eClientsMeasurementMode &clients_measurement_mode)
{
    int mode_value = static_cast<int>(eClientsMeasurementMode::ENABLE_ALL);
    if (!read_agent_config_param("ClientsMeasurementMode", mode_value)) {
        MAPF_WARN("cfg_get_clients_measurement_mode: ClientsMeasurementMode unavailable, "
                  "using default\n");
        clients_measurement_mode = eClientsMeasurementMode::ENABLE_ALL;
        return true;
    }

    if (mode_value < static_cast<int>(eClientsMeasurementMode::DISABLE_ALL) ||
        mode_value >
            static_cast<int>(eClientsMeasurementMode::ONLY_CLIENTS_SELECTED_FOR_STEERING)) {
        MAPF_ERR("cfg_get_clients_measurement_mode: unknown ClientsMeasurementMode value\n");
        return false;
    }

    clients_measurement_mode = static_cast<eClientsMeasurementMode>(mode_value);
    return true;
}

int cfg_get_beerocks_credentials(const int radio_dir, char ssid[BPL_SSID_LEN],
                                 char pass[BPL_PASS_LEN], char sec[BPL_SEC_LEN])
{
    auto safe_copy = [](const std::string &in, char *out, size_t max_len) {
        size_t copy_len = std::min(in.size(), max_len - 1);
        std::memcpy(out, in.data(), copy_len);
        out[copy_len] = '\0';
    };

    bool success = true;
    std::string tmp;

    // SSID
    if (!read_agent_config_param("SSID", tmp)) {
        LOG(WARNING) << "cfg: missing SSID";
        success = false;
    } else {
        safe_copy(tmp, ssid, BPL_SSID_LEN);
    }

    // Security
    if (!read_agent_config_param("Security", tmp)) {
        LOG(WARNING) << "cfg: missing Security";
        success = false;
    } else {
        safe_copy(tmp, sec, BPL_SEC_LEN);
    }

    // Passphrase or WEPKey depending on security
    if (tmp == "WEP-64" || tmp == "WEP-128") {
        if (!read_agent_config_param("WEPKey", tmp)) {
            LOG(WARNING) << "cfg: missing WEPKey";
            success = false;
        }
    } else {
        if (!read_agent_config_param("Passphrase", tmp)) {
            LOG(WARNING) << "cfg: missing Passphrase";
            success = false;
        }
    }
    safe_copy(tmp, pass, BPL_PASS_LEN);

    return success ? RETURN_OK : RETURN_ERR;
}

bool cfg_get_private_bridge_iface(std::string &bridge_iface)
{
    if (!read_agent_config_param("PrivateBridgeIface", bridge_iface)) {
        LOG(ERROR) << "failed to read PrivateBridgeIface";
        return false;
    }
    return true;
}

bool cfg_get_guest_bridge_iface(std::string &bridge_iface)
{
    if (!read_agent_config_param("GuestBridgeIface", bridge_iface)) {
        LOG(ERROR) << "failed to read GuestBridgeIface";
        return false;
    }
    return true;
}

} // namespace bpl
} // namespace beerocks
