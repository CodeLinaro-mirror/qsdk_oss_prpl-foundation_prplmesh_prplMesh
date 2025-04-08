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

#ifndef _TLVF_AIRTIES_TLVAIRTIESRADIOCAPABILITY_H_
#define _TLVF_AIRTIES_TLVAIRTIESRADIOCAPABILITY_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <asm/byteorder.h>
#include "tlvf/ieee_1905_1/sVendorOUI.h"
#include "tlvf/airties/eAirtiesTlvTypeMap.h"
#include "tlvf/common/sMacAddr.h"

namespace airties {


class tlvAirtiesRadioCapability : public BaseClass
{
    public:
        tlvAirtiesRadioCapability(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesRadioCapability(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesRadioCapability();

        typedef struct sStandards {
            #if defined(__LITTLE_ENDIAN_BITFIELD)
            uint8_t reserved : 1;
            uint8_t s_80211be : 1;
            uint8_t s_80211ax : 1;
            uint8_t s_80211ac : 1;
            uint8_t s_80211n : 1;
            uint8_t s_80211g : 1;
            uint8_t s_80211b : 1;
            uint8_t s_80211a : 1;
            #elif defined(__BIG_ENDIAN_BITFIELD)
            uint8_t s_80211a : 1;
            uint8_t s_80211b : 1;
            uint8_t s_80211g : 1;
            uint8_t s_80211n : 1;
            uint8_t s_80211ac : 1;
            uint8_t s_80211ax : 1;
            uint8_t s_80211be : 1;
            uint8_t reserved : 1;
            #else
            #error "Bitfield macros are not defined"
            #endif
            void struct_swap(){
            }
            void struct_init(){
            }
        } __attribute__((packed)) sStandards;
        
        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        uint16_t& tlv_id();
        sMacAddr& radio_id();
        sStandards& standards();
        uint8_t& standards_reserved();
        uint16_t& reserved();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
        uint16_t* m_tlv_id = nullptr;
        sMacAddr* m_radio_id = nullptr;
        sStandards* m_standards = nullptr;
        uint8_t* m_standards_reserved = nullptr;
        uint16_t* m_reserved = nullptr;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVAIRTIESRADIOCAPABILITY_H_
