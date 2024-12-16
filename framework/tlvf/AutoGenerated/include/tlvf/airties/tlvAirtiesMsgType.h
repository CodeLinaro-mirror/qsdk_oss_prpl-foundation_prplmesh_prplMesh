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

#ifndef _TLVF_AIRTIES_TLVAIRTIESMSGTYPE_H_
#define _TLVF_AIRTIES_TLVAIRTIESMSGTYPE_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <ostream>
#include "tlvf/ieee_1905_1/sVendorOUI.h"
#include "tlvf/airties/eAirtiesTlvTypeMap.h"

namespace airties {


class tlvAirtiesMsgType : public BaseClass
{
    public:
        tlvAirtiesMsgType(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesMsgType(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesMsgType();

        enum airtiesVendorOUI: uint32_t {
            OUI_BYTES = 0x3,
            OUI_AIRTIES = 0xfc4188,
        };
        // Enum AutoPrint generated code snippet begining- DON'T EDIT!
        // clang-format off
        static const char *airtiesVendorOUI_str(airtiesVendorOUI enum_value) {
            switch (enum_value) {
            case OUI_BYTES:   return "OUI_BYTES";
            case OUI_AIRTIES: return "OUI_AIRTIES";
            }
            static std::string out_str = std::to_string(int(enum_value));
            return out_str.c_str();
        }
        friend inline std::ostream &operator<<(std::ostream &out, airtiesVendorOUI value) { return out << airtiesVendorOUI_str(value); }
        // clang-format on
        // Enum AutoPrint generated code snippet end
        
        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVAIRTIESMSGTYPE_H_
