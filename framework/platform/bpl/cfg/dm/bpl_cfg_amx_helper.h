/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_CFG_AMX_HELPER_H_
#define _BPL_CFG_AMX_HELPER_H_

#include "bpl_cfg_pwhm.h"

#include <ambiorix_variant.h>

using beerocks::wbapi::AmbiorixVariantSmartPtr;

namespace beerocks {
namespace bpl {

AmbiorixVariantSmartPtr get_controller_dm();

AmbiorixVariantSmartPtr get_agent_dm();

} // namespace bpl
} // namespace beerocks

#endif // _BPL_CFG_AMX_HELPER_H_
