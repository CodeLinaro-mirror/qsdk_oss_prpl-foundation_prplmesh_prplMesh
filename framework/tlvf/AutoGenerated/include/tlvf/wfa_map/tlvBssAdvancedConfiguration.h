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

#ifndef _TLVF_WFA_MAP_TLVBSSADVANCEDCONFIGURATION_H_
#define _TLVF_WFA_MAP_TLVBSSADVANCEDCONFIGURATION_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include "tlvf/wfa_map/eTlvTypeMap.h"
#include <tuple>
#include <vector>
#include "tlvf/common/sMacAddr.h"
#include <asm/byteorder.h>

namespace wfa_map {

class cBssAdvancedConfigurationRadio;
class cBssAdvancedConfigurationBss;

class tlvBssAdvancedConfiguration : public BaseClass
{
    public:
        tlvBssAdvancedConfiguration(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvBssAdvancedConfiguration(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvBssAdvancedConfiguration();

        const eTlvTypeMap& type();
        const uint16_t& length();
        uint8_t& num_radio();
        std::tuple<bool, cBssAdvancedConfigurationRadio&> radios(size_t idx);
        std::shared_ptr<cBssAdvancedConfigurationRadio> create_radios();
        bool add_radios(std::shared_ptr<cBssAdvancedConfigurationRadio> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        uint8_t* m_num_radio = nullptr;
        cBssAdvancedConfigurationRadio* m_radios = nullptr;
        size_t m_radios_idx__ = 0;
        std::vector<std::shared_ptr<cBssAdvancedConfigurationRadio>> m_radios_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cBssAdvancedConfigurationRadio : public BaseClass
{
    public:
        cBssAdvancedConfigurationRadio(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cBssAdvancedConfigurationRadio(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cBssAdvancedConfigurationRadio();

        sMacAddr& ruid();
        uint8_t& num_bss();
        std::tuple<bool, cBssAdvancedConfigurationBss&> bsss(size_t idx);
        std::shared_ptr<cBssAdvancedConfigurationBss> create_bsss();
        bool add_bsss(std::shared_ptr<cBssAdvancedConfigurationBss> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        sMacAddr* m_ruid = nullptr;
        uint8_t* m_num_bss = nullptr;
        cBssAdvancedConfigurationBss* m_bsss = nullptr;
        size_t m_bsss_idx__ = 0;
        std::vector<std::shared_ptr<cBssAdvancedConfigurationBss>> m_bsss_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cBssAdvancedConfigurationBss : public BaseClass
{
    public:
        cBssAdvancedConfigurationBss(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cBssAdvancedConfigurationBss(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cBssAdvancedConfigurationBss();

        typedef struct sBssInfos {
            #if defined(__LITTLE_ENDIAN_BITFIELD)
            uint8_t reserved : 7;
            uint8_t ssid_advertisement : 1;
            #elif defined(__BIG_ENDIAN_BITFIELD)
            uint8_t ssid_advertisement : 1;
            uint8_t reserved : 7;
            #else
            #error "Bitfield macros are not defined"
            #endif
            void struct_swap(){
            }
            void struct_init(){
            }
        } __attribute__((packed)) sBssInfos;
        
        sMacAddr& bssid();
        uint8_t& bss_index();
        sBssInfos& bss_infos();
        uint8_t* reserved(size_t idx = 0);
        bool set_reserved(const void* buffer, size_t size);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        sMacAddr* m_bssid = nullptr;
        uint8_t* m_bss_index = nullptr;
        sBssInfos* m_bss_infos = nullptr;
        uint8_t* m_reserved = nullptr;
        size_t m_reserved_idx__ = 0;
        int m_lock_order_counter__ = 0;
};

}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_TLVBSSADVANCEDCONFIGURATION_H_
