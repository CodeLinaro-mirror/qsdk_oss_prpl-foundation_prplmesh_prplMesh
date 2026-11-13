/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef OPTIONAL_SUBOBJECTS_H
#define OPTIONAL_SUBOBJECTS_H

#include <ambiorix_impl.h>

#include <vector>

namespace prplmesh {
namespace controller {
namespace actions {

/**
 * @brief The mibs that the controller attaches with add_optional_subobject(), and the objects
 * they extend.
 *
 * @return The list of mibs, for AmbiorixImpl.
 */
std::vector<beerocks::nbapi::sOptionalSubobject> get_optional_subobject_list(void);

} // namespace actions
} // namespace controller
} // namespace prplmesh

#endif // OPTIONAL_SUBOBJECTS_H
