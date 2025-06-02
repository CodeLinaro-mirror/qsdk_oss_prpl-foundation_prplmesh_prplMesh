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

#ifndef _TLVF_WFA_MAP_TLVRSNPARAMETERSCONFIGURATION_H_
#define _TLVF_WFA_MAP_TLVRSNPARAMETERSCONFIGURATION_H_

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

namespace wfa_map {

class cRsnRadio;
class cRsnBss;

class tlvRsnParametersConfiguration : public BaseClass
{
    public:
        tlvRsnParametersConfiguration(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvRsnParametersConfiguration(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvRsnParametersConfiguration();

        const eTlvTypeMap& type();
        const uint16_t& length();
        uint8_t& num_radio();
        std::tuple<bool, cRsnRadio&> radios(size_t idx);
        std::shared_ptr<cRsnRadio> create_radios();
        bool add_radios(std::shared_ptr<cRsnRadio> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        uint8_t* m_num_radio = nullptr;
        cRsnRadio* m_radios = nullptr;
        size_t m_radios_idx__ = 0;
        std::vector<std::shared_ptr<cRsnRadio>> m_radios_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cRsnRadio : public BaseClass
{
    public:
        cRsnRadio(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cRsnRadio(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cRsnRadio();

        sMacAddr& ruid();
        uint8_t& num_bss();
        std::tuple<bool, cRsnBss&> bsss(size_t idx);
        std::shared_ptr<cRsnBss> create_bsss();
        bool add_bsss(std::shared_ptr<cRsnBss> ptr);
        uint8_t* reserved(size_t idx = 0);
        bool set_reserved(const void* buffer, size_t size);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        sMacAddr* m_ruid = nullptr;
        uint8_t* m_num_bss = nullptr;
        cRsnBss* m_bsss = nullptr;
        size_t m_bsss_idx__ = 0;
        std::vector<std::shared_ptr<cRsnBss>> m_bsss_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
        uint8_t* m_reserved = nullptr;
        size_t m_reserved_idx__ = 0;
};

class cRsnBss : public BaseClass
{
    public:
        cRsnBss(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cRsnBss(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cRsnBss();

        sMacAddr& bssid();
        uint8_t& bss_index();
        uint16_t& security_ies_length();
        uint8_t* security_ies(size_t idx = 0);
        bool set_security_ies(const void* buffer, size_t size);
        bool alloc_security_ies(size_t count = 1);
        uint8_t* reserved(size_t idx = 0);
        bool set_reserved(const void* buffer, size_t size);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        sMacAddr* m_bssid = nullptr;
        uint8_t* m_bss_index = nullptr;
        uint16_t* m_security_ies_length = nullptr;
        uint8_t* m_security_ies = nullptr;
        size_t m_security_ies_idx__ = 0;
        int m_lock_order_counter__ = 0;
        uint8_t* m_reserved = nullptr;
        size_t m_reserved_idx__ = 0;
};

}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_TLVRSNPARAMETERSCONFIGURATION_H_
