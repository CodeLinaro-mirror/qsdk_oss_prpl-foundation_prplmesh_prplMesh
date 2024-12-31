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

#ifndef _TLVF_AIRTIES_TLVAIRTIESDEVICEINFO_H_
#define _TLVF_AIRTIES_TLVAIRTIESDEVICEINFO_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <tuple>
#include <asm/byteorder.h>
#include "tlvf/ieee_1905_1/sVendorOUI.h"
#include "tlvf/airties/eAirtiesTlvTypeMap.h"

namespace airties {


class tlvAirtiesDeviceInfo : public BaseClass
{
    public:
        tlvAirtiesDeviceInfo(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesDeviceInfo(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesDeviceInfo();

        typedef struct sFlags1 {
            #if defined(__LITTLE_ENDIAN_BITFIELD)
            uint8_t reserved : 5;
            uint8_t stb_product_class : 1;
            uint8_t extender_product_class : 1;
            uint8_t gateway_product_class : 1;
            #elif defined(__BIG_ENDIAN_BITFIELD)
            uint8_t gateway_product_class : 1;
            uint8_t extender_product_class : 1;
            uint8_t stb_product_class : 1;
            uint8_t reserved : 5;
            #else
            #error "Bitfield macros are not defined"
            #endif
            void struct_swap(){
            }
            void struct_init(){
            }
        } __attribute__((packed)) sFlags1;
        
        typedef struct sFlags2 {
            #if defined(__LITTLE_ENDIAN_BITFIELD)
            uint8_t reserved : 7;
            uint8_t device_role_indication : 1;
            #elif defined(__BIG_ENDIAN_BITFIELD)
            uint8_t device_role_indication : 1;
            uint8_t reserved : 7;
            #else
            #error "Bitfield macros are not defined"
            #endif
            void struct_swap(){
            }
            void struct_init(){
            }
        } __attribute__((packed)) sFlags2;
        
        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        uint16_t& tlv_id();
        uint32_t& boot_id();
        uint8_t& client_id_length();
        std::string client_id_str();
        char* client_id(size_t length = 0);
        bool set_client_id(const std::string& str);
        bool set_client_id(const char buffer[], size_t size);
        bool alloc_client_id(size_t count = 1);
        uint8_t& client_secret_length();
        std::string client_secret_str();
        char* client_secret(size_t length = 0);
        bool set_client_secret(const std::string& str);
        bool set_client_secret(const char buffer[], size_t size);
        bool alloc_client_secret(size_t count = 1);
        sFlags1& flags1();
        sFlags2& flags2();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
        uint16_t* m_tlv_id = nullptr;
        uint32_t* m_boot_id = nullptr;
        uint8_t* m_client_id_length = nullptr;
        char* m_client_id = nullptr;
        size_t m_client_id_idx__ = 0;
        int m_lock_order_counter__ = 0;
        uint8_t* m_client_secret_length = nullptr;
        char* m_client_secret = nullptr;
        size_t m_client_secret_idx__ = 0;
        sFlags1* m_flags1 = nullptr;
        sFlags2* m_flags2 = nullptr;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVAIRTIESDEVICEINFO_H_
