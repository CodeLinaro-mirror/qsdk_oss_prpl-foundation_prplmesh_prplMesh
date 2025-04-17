/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_AXM_H_
#define _BPL_AXM_H_

#include <memory>

#include <ambiorix.h>

namespace beerocks {
namespace bpl {

void set_ambiorix_impl_ptr(const std::shared_ptr<beerocks::nbapi::Ambiorix> &ptr);

} // namespace bpl
} // namespace beerocks

#endif // _BPL_AXM_H_
