/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _CONTROLLER_DPP_SESSION_H_
#define _CONTROLLER_DPP_SESSION_H_

#include <bcl/network/network_utils.h>
#include <cstdint>
#include <string>
#include <vector>

namespace son {
namespace controller_dpp {

struct SessionState {
    sMacAddr proxy_agent            = beerocks::net::network_utils::ZERO_MAC;
    sMacAddr last_chirp_enrollee    = beerocks::net::network_utils::ZERO_MAC;
    bool last_chirp_enrollee_valid   = false;
    bool authentication_confirm_sent = false;
    bool configuration_request_received = false;
    std::string configuration_request_json;
    std::string requested_net_role;
    bool configuration_result_received = false;
    uint8_t configuration_result_status = 0;
    bool connection_status_result_received = false;
    uint8_t connection_status_result = 0;
    std::vector<std::string> pending_configuration_objects;
    bool pending_send_conn_status = false;
    bool configuration_response_sent = false;

};

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_SESSION_H_
