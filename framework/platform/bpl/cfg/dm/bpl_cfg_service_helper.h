/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_CFG_SERVICE_HELPER_H_
#define _BPL_CFG_SERVICE_HELPER_H_

#include <string>

#include <mapf/common/logger.h>

#include "ambiorix_client.h"
#include "bpl_cfg_service.h"

namespace beerocks {
namespace bpl {

/* Common WBAPI bus helpers */

inline std::string normalize_path(std::string path)
{
    static const std::string device_prefix("Device.");
    if (path.rfind(device_prefix, 0) == 0) {
        path.erase(0, device_prefix.length());
    }
    return path;
}

template <typename T>
bool read_param_via_common_socket(const std::string &object_path, const std::string &name, T &value)
{
    auto *client = BplConfigService::instance().common_client();
    LOG_IF(!client, ERROR) << "BPL common Ambiorix client is not initialized";
    return client && client->get_param(value, normalize_path(object_path), name);
}

inline wbapi::AmbiorixVariantSmartPtr get_object_via_common_socket(const std::string &object_path)
{
    auto *client = BplConfigService::instance().common_client();
    LOG_IF(!client, ERROR) << "BPL common Ambiorix client is not initialized";
    return client ? client->get_object(normalize_path(object_path))
                  : wbapi::AmbiorixVariantSmartPtr{};
}

inline wbapi::AmbiorixVariantMapSmartPtr
get_object_multi_via_common_socket(const std::string &object_path)
{
    auto *client = BplConfigService::instance().common_client();
    LOG_IF(!client, ERROR) << "BPL common Ambiorix client is not initialized";
    return client ? client->get_object_multi<wbapi::AmbiorixVariantMapSmartPtr>(
                        normalize_path(object_path))
                  : wbapi::AmbiorixVariantMapSmartPtr{};
}

inline bool resolve_path_via_common_socket(const std::string &search_path,
                                           std::string &absolute_path,
                                           const char *caller = __builtin_FUNCTION())
{
    auto *client = BplConfigService::instance().common_client();
    if (!client) {
        LOG(ERROR) << caller << ": BPL common Ambiorix client is not initialized";
        return false;
    }

    const auto normalized_path = normalize_path(search_path);
    const auto success         = client->resolve_path(normalized_path, absolute_path);
    LOG_IF(!success, ERROR) << caller << ": failed to resolve path " << normalized_path
                            << " via BPL common Ambiorix client";
    return success;
}

} // namespace bpl
} // namespace beerocks

#endif // _BPL_CFG_SERVICE_HELPER_H_
