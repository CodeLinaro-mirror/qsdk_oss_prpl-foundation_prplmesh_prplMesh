/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BPL_CFG_AMX_HELPER_H_
#define _BPL_CFG_AMX_HELPER_H_

#include <string>

#include <ambiorix.h>

namespace beerocks {
namespace bpl {

extern std::shared_ptr<beerocks::nbapi::Ambiorix> amb_ptr;

template <typename T> bool set_controller_config_param(const std::string &name, const T &value);

template <typename T> bool set_agent_config_param(const std::string &name, const T &value);

template <typename T> bool read_controller_config_param(const std::string &name, T &value);

template <typename T> bool read_agent_config_param(const std::string &name, T &value);

} // namespace bpl
} // namespace beerocks

#endif // _BPL_CFG_AMX_HELPER_H_
