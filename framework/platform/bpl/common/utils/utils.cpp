/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "utils.h"

#include <arpa/inet.h>
#include <cstring>
#include <linux/if_bridge.h>
#include <net/if.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

#include <mapf/common/logger.h>

namespace beerocks {
namespace bpl {
namespace utils {

static const std::string WHITESPACE_CHARS(" \t\n\r\f\v");

void ltrim(std::string &str, const std::string &additional_chars)
{
    str.erase(0, str.find_first_not_of(WHITESPACE_CHARS + additional_chars));
}

void rtrim(std::string &str, const std::string &additional_chars)
{
    str.erase(str.find_last_not_of(WHITESPACE_CHARS + additional_chars) + 1);
}

void trim(std::string &str, const std::string &additional_chars)
{
    ltrim(str, additional_chars);
    rtrim(str, additional_chars);
}

int64_t stoi(std::string str)
{
    std::stringstream val_s(str);
    int64_t val;
    val_s >> val;
    if (val_s.fail()) {
        MAPF_WARN("stoi(), cannot convert \"" << str << "\" to int64_t");
        return 0;
    }
    return val;
}

std::string int_to_hex_string(const unsigned int integer, const uint8_t number_of_digits)
{
    // 'number_of_digits' represent how much digits the number should have, so the function will
    // pad the number with zeroes from left, if necessary.
    // for example: int_to_hex_string(255, 4) -> "00ff"
    //              int_to_hex_string(255, 1) -> "ff"

    std::stringstream ss_hex_string;

    // convert to hex
    ss_hex_string << std::setw(number_of_digits) << std::setfill('0') << std::hex << integer;

    return ss_hex_string.str();
}

bool is_stp_enabled(const std::string &bridge_name)
{
    int g_ioctl_sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (g_ioctl_sockfd < 0) {
        MAPF_ERR("Socket not initialized");
        return false;
    }

    struct ifreq ifr        = {0};
    struct __bridge_info bi = {0};
    unsigned long args[4];

    strncpy(ifr.ifr_name, bridge_name.c_str(), sizeof(ifr.ifr_name) - 1);

    args[0] = BRCTL_GET_BRIDGE_INFO;
    args[1] = (unsigned long)&bi;
    args[2] = 0;
    args[3] = 0;

    ifr.ifr_data = (char *)args;

    if (ioctl(g_ioctl_sockfd, SIOCDEVPRIVATE, &ifr) < 0) {
        MAPF_ERR("ioctl error");
        close(g_ioctl_sockfd);
        return false;
    }

    close(g_ioctl_sockfd);
    return bi.stp_enabled;
}

} // namespace utils
} // namespace bpl
} // namespace beerocks
