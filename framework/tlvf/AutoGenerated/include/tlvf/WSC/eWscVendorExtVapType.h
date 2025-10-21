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

#ifndef _TLVF_WSC_EWSCVENDOREXTVAPTYPE_H_
#define _TLVF_WSC_EWSCVENDOREXTVAPTYPE_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace WSC {

enum class eWscVendorExtVapType : uint8_t {
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
static const char *eWscVendorExtVapType_str(eWscVendorExtVapType enum_value) {
    switch (enum_value) {
    case eWscVendorExtVapType::HOME:     return "eWscVendorExtVapType::HOME";
    case eWscVendorExtVapType::GUEST:    return "eWscVendorExtVapType::GUEST";
    case eWscVendorExtVapType::VIDEO:    return "eWscVendorExtVapType::VIDEO";
    case eWscVendorExtVapType::BACKHAUL: return "eWscVendorExtVapType::BACKHAUL";
    case eWscVendorExtVapType::HOTSPOT:  return "eWscVendorExtVapType::HOTSPOT";
    case eWscVendorExtVapType::STAFF:    return "eWscVendorExtVapType::STAFF";
    case eWscVendorExtVapType::ISOLATED: return "eWscVendorExtVapType::ISOLATED";
    case eWscVendorExtVapType::OTHER:    return "eWscVendorExtVapType::OTHER";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eWscVendorExtVapType value) { return out << eWscVendorExtVapType_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eWscVendorExtVapTypeValidate {
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


}; // close namespace: WSC

#endif //_TLVF/WSC_EWSCVENDOREXTVAPTYPE_H_
