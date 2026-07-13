/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "bpl_cfg_backend_uci.h"

#include "bpl_cfg_status.h"

namespace beerocks {
namespace bpl {

/**
 * @brief UCI implementation of BPL cfg backend.
 */
class BplConfigBackendUci : public BplConfigBackend {
public:
    /**
     * @brief Initialize UCI cfg backend.
     *
     * @return RETURN_OK.
     */
    int init() override
    {
        // UCI backend does not expose DM/WBAPI clients.
        return RETURN_OK;
    }

    /**
     * @brief Shut down UCI cfg backend.
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
     * @return Empty pointer for UCI backend.
     */
    std::shared_ptr<nbapi::Ambiorix> nbapi_dm() override { return {}; }
};

/**
 * @brief Create UCI cfg backend.
 *
 * @return UCI backend instance.
 */
std::unique_ptr<BplConfigBackend> create_backend_uci()
{
    return std::make_unique<BplConfigBackendUci>();
}

} // namespace bpl
} // namespace beerocks
