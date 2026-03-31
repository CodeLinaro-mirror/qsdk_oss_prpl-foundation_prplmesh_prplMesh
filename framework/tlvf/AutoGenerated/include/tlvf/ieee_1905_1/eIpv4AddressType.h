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

#ifndef _TLVF_IEEE_1905_1_EIPV4ADDRESSTYPE_H_
#define _TLVF_IEEE_1905_1_EIPV4ADDRESSTYPE_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace ieee1905_1 {

enum class eIpv4AddressType : uint8_t {
    UNKNOWN = 0x0,
    DHCP = 0x1,
    STATIC = 0x2,
    AUTO_IP = 0x3,
    RESERVED = 0x4,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eIpv4AddressType_str(eIpv4AddressType enum_value) {
    switch (enum_value) {
    case eIpv4AddressType::UNKNOWN:  return "eIpv4AddressType::UNKNOWN";
    case eIpv4AddressType::DHCP:     return "eIpv4AddressType::DHCP";
    case eIpv4AddressType::STATIC:   return "eIpv4AddressType::STATIC";
    case eIpv4AddressType::AUTO_IP:  return "eIpv4AddressType::AUTO_IP";
    case eIpv4AddressType::RESERVED: return "eIpv4AddressType::RESERVED";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eIpv4AddressType value) { return out << eIpv4AddressType_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eIpv4AddressTypeValidate {
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

#endif //_TLVF/IEEE_1905_1_EIPV4ADDRESSTYPE_H_
