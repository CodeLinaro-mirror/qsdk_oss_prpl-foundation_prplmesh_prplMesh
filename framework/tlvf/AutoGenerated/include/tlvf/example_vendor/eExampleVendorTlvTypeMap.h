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

#ifndef _TLVF_EXAMPLE_VENDOR_EEXAMPLEVENDORTLVTYPEMAP_H_
#define _TLVF_EXAMPLE_VENDOR_EEXAMPLEVENDORTLVTYPEMAP_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace example_vendor {

enum class eExampleVendorTlvTypeMap : uint8_t {
    TLV_VENDOR_SPECIFIC = 0xb,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eExampleVendorTlvTypeMap_str(eExampleVendorTlvTypeMap enum_value) {
    switch (enum_value) {
    case eExampleVendorTlvTypeMap::TLV_VENDOR_SPECIFIC: return "eExampleVendorTlvTypeMap::TLV_VENDOR_SPECIFIC";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eExampleVendorTlvTypeMap value) { return out << eExampleVendorTlvTypeMap_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eExampleVendorTlvTypeMapValidate {
public:
    static bool check(uint8_t value) {
        bool ret = false;
        switch (value) {
        case 0xb:
                ret = true;
                break;
            default:
                ret = false;
                break;
        }
        return ret;
    }
};


}; // close namespace: example_vendor

#endif //_TLVF/EXAMPLE_VENDOR_EEXAMPLEVENDORTLVTYPEMAP_H_
