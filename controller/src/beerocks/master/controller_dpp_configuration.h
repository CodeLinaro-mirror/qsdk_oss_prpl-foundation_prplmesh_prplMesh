/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _CONTROLLER_DPP_CONFIGURATION_H_
#define _CONTROLLER_DPP_CONFIGURATION_H_

#include "controller_dpp_protocol.h"
#include "db/db.h"

#include <memory>
#include <string>
#include <vector>

namespace son {
namespace controller_dpp {

struct DppBackhaulStaConfiguration {
    std::string ssid;
    std::string akm;
    std::string passphrase;
    std::string psk_hex;
    std::string security_ies_hex;
    bool ssid_advertisement_valid = false;
    bool ssid_advertisement       = true;
};

struct DppStaConfiguration {
    std::string ssid;
    std::string akm;
    std::string passphrase;
    std::string psk_hex;
    std::string security_ies_hex;
    bool ssid_advertisement_valid = false;
    bool ssid_advertisement       = true;
};

/**
 * @brief Select backhaul BSS credentials from Controller BSS policy
 * (get_configured_bss_info / get_bss_info_configuration).
 */
bool get_dpp_backhaul_sta_configuration(db &database, const std::shared_ptr<Agent> &agent,
                                        const std::string &ssid_hex,
                                        const std::string &requested_backhaul_akm,
                                        DppBackhaulStaConfiguration &configuration,
                                        std::string &error);

/**
 * @brief Select fronthaul BSS credentials for netRole=sta (M-9: no backhaul creds).
 */
bool get_dpp_sta_configuration(db &database, const std::shared_ptr<Agent> &agent,
                               const std::string &ssid_hex, DppStaConfiguration &configuration,
                               std::string &error);

/**
 * @brief FEAT-68 C-4: build Configuration Object JSON list from Controller BSS policy.
 *
 * Roles (Architecture / M-5 / M-9):
 *   - mapAgent: always for Multi-AP Enrollee Config Request
 *   - mapBackhaulSta: when requested in the Config Request (or nested bSTAList)
 *   - sta: infrastructure STA path using fronthaul BSS only
 *
 * Signing uses Controller C-sign from the DPP keystore (0016) and
 * build_dpp_configuration_object / build_connector_payload (0004).
 * Optional keystore fields pp_key / group_id from [dpp_controller_keys] are used when present.
 */
bool build_dpp_configuration_objects_from_policy(db &database, DppConfiguratorSession &session,
                                                 const sMacAddr &agent_mac,
                                                 const std::string &configuration_request_json,
                                                 std::vector<std::string> &config_objects,
                                                 bool &send_conn_status, std::string &error);

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_CONFIGURATION_H_
