/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BEEROCKS_VERSION_H_
#define _BEEROCKS_VERSION_H_

#include "beerocks_defines.h"

#include <string>

namespace beerocks {

typedef struct sBinaryVersion {
    uint8_t major;
    uint8_t minor;
    uint16_t build_number;
} sBinaryVersion;

class version {
public:
    static constexpr sBinaryVersion INVALID_VERSION  = {};
    static constexpr char INVALID_VERSION_STRING[]   = "0.0.0";
    static constexpr char INVALID_TIMESTAMP_STRING[] = "00/00/00--00:00";

    static void print_version(bool verbose, const std::string &name,
                              const std::string &description = std::string());
    static bool handle_version_query(int argc, char **argv,
                                     const std::string &description = std::string());
    static void log_version(int argc, char **argv, const std::string &logger_id = std::string());

    // Version, build date and revision of prplMesh, set at configure time.
    // Only beerocks_version.cpp is compiled with them, so that a new build date or revision
    // doesn't rebuild every user.
    static const char *get_module_version();
    static const char *get_module_timestamp();
    static const char *get_module_revision();
    static std::string version_to_string(const sBinaryVersion &version);
    static sBinaryVersion version_from_string(const std::string &version);
};
} // namespace beerocks

#endif // _BEEROCKS_VERSION_H_
