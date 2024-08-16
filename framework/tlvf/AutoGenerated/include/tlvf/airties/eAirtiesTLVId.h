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

#ifndef _TLVF_AIRTIES_EAIRTIESTLVID_H_
#define _TLVF_AIRTIES_EAIRTIESTLVID_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace airties {

enum class eAirtiesTlVId : uint16_t {
    AIRTIES_MSG_TYPE = 0x1,
    AIRTIES_FEATURE_PROFILE = 0x2,
    AIRTIES_DEVICE_INFO = 0x3,
    AIRTIES_DEVICE_METRICS = 0x4,
    AIRTIES_REBOOT_REQUEST = 0x5,
    AIRTIES_ETHERNET_STATS = 0x10,
    AIRTIES_LED_STATUS = 0xd,
    AIRTIES_SERVICE_STATUS = 0xe,
    AIRTIES_ETHERNET_INTERFACE = 0xf,
    AIRTIES_1905_NEIGHBOR_DEVICE_LIST = 0x12,
    AIRTIES_NON_1905_NEIGHBOR_DEVICE_LIST = 0x11,
    AIRTIES_RADIO_CAPABILITY = 0x13,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eAirtiesTlVId_str(eAirtiesTlVId enum_value) {
    switch (enum_value) {
    case eAirtiesTlVId::AIRTIES_MSG_TYPE:                      return "eAirtiesTlVId::AIRTIES_MSG_TYPE";
    case eAirtiesTlVId::AIRTIES_FEATURE_PROFILE:               return "eAirtiesTlVId::AIRTIES_FEATURE_PROFILE";
    case eAirtiesTlVId::AIRTIES_DEVICE_INFO:                   return "eAirtiesTlVId::AIRTIES_DEVICE_INFO";
    case eAirtiesTlVId::AIRTIES_DEVICE_METRICS:                return "eAirtiesTlVId::AIRTIES_DEVICE_METRICS";
    case eAirtiesTlVId::AIRTIES_REBOOT_REQUEST:                return "eAirtiesTlVId::AIRTIES_REBOOT_REQUEST";
    case eAirtiesTlVId::AIRTIES_ETHERNET_STATS:                return "eAirtiesTlVId::AIRTIES_ETHERNET_STATS";
    case eAirtiesTlVId::AIRTIES_LED_STATUS:                    return "eAirtiesTlVId::AIRTIES_LED_STATUS";
    case eAirtiesTlVId::AIRTIES_SERVICE_STATUS:                return "eAirtiesTlVId::AIRTIES_SERVICE_STATUS";
    case eAirtiesTlVId::AIRTIES_ETHERNET_INTERFACE:            return "eAirtiesTlVId::AIRTIES_ETHERNET_INTERFACE";
    case eAirtiesTlVId::AIRTIES_1905_NEIGHBOR_DEVICE_LIST:     return "eAirtiesTlVId::AIRTIES_1905_NEIGHBOR_DEVICE_LIST";
    case eAirtiesTlVId::AIRTIES_NON_1905_NEIGHBOR_DEVICE_LIST: return "eAirtiesTlVId::AIRTIES_NON_1905_NEIGHBOR_DEVICE_LIST";
    case eAirtiesTlVId::AIRTIES_RADIO_CAPABILITY:              return "eAirtiesTlVId::AIRTIES_RADIO_CAPABILITY";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eAirtiesTlVId value) { return out << eAirtiesTlVId_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eAirtiesTlVIdValidate {
public:
    static bool check(uint16_t value) {
        bool ret = false;
        switch (value) {
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x10:
        case 0xd:
        case 0xe:
        case 0xf:
        case 0x12:
        case 0x11:
        case 0x13:
                ret = true;
                break;
            default:
                ret = false;
                break;
        }
        return ret;
    }
};


}; // close namespace: airties

#endif //_TLVF/AIRTIES_EAIRTIESTLVID_H_
