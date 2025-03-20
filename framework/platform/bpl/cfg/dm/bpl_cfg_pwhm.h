/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_CFG_PRIVATE_H_
#define _BPL_CFG_PRIVATE_H_

#include <memory>
#include <stdint.h>
#include <string>

#include "ambiorix_client.h"

#define RETURN_OK 0
#define RETURN_ERR -1

namespace beerocks {
namespace bpl {

constexpr char DEFAULT_DM_LAN_INTERFACE_NAMES[] = "eth0_1 eth0_2 eth0_3 eth0_4 lan0 lan1 lan2 lan3";

extern beerocks::wbapi::AmbiorixClient m_ambiorix_cl;

} // namespace bpl
} // namespace beerocks

#endif /* _BPL_CFG_PRIVATE_H_ */
