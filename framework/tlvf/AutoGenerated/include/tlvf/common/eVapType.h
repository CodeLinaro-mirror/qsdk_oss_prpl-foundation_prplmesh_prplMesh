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

#ifndef _TLVF_COMMON_EVAPTYPE_H_
#define _TLVF_COMMON_EVAPTYPE_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>
enum class eVapType : uint8_t {
    HOME = 0x0,
    GUEST = 0x1,
    VIDEO = 0x2,
    BACKHAUL = 0x3,
    HOTSPOT = 0x4,
    STAFF = 0x5,
    ISOLATED = 0x6,
    OTHER = 0xff,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eVapType_str(eVapType enum_value) {
    switch (enum_value) {
    case eVapType::HOME:     return "eVapType::HOME";
    case eVapType::GUEST:    return "eVapType::GUEST";
    case eVapType::VIDEO:    return "eVapType::VIDEO";
    case eVapType::BACKHAUL: return "eVapType::BACKHAUL";
    case eVapType::HOTSPOT:  return "eVapType::HOTSPOT";
    case eVapType::STAFF:    return "eVapType::STAFF";
    case eVapType::ISOLATED: return "eVapType::ISOLATED";
    case eVapType::OTHER:    return "eVapType::OTHER";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eVapType value) { return out << eVapType_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eVapTypeValidate {
public:
    static bool check(uint8_t value) {
        bool ret = false;
        switch (value) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0xff:
                ret = true;
                break;
            default:
                ret = false;
                break;
        }
        return ret;
    }
};


#endif //_TLVF/COMMON_EVAPTYPE_H_
