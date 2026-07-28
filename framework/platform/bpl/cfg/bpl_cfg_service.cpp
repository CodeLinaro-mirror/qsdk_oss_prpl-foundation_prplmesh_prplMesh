/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_service.h"
#include "bpl_cfg_backend.h"
#include "bpl_cfg_status.h"

#include <utility>

namespace beerocks {
namespace bpl {

BplConfigService &BplConfigService::instance()
{
    static BplConfigService s_instance;
    return s_instance;
}

void BplConfigService::set_backend(std::unique_ptr<BplConfigBackend> backend)
{
    shutdown();
    m_backend = std::move(backend);
}

bool BplConfigService::backend_configured() const { return static_cast<bool>(m_backend); }

int BplConfigService::init()
{
    if (m_is_initialized) {
        return RETURN_OK;
    }
    if (!m_backend) {
        return RETURN_ERR;
    }

    auto ret = m_backend->init();
    if (ret == RETURN_OK) {
        m_is_initialized = true;
    }
    return ret;
}

void BplConfigService::shutdown()
{
    if (!m_backend) {
        return;
    }

    if (m_is_initialized) {
        m_backend->shutdown();
    }
    m_backend.reset();
    m_is_initialized = false;
}

wbapi::AmbiorixClient *BplConfigService::common_client()
{
    return m_backend ? m_backend->common_client() : nullptr;
}

void BplConfigService::set_nbapi_dm(std::shared_ptr<nbapi::Ambiorix> dm)
{
    if (!m_backend) {
        return;
    }
    m_backend->set_nbapi_dm(std::move(dm));
}

std::shared_ptr<nbapi::Ambiorix> BplConfigService::nbapi_dm()
{
    if (!m_backend) {
        return {};
    }
    return m_backend->nbapi_dm();
}

} // namespace bpl
} // namespace beerocks
