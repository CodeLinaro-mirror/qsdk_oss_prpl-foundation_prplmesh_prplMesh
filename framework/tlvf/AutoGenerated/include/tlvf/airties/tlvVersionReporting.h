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

#ifndef _TLVF_AIRTIES_TLVVERSIONREPORTING_H_
#define _TLVF_AIRTIES_TLVVERSIONREPORTING_H_

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

class cLocalInterfaceInfo;

class tlvVersionReporting : public BaseClass
{
    public:
        tlvVersionReporting(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvVersionReporting(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvVersionReporting();

        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        uint16_t& tlv_id();
        uint32_t& em_agent_version();
        uint16_t& em_agent_feature_list_length();
        std::tuple<bool, cLocalInterfaceInfo&> em_agent_feature_list(size_t idx);
        std::shared_ptr<cLocalInterfaceInfo> create_em_agent_feature_list();
        bool add_em_agent_feature_list(std::shared_ptr<cLocalInterfaceInfo> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
        uint16_t* m_tlv_id = nullptr;
        uint32_t* m_em_agent_version = nullptr;
        uint16_t* m_em_agent_feature_list_length = nullptr;
        cLocalInterfaceInfo* m_em_agent_feature_list = nullptr;
        size_t m_em_agent_feature_list_idx__ = 0;
        std::vector<std::shared_ptr<cLocalInterfaceInfo>> m_em_agent_feature_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cLocalInterfaceInfo : public BaseClass
{
    public:
        cLocalInterfaceInfo(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cLocalInterfaceInfo(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cLocalInterfaceInfo();

        uint32_t& feature_info();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint32_t* m_feature_info = nullptr;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVVERSIONREPORTING_H_
