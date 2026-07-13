/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_CFG_BACKEND_H_
#define _BPL_CFG_BACKEND_H_

#include <memory>

namespace beerocks {
namespace nbapi {
class Ambiorix;
}
namespace wbapi {
class AmbiorixClient;
}
} // namespace beerocks

namespace beerocks {
namespace bpl {

/**
 * @brief Interface for platform-specific BPL cfg backends.
 */
class BplConfigBackend {
public:
    /**
     * @brief Destroy the backend instance.
     */
    virtual ~BplConfigBackend() = default;

    /**
     * @brief Initialize backend resources.
     *
     * @return RETURN_OK on success, RETURN_ERR on error.
     */
    virtual int init() = 0;

    /**
     * @brief Release backend runtime resources.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Get backend client for common-bus DM access.
     *
     * @return Common Ambiorix client, or nullptr when unsupported.
     */
    virtual wbapi::AmbiorixClient *common_client() = 0;

    /**
     * @brief Register process-local NBAPI data model.
     */
    virtual void set_nbapi_dm(std::shared_ptr<nbapi::Ambiorix>) {}

    /**
     * @brief Get process-local NBAPI data model.
     *
     * @return NBAPI data model object, or empty pointer when unsupported.
     */
    virtual std::shared_ptr<nbapi::Ambiorix> nbapi_dm() { return {}; }
};

} // namespace bpl
} // namespace beerocks

#endif // _BPL_CFG_BACKEND_H_
