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

#ifndef _TLVF_WSC_EWSCVENDOREXTHIDDENSSID_H_
#define _TLVF_WSC_EWSCVENDOREXTHIDDENSSID_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace WSC {

enum class eWscVendorExtHiddenSsid : uint8_t {
    DISABLED = 0x0,
    ENABLED = 0x1,
    UNSET = 0xff,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eWscVendorExtHiddenSsid_str(eWscVendorExtHiddenSsid enum_value) {
    switch (enum_value) {
    case eWscVendorExtHiddenSsid::DISABLED: return "eWscVendorExtHiddenSsid::DISABLED";
    case eWscVendorExtHiddenSsid::ENABLED:  return "eWscVendorExtHiddenSsid::ENABLED";
    case eWscVendorExtHiddenSsid::UNSET:    return "eWscVendorExtHiddenSsid::UNSET";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eWscVendorExtHiddenSsid value) { return out << eWscVendorExtHiddenSsid_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eWscVendorExtHiddenSsidValidate {
public:
    static bool check(uint8_t value) {
        bool ret = false;
        switch (value) {
        case 0x0:
        case 0x1:
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

#endif //_TLVF/WSC_EWSCVENDOREXTHIDDENSSID_H_
