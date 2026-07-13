/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bpl/bpl.h>
#include <bpl/bpl_cfg.h>

#include "bpl_cfg_backend_dm.h"
#include "bpl_cfg_service.h"

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Implementation ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

namespace beerocks {
namespace bpl {

void set_nbapi_dm(const std::shared_ptr<beerocks::nbapi::Ambiorix> &dm)
{
    auto &cfg_service = BplConfigService::instance();
    if (!cfg_service.backend_configured()) {
        cfg_service.set_backend(create_backend_dm());
    }
    cfg_service.set_nbapi_dm(dm);
}

int bpl_init()
{
    auto &cfg_service = BplConfigService::instance();
    if (!cfg_service.backend_configured()) {
        cfg_service.set_backend(create_backend_dm());
    }
    return cfg_service.init();
}

void bpl_close() { BplConfigService::instance().shutdown(); }

} // namespace bpl
} // namespace beerocks
