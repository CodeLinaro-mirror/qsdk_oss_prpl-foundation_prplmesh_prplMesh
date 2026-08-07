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

#ifndef _TLVF_IEEE_1905_1_E1905PROFILEVERSION_H_
#define _TLVF_IEEE_1905_1_E1905PROFILEVERSION_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace ieee1905_1 {

enum class e1905ProfileVersion : uint8_t {
    IEEE_1905_1 = 0x0,
    IEEE_1905_1_A = 0x1,
    RESERVED = 0x2,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *e1905ProfileVersion_str(e1905ProfileVersion enum_value) {
    switch (enum_value) {
    case e1905ProfileVersion::IEEE_1905_1:   return "e1905ProfileVersion::IEEE_1905_1";
    case e1905ProfileVersion::IEEE_1905_1_A: return "e1905ProfileVersion::IEEE_1905_1_A";
    case e1905ProfileVersion::RESERVED:      return "e1905ProfileVersion::RESERVED";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, e1905ProfileVersion value) { return out << e1905ProfileVersion_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class e1905ProfileVersionValidate {
public:
    static bool check(uint8_t value) {
        bool ret = false;
        switch (value) {
        case 0x0:
        case 0x1:
        case 0x2:
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

#endif //_TLVF/IEEE_1905_1_E1905PROFILEVERSION_H_
