/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bpl/bpl_amx.h>

#include <memory>

#include "bpl_amb_ptr.h"

namespace beerocks {
namespace bpl {

std::shared_ptr<beerocks::nbapi::Ambiorix> amb_ptr = nullptr;

void set_ambiorix_impl_ptr(const std::shared_ptr<beerocks::nbapi::Ambiorix> &ptr) { amb_ptr = ptr; }

} // namespace bpl
} // namespace beerocks
