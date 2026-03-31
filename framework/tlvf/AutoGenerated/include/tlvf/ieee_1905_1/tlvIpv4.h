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

#ifndef _TLVF_IEEE_1905_1_TLVIPV4_H_
#define _TLVF_IEEE_1905_1_TLVIPV4_H_

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
#include "tlvf/ieee_1905_1/eIpv4AddressType.h"

namespace ieee1905_1 {

class cIpv4InterfaceBlock;

class tlvIpv4 : public BaseClass
{
    public:
        tlvIpv4(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvIpv4(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvIpv4();

        const eTlvType& type();
        const uint16_t& length();
        uint8_t& number_of_entries();
        std::tuple<bool, cIpv4InterfaceBlock&> ipv4_interfaces_list(size_t idx);
        std::shared_ptr<cIpv4InterfaceBlock> create_ipv4_interfaces_list();
        bool add_ipv4_interfaces_list(std::shared_ptr<cIpv4InterfaceBlock> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvType* m_type = nullptr;
        uint16_t* m_length = nullptr;
        uint8_t* m_number_of_entries = nullptr;
        cIpv4InterfaceBlock* m_ipv4_interfaces_list = nullptr;
        size_t m_ipv4_interfaces_list_idx__ = 0;
        std::vector<std::shared_ptr<cIpv4InterfaceBlock>> m_ipv4_interfaces_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cIpv4InterfaceBlock : public BaseClass
{
    public:
        cIpv4InterfaceBlock(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cIpv4InterfaceBlock(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cIpv4InterfaceBlock();

        typedef struct sIpv4AddressEntry {
            eIpv4AddressType ipv4_address_type;
            uint32_t ipv4_address;
            uint32_t ipv4_dhcp_server;
            void struct_swap(){
                tlvf_swap(8*sizeof(eIpv4AddressType), reinterpret_cast<uint8_t*>(&ipv4_address_type));
                tlvf_swap(32, reinterpret_cast<uint8_t*>(&ipv4_address));
                tlvf_swap(32, reinterpret_cast<uint8_t*>(&ipv4_dhcp_server));
            }
            void struct_init(){
            }
        } __attribute__((packed)) sIpv4AddressEntry;
        
        sMacAddr& mac_address();
        uint8_t& number_of_ipv4_addresses();
        std::tuple<bool, sIpv4AddressEntry&> ipv4_address_entries(size_t idx);
        bool alloc_ipv4_address_entries(size_t count = 1);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        sMacAddr* m_mac_address = nullptr;
        uint8_t* m_number_of_ipv4_addresses = nullptr;
        sIpv4AddressEntry* m_ipv4_address_entries = nullptr;
        size_t m_ipv4_address_entries_idx__ = 0;
        int m_lock_order_counter__ = 0;
};

}; // close namespace: ieee1905_1

#endif //_TLVF/IEEE_1905_1_TLVIPV4_H_
