/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <mapf/common/amx_mutex.h>

namespace beerocks {

std::recursive_mutex &amx_mutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

} // namespace beerocks
