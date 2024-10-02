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

#ifndef _TLVF_EXAMPLE_VENDOR_TLVEXAMPLEVENDOR_H_
#define _TLVF_EXAMPLE_VENDOR_TLVEXAMPLEVENDOR_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <ostream>
#include "tlvf/ieee_1905_1/sVendorOUI.h"
#include "tlvf/example_vendor/eExampleVendorTlvTypeMap.h"

namespace example_vendor {


class tlvExampleVendor : public BaseClass
{
    public:
        tlvExampleVendor(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvExampleVendor(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvExampleVendor();

        enum exampleVendorOUI: uint32_t {
            OUI_BYTES = 0x3,
            EXAMPLE_OUI = 0x563412,
        };
        // Enum AutoPrint generated code snippet begining- DON'T EDIT!
        // clang-format off
        static const char *exampleVendorOUI_str(exampleVendorOUI enum_value) {
            switch (enum_value) {
            case OUI_BYTES:   return "OUI_BYTES";
            case EXAMPLE_OUI: return "EXAMPLE_OUI";
            }
            static std::string out_str = std::to_string(int(enum_value));
            return out_str.c_str();
        }
        friend inline std::ostream &operator<<(std::ostream &out, exampleVendorOUI value) { return out << exampleVendorOUI_str(value); }
        // clang-format on
        // Enum AutoPrint generated code snippet end
        
        const eExampleVendorTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eExampleVendorTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
};

}; // close namespace: example_vendor

#endif //_TLVF/EXAMPLE_VENDOR_TLVEXAMPLEVENDOR_H_
