/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_backend_linux.h"

#include "bpl_cfg_status.h"

namespace beerocks {
namespace bpl {

/**
 * @brief Linux implementation of BPL cfg backend.
 */
class BplConfigBackendLinux : public BplConfigBackend {
public:
    /**
     * @brief Initialize Linux cfg backend.
     *
     * @return RETURN_OK.
     */
    int init() override
    {
        // Linux backend does not expose DM/WBAPI clients.
        return RETURN_OK;
    }

    /**
     * @brief Shut down Linux cfg backend.
     */
    void shutdown() override
    {
        // Do nothing
    }

    /**
     * @brief Get unsupported common DM client.
     *
     * @return nullptr.
     */
    wbapi::AmbiorixClient *common_client() override { return nullptr; }

    /**
     * @brief Get NBAPI data model.
     *
     * @return Empty pointer for Linux backend.
     */
    std::shared_ptr<nbapi::Ambiorix> nbapi_dm() override { return {}; }
};

/**
 * @brief Create Linux cfg backend.
 *
 * @return Linux backend instance.
 */
std::unique_ptr<BplConfigBackend> create_backend_linux()
{
    return std::make_unique<BplConfigBackendLinux>();
}

} // namespace bpl
} // namespace beerocks
