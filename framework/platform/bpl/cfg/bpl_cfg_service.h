/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_CFG_SERVICE_H_
#define _BPL_CFG_SERVICE_H_

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

class BplConfigBackend;

/**
 * @brief Singleton facade for BPL cfg backend access.
 */
class BplConfigService {
public:
    /**
     * @brief Get singleton service instance.
     *
     * @return BPL cfg service instance.
     */
    static BplConfigService &instance();

    /**
     * @brief Copy construction is not allowed.
     */
    BplConfigService(const BplConfigService &) = delete;

    /**
     * @brief Copy assignment is not allowed.
     */
    BplConfigService &operator=(const BplConfigService &) = delete;

    /**
     * @brief Replace configured backend.
     *
     * @param backend Backend instance to own.
     */
    void set_backend(std::unique_ptr<BplConfigBackend> backend);

    /**
     * @brief Check whether backend is configured.
     *
     * @return true when backend exists, otherwise false.
     */
    bool backend_configured() const;

    /**
     * @brief Initialize configured backend.
     *
     * @return RETURN_OK on success, RETURN_ERR on error.
     */
    int init();

    /**
     * @brief Shut down and remove configured backend.
     */
    void shutdown();

    /**
     * @brief Get backend client for common-bus DM access.
     *
     * @return Common Ambiorix client, or nullptr when unsupported.
     */
    wbapi::AmbiorixClient *common_client();

    /**
     * @brief Register process-local NBAPI data model.
     *
     * @param dm NBAPI data model object.
     */
    void set_nbapi_dm(std::shared_ptr<nbapi::Ambiorix> dm);

    /**
     * @brief Get process-local NBAPI data model.
     *
     * @return NBAPI data model object, or empty pointer when unsupported.
     */
    std::shared_ptr<nbapi::Ambiorix> nbapi_dm();

private:
    /**
     * @brief Construct singleton service instance.
     */
    BplConfigService() = default;

    std::unique_ptr<BplConfigBackend> m_backend;
    bool m_is_initialized = false;
};

} // namespace bpl
} // namespace beerocks

#endif // _BPL_CFG_SERVICE_H_
