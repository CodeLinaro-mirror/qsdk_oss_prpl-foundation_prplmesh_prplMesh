///////////////////////////////////////
// AUTO GENERATED FILE - DO NOT EDIT //
///////////////////////////////////////

/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _TLVF_IEEE_1905_1_EIPV6ADDRESSTYPE_H_
#define _TLVF_IEEE_1905_1_EIPV6ADDRESSTYPE_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace ieee1905_1 {

enum class eIpv6AddressType : uint8_t {
    UNKNOWN = 0x0,
    DHCP = 0x1,
    STATIC = 0x2,
    SLAAC = 0x3,
    RESERVED = 0x4,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eIpv6AddressType_str(eIpv6AddressType enum_value) {
    switch (enum_value) {
    case eIpv6AddressType::UNKNOWN:  return "eIpv6AddressType::UNKNOWN";
    case eIpv6AddressType::DHCP:     return "eIpv6AddressType::DHCP";
    case eIpv6AddressType::STATIC:   return "eIpv6AddressType::STATIC";
    case eIpv6AddressType::SLAAC:    return "eIpv6AddressType::SLAAC";
    case eIpv6AddressType::RESERVED: return "eIpv6AddressType::RESERVED";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eIpv6AddressType value) { return out << eIpv6AddressType_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eIpv6AddressTypeValidate {
public:
    static bool check(uint8_t value) {
        bool ret = false;
        switch (value) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
                ret = true;
                break;
            default:
                ret = false;
                break;
        }
        return ret;
    }
};


}; // close namespace: ieee1905_1

#endif //_TLVF/IEEE_1905_1_EIPV6ADDRESSTYPE_H_
