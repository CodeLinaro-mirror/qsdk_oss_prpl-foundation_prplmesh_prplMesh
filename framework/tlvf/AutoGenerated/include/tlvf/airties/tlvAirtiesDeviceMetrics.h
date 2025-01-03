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

#ifndef _TLVF_AIRTIES_TLVAIRTIESDEVICEMETRICS_H_
#define _TLVF_AIRTIES_TLVAIRTIESDEVICEMETRICS_H_

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
#include "tlvf/common/sMacAddr.h"

namespace airties {

class cRadioInfo;

class tlvAirtiesDeviceMetrics : public BaseClass
{
    public:
        tlvAirtiesDeviceMetrics(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesDeviceMetrics(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesDeviceMetrics();

        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        uint16_t& tlv_id();
        uint32_t& uptime_to_boot();
        uint8_t& cpu_loadtime_platform();
        uint8_t& cpu_temperature();
        uint32_t& platform_totalmemory();
        uint32_t& platform_freememory();
        uint32_t& platform_cachedmemory();
        uint8_t& num_of_radios();
        std::tuple<bool, cRadioInfo&> radio_list(size_t idx);
        std::shared_ptr<cRadioInfo> create_radio_list();
        bool add_radio_list(std::shared_ptr<cRadioInfo> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
        uint16_t* m_tlv_id = nullptr;
        uint32_t* m_uptime_to_boot = nullptr;
        uint8_t* m_cpu_loadtime_platform = nullptr;
        uint8_t* m_cpu_temperature = nullptr;
        uint32_t* m_platform_totalmemory = nullptr;
        uint32_t* m_platform_freememory = nullptr;
        uint32_t* m_platform_cachedmemory = nullptr;
        uint8_t* m_num_of_radios = nullptr;
        cRadioInfo* m_radio_list = nullptr;
        size_t m_radio_list_idx__ = 0;
        std::vector<std::shared_ptr<cRadioInfo>> m_radio_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cRadioInfo : public BaseClass
{
    public:
        cRadioInfo(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cRadioInfo(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cRadioInfo();

        sMacAddr& radio_id();
        uint8_t& radio_temperature();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        sMacAddr* m_radio_id = nullptr;
        uint8_t* m_radio_temperature = nullptr;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVAIRTIESDEVICEMETRICS_H_
