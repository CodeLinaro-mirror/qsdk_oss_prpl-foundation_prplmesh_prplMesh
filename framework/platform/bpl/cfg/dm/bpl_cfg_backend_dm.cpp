/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_backend_dm.h"

#include "ambiorix_client.h"
#include "bpl_cfg_status.h"

#include <mapf/common/logger.h>

#include <utility>

namespace beerocks {
namespace bpl {

/**
 * @brief DM implementation of BPL cfg backend.
 */
class BplConfigBackendDm : public BplConfigBackend {
public:
    /**
     * @brief Connect the common WBAPI bus client.
     *
     * @return RETURN_OK on success, RETURN_ERR on error.
     */
    int init() override
    {
        if (!m_common_client.connect(AMBIORIX_WBAPI_BACKEND_PATH, AMBIORIX_WBAPI_BUS_URI)) {
            LOG(ERROR) << "Unable to connect to common WBAPI bus";
            return RETURN_ERR;
        }
        return RETURN_OK;
    }

    /**
     * @brief Release local NBAPI data model reference.
     */
    void shutdown() override { m_nbapi_dm.reset(); }

    /**
     * @brief Get common WBAPI bus client.
     *
     * @return Common Ambiorix client.
     */
    wbapi::AmbiorixClient *common_client() override { return &m_common_client; }

    /**
     * @brief Store process-local NBAPI data model.
     *
     * @param dm NBAPI data model object.
     */
    void set_nbapi_dm(std::shared_ptr<nbapi::Ambiorix> dm) override { m_nbapi_dm = std::move(dm); }

    /**
     * @brief Get process-local NBAPI data model.
     *
     * @return NBAPI data model object.
     */
    std::shared_ptr<nbapi::Ambiorix> nbapi_dm() override { return m_nbapi_dm; }

private:
    wbapi::AmbiorixClient m_common_client;
    std::shared_ptr<nbapi::Ambiorix> m_nbapi_dm;
};

/**
 * @brief Create DM cfg backend.
 *
 * @return DM backend instance.
 */
std::unique_ptr<BplConfigBackend> create_backend_dm()
{
    return std::make_unique<BplConfigBackendDm>();
}

} // namespace bpl
} // namespace beerocks
