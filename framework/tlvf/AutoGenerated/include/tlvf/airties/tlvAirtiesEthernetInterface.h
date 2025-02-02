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

#ifndef _TLVF_AIRTIES_TLVAIRTIESETHERNETINTERFACE_H_
#define _TLVF_AIRTIES_TLVAIRTIESETHERNETINTERFACE_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <tuple>
#include <vector>
#include <asm/byteorder.h>
#include "tlvf/ieee_1905_1/sVendorOUI.h"
#include "tlvf/airties/eAirtiesTlvTypeMap.h"
#include "tlvf/common/sMacAddr.h"

namespace airties {

class cInterfaceList;

class tlvAirtiesEthernetInterface : public BaseClass
{
    public:
        tlvAirtiesEthernetInterface(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesEthernetInterface(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesEthernetInterface();

        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        uint16_t& tlv_id();
        uint8_t& number_of_eth_phy();
        std::tuple<bool, cInterfaceList&> interface_list(size_t idx);
        std::shared_ptr<cInterfaceList> create_interface_list();
        bool add_interface_list(std::shared_ptr<cInterfaceList> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
        uint16_t* m_tlv_id = nullptr;
        uint8_t* m_number_of_eth_phy = nullptr;
        cInterfaceList* m_interface_list = nullptr;
        size_t m_interface_list_idx__ = 0;
        std::vector<std::shared_ptr<cInterfaceList>> m_interface_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cInterfaceList : public BaseClass
{
    public:
        cInterfaceList(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cInterfaceList(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cInterfaceList();

        typedef struct sFlags1 {
            #if defined(__LITTLE_ENDIAN_BITFIELD)
            uint8_t reserved : 5;
            uint8_t eth_port_duplex_mode : 1;
            uint8_t eth_port_link_state : 1;
            uint8_t eth_port_admin_state : 1;
            #elif defined(__BIG_ENDIAN_BITFIELD)
            uint8_t eth_port_admin_state : 1;
            uint8_t eth_port_link_state : 1;
            uint8_t eth_port_duplex_mode : 1;
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
            uint8_t current_link_type : 4;
            uint8_t supported_link_type : 4;
            #elif defined(__BIG_ENDIAN_BITFIELD)
            uint8_t supported_link_type : 4;
            uint8_t current_link_type : 4;
            #else
            #error "Bitfield macros are not defined"
            #endif
            void struct_swap(){
            }
            void struct_init(){
            }
        } __attribute__((packed)) sFlags2;
        
        uint8_t& port_id();
        sMacAddr& eth_mac();
        uint8_t& eth_intf_name_len();
        std::string eth_intf_name_str();
        char* eth_intf_name(size_t length = 0);
        bool set_eth_intf_name(const std::string& str);
        bool set_eth_intf_name(const char buffer[], size_t size);
        bool alloc_eth_intf_name(size_t count = 1);
        sFlags1& flags1();
        sFlags2& flags2();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint8_t* m_port_id = nullptr;
        sMacAddr* m_eth_mac = nullptr;
        uint8_t* m_eth_intf_name_len = nullptr;
        char* m_eth_intf_name = nullptr;
        size_t m_eth_intf_name_idx__ = 0;
        int m_lock_order_counter__ = 0;
        sFlags1* m_flags1 = nullptr;
        sFlags2* m_flags2 = nullptr;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVAIRTIESETHERNETINTERFACE_H_
