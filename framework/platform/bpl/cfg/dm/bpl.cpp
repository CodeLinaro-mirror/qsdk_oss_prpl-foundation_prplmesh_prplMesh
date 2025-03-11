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

#include "bpl_cfg_pwhm.h"

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Implementation ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

namespace beerocks {
namespace bpl {

beerocks::wbapi::AmbiorixClient m_ambiorix_cl;

void m_ambiorix_cl_destructor() { m_ambiorix_cl.~AmbiorixClient(); }

int bpl_init()
{
    LOG_IF(!m_ambiorix_cl.connect(AMBIORIX_USP_BACKEND_PATH, AMBIORIX_PWHM_USP_BACKEND_URI), FATAL)
        << "Unable to connect to the ambiorix backend!";

    std::atexit(m_ambiorix_cl_destructor);
    return RETURN_OK;
}

void bpl_close()
{
    // Do nothing
}

} // namespace bpl
} // namespace beerocks
