/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_CFG_BACKEND_DM_H_
#define _BPL_CFG_BACKEND_DM_H_

#include "bpl_cfg_backend.h"

#include <memory>

namespace beerocks {
namespace bpl {

/**
 * @brief Create DM cfg backend.
 *
 * @return DM backend instance.
 */
std::unique_ptr<BplConfigBackend> create_backend_dm();

} // namespace bpl
} // namespace beerocks

#endif // _BPL_CFG_BACKEND_DM_H_
