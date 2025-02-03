/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BEEROCKS_AMX_RACE_CONDITION_H_
#define _BEEROCKS_AMX_RACE_CONDITION_H_

#include <mutex>

namespace beerocks {

extern std::mutex amxp_signal_read_mutex;
};

#endif
