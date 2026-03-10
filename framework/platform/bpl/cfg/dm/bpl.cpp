/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bpl/bpl.h>
#include <cstdlib>

#include <mapf/common/logger.h>

#include "bpl_amb_ptr.h"
#include "bpl_cfg_pwhm.h"

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Implementation ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

namespace beerocks {
namespace bpl {

beerocks::wbapi::AmbiorixClient m_ambiorix_cl;
beerocks::wbapi::AmbiorixClient m_ambiorix_cl_ubus;

void m_ambiorix_cl_destructor() { m_ambiorix_cl.~AmbiorixClient(); }
void m_ambiorix_cl_ubus_destructor() { m_ambiorix_cl_ubus.~AmbiorixClient(); }

int bpl_init()
{
    LOG_IF(!m_ambiorix_cl.connect(AMBIORIX_USP_BACKEND_PATH, AMBIORIX_PWHM_USP_BACKEND_URI), FATAL)
        << "Unable to connect to the ambiorix backend!";

    // Keep a separate ubus connection for non-pwhm external DMs (for example DeviceInfo/PM).
    LOG_IF(!m_ambiorix_cl_ubus.connect(AMBIORIX_WBAPI_BACKEND_PATH, AMBIORIX_WBAPI_BUS_URI),
           WARNING)
        << "Unable to connect to WBAPI ubus backend, external non-pwhm DM reads may fail";

    std::atexit(m_ambiorix_cl_destructor);
    std::atexit(m_ambiorix_cl_ubus_destructor);
    return RETURN_OK;
}

void bpl_close()
{
    if (amb_ptr) {
        amb_ptr.reset();
    }
}

} // namespace bpl
} // namespace beerocks
