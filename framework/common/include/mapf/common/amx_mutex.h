/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef MAPF_COMMON_AMX_MUTEX_H_
#define MAPF_COMMON_AMX_MUTEX_H_

#include <mutex>

namespace beerocks {

/**
 * @brief The process-wide mutex for shared Ambiorix state.
 *
 * Defined in mapfcommon so NBAPI and WBAPI use the same mutex. Recursive locking
 * allows callbacks dispatched by AMX to re-enter the wrappers on the same thread.
 */
std::recursive_mutex &amx_mutex();

/**
 * @brief Guard a complete operation on shared Ambiorix state.
 *
 * Keep the guard alive from borrowed-object lookup through its last use, including
 * transaction cleanup and callback dispatch. Acquire it before any connection,
 * manager or application lock, and never hold it while joining another thread.
 * Private, detached variants do not need this guard.
 */
class AmxGuard {
public:
    AmxGuard() : m_lock(amx_mutex()) {}

private:
    std::lock_guard<std::recursive_mutex> m_lock;
};

} // namespace beerocks

#endif // MAPF_COMMON_AMX_MUTEX_H_
