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

#ifndef _TLVF_IEEE_1905_1_TLVIPV6_H_
#define _TLVF_IEEE_1905_1_TLVIPV6_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include "tlvf/ieee_1905_1/eTlvType.h"
#include <tuple>
#include <vector>
#include "tlvf/common/sMacAddr.h"
#include "tlvf/ieee_1905_1/eIpv6AddressType.h"

namespace ieee1905_1 {

class cIpv6InterfaceBlock;

class tlvIpv6 : public BaseClass
{
    public:
        tlvIpv6(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvIpv6(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvIpv6();

        const eTlvType& type();
        const uint16_t& length();
        uint8_t& number_of_entries();
        std::tuple<bool, cIpv6InterfaceBlock&> ipv6_interfaces_list(size_t idx);
        std::shared_ptr<cIpv6InterfaceBlock> create_ipv6_interfaces_list();
        bool add_ipv6_interfaces_list(std::shared_ptr<cIpv6InterfaceBlock> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvType* m_type = nullptr;
        uint16_t* m_length = nullptr;
        uint8_t* m_number_of_entries = nullptr;
        cIpv6InterfaceBlock* m_ipv6_interfaces_list = nullptr;
        size_t m_ipv6_interfaces_list_idx__ = 0;
        std::vector<std::shared_ptr<cIpv6InterfaceBlock>> m_ipv6_interfaces_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cIpv6InterfaceBlock : public BaseClass
{
    public:
        cIpv6InterfaceBlock(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cIpv6InterfaceBlock(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cIpv6InterfaceBlock();

        typedef struct sIpv6AddressEntry {
            eIpv6AddressType ipv6_address_type;
            uint8_t ipv6_address[16];
            uint8_t ipv6_address_origin[16];
            void struct_swap(){
                tlvf_swap(8*sizeof(eIpv6AddressType), reinterpret_cast<uint8_t*>(&ipv6_address_type));
            }
            void struct_init(){
            }
        } __attribute__((packed)) sIpv6AddressEntry;
        
        sMacAddr& mac_address();
        uint8_t* ipv6_link_local_address(size_t idx = 0);
        bool set_ipv6_link_local_address(const void* buffer, size_t size);
        uint8_t& number_of_ipv6_addresses();
        std::tuple<bool, sIpv6AddressEntry&> ipv6_address_entries(size_t idx);
        bool alloc_ipv6_address_entries(size_t count = 1);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        sMacAddr* m_mac_address = nullptr;
        uint8_t* m_ipv6_link_local_address = nullptr;
        size_t m_ipv6_link_local_address_idx__ = 0;
        int m_lock_order_counter__ = 0;
        uint8_t* m_number_of_ipv6_addresses = nullptr;
        sIpv6AddressEntry* m_ipv6_address_entries = nullptr;
        size_t m_ipv6_address_entries_idx__ = 0;
};

}; // close namespace: ieee1905_1

#endif //_TLVF/IEEE_1905_1_TLVIPV6_H_
