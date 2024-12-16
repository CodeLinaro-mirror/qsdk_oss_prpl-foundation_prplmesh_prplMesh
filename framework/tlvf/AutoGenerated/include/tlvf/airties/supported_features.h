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

#ifndef _TLVF_AIRTIES_SUPPORTED_FEATURES_H_
#define _TLVF_AIRTIES_SUPPORTED_FEATURES_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <ostream>

namespace airties {

enum class eAirtiesFeatureIDs : uint16_t {
    AIRTIES_FEATURE_DEVICE_METRICS = 0x1,
    AIRTIES_FEATURE_IEEE1905_1_14 = 0x2,
    AIRTIES_FEATURE_DEVICE_INFO = 0x3,
    AIRTIES_FEATURE_ETH_STATS = 0x4,
    AIRTIES_FEATURE_REBOOT_RESET = 0x6,
    AIRTIES_FEATURE_WIFI6_CAP = 0x7,
    AIRTIES_FEATURE_DPP_ONBOARD = 0x8,
    AIRTIES_FEATURE_STP = 0x9,
    AIRTIES_FEATURE_LED = 0xb,
    AIRTIES_FEATURE_SERVICE_STATUS_WIFI_ON_OFF = 0xc,
    AIRTIES_FEATURE_HIDDEN_SSID = 0xe,
    AIRTIES_FEATURE_RADIO_CAPABILITY = 0x10,
    AIRTIES_FEATURE_END = 0x11,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eAirtiesFeatureIDs_str(eAirtiesFeatureIDs enum_value) {
    switch (enum_value) {
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_DEVICE_METRICS:             return "eAirtiesFeatureIDs::AIRTIES_FEATURE_DEVICE_METRICS";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_IEEE1905_1_14:              return "eAirtiesFeatureIDs::AIRTIES_FEATURE_IEEE1905_1_14";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_DEVICE_INFO:                return "eAirtiesFeatureIDs::AIRTIES_FEATURE_DEVICE_INFO";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_ETH_STATS:                  return "eAirtiesFeatureIDs::AIRTIES_FEATURE_ETH_STATS";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_REBOOT_RESET:               return "eAirtiesFeatureIDs::AIRTIES_FEATURE_REBOOT_RESET";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_WIFI6_CAP:                  return "eAirtiesFeatureIDs::AIRTIES_FEATURE_WIFI6_CAP";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_DPP_ONBOARD:                return "eAirtiesFeatureIDs::AIRTIES_FEATURE_DPP_ONBOARD";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_STP:                        return "eAirtiesFeatureIDs::AIRTIES_FEATURE_STP";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_LED:                        return "eAirtiesFeatureIDs::AIRTIES_FEATURE_LED";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_SERVICE_STATUS_WIFI_ON_OFF: return "eAirtiesFeatureIDs::AIRTIES_FEATURE_SERVICE_STATUS_WIFI_ON_OFF";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_HIDDEN_SSID:                return "eAirtiesFeatureIDs::AIRTIES_FEATURE_HIDDEN_SSID";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_RADIO_CAPABILITY:           return "eAirtiesFeatureIDs::AIRTIES_FEATURE_RADIO_CAPABILITY";
    case eAirtiesFeatureIDs::AIRTIES_FEATURE_END:                        return "eAirtiesFeatureIDs::AIRTIES_FEATURE_END";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eAirtiesFeatureIDs value) { return out << eAirtiesFeatureIDs_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end
class eAirtiesFeatureIDsValidate {
public:
    static bool check(uint16_t value) {
        bool ret = false;
        switch (value) {
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x6:
        case 0x7:
        case 0x8:
        case 0x9:
        case 0xb:
        case 0xc:
        case 0xe:
        case 0x10:
        case 0x11:
                ret = true;
                break;
            default:
                ret = false;
                break;
        }
        return ret;
    }
};

enum eAirtiesFeatureVersion: uint16_t {
    feature_version = 0x1,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eAirtiesFeatureVersion_str(eAirtiesFeatureVersion enum_value) {
    switch (enum_value) {
    case feature_version: return "feature_version";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eAirtiesFeatureVersion value) { return out << eAirtiesFeatureVersion_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end

enum eMasterVersion: uint16_t {
    master_version = 0x4,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eMasterVersion_str(eMasterVersion enum_value) {
    switch (enum_value) {
    case master_version: return "master_version";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eMasterVersion value) { return out << eMasterVersion_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end

enum eSubVersion: uint16_t {
    sub_version = 0x0,
};
// Enum AutoPrint generated code snippet begining- DON'T EDIT!
// clang-format off
static const char *eSubVersion_str(eSubVersion enum_value) {
    switch (enum_value) {
    case sub_version: return "sub_version";
    }
    static std::string out_str = std::to_string(int(enum_value));
    return out_str.c_str();
}
inline std::ostream &operator<<(std::ostream &out, eSubVersion value) { return out << eSubVersion_str(value); }
// clang-format on
// Enum AutoPrint generated code snippet end


}; // close namespace: airties

#endif //_TLVF/AIRTIES_SUPPORTED_FEATURES_H_
