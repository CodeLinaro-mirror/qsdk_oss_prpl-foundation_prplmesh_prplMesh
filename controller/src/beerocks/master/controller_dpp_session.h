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

namespace son {
namespace controller_dpp {

struct SessionState {
    sMacAddr proxy_agent            = beerocks::net::network_utils::ZERO_MAC;
    sMacAddr last_chirp_enrollee    = beerocks::net::network_utils::ZERO_MAC;
    bool last_chirp_enrollee_valid   = false;
};

} // namespace controller_dpp
} // namespace son

#endif // _CONTROLLER_DPP_SESSION_H_
