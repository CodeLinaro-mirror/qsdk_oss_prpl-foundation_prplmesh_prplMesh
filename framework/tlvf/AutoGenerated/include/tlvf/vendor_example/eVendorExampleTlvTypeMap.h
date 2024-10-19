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

#ifndef _TLVF_VENDOR_EXAMPLE_EVENDOREXAMPLETLVTYPEMAP_H_
#define _TLVF_VENDOR_EXAMPLE_EVENDOREXAMPLETLVTYPEMAP_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace vendor_example {

enum class eVendorExampleTlvTypeMap : uint8_t {
    TLV_VENDOR_SPECIFIC = 0xb,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eVendorExampleTlvTypeMap_str(eVendorExampleTlvTypeMap enum_value) {
    switch (enum_value) {
    case eVendorExampleTlvTypeMap::TLV_VENDOR_SPECIFIC: return "eVendorExampleTlvTypeMap::TLV_VENDOR_SPECIFIC";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eVendorExampleTlvTypeMap value) { return out << eVendorExampleTlvTypeMap_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eVendorExampleTlvTypeMapValidate {
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


}; // close namespace: vendor_example

#endif //_TLVF/VENDOR_EXAMPLE_EVENDOREXAMPLETLVTYPEMAP_H_
