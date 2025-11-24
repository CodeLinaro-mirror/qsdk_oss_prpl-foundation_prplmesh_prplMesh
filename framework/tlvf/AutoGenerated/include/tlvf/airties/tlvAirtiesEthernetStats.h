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

#ifndef _TLVF_AIRTIES_TLVAIRTIESETHERNETSTATS_H_
#define _TLVF_AIRTIES_TLVAIRTIESETHERNETSTATS_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <tuple>
#include <vector>
#include "tlvf/ieee_1905_1/sVendorOUI.h"
#include "tlvf/airties/eAirtiesTlvTypeMap.h"

namespace airties {

class cPortList;
class cStatsItem;

class tlvAirtiesEthernetStats : public BaseClass
{
    public:
        tlvAirtiesEthernetStats(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesEthernetStats(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesEthernetStats();

        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        uint16_t& tlv_id();
        uint16_t& supported_extra_stats();
        uint8_t& num_of_ports();
        std::tuple<bool, cPortList&> port_list(size_t idx);
        std::shared_ptr<cPortList> create_port_list();
        bool add_port_list(std::shared_ptr<cPortList> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
        uint16_t* m_tlv_id = nullptr;
        uint16_t* m_supported_extra_stats = nullptr;
        uint8_t* m_num_of_ports = nullptr;
        cPortList* m_port_list = nullptr;
        size_t m_port_list_idx__ = 0;
        std::vector<std::shared_ptr<cPortList>> m_port_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cPortList : public BaseClass
{
    public:
        cPortList(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cPortList(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cPortList();

        uint8_t& port_id();
        size_t statsItem_length();
        std::tuple<bool, cStatsItem&> statsItem(size_t idx);
        std::shared_ptr<cStatsItem> create_statsItem();
        bool add_statsItem(std::shared_ptr<cStatsItem> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint8_t* m_port_id = nullptr;
        cStatsItem* m_statsItem = nullptr;
        size_t m_statsItem_idx__ = 0;
        std::vector<std::shared_ptr<cStatsItem>> m_statsItem_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cStatsItem : public BaseClass
{
    public:
        cStatsItem(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cStatsItem(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cStatsItem();

        uint8_t* item(size_t idx = 0);
        bool set_item(const void* buffer, size_t size);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint8_t* m_item = nullptr;
        size_t m_item_idx__ = 0;
        int m_lock_order_counter__ = 0;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVAIRTIESETHERNETSTATS_H_
