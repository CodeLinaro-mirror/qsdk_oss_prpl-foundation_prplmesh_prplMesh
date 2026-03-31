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

#ifndef _TLVF_IEEE_1905_1_TLVDEVICEIDENTIFICATION_H_
#define _TLVF_IEEE_1905_1_TLVDEVICEIDENTIFICATION_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include "tlvf/ieee_1905_1/eTlvType.h"
#include <tuple>

namespace ieee1905_1 {


class tlvDeviceIdentification : public BaseClass
{
    public:
        tlvDeviceIdentification(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvDeviceIdentification(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvDeviceIdentification();

        const eTlvType& type();
        const uint16_t& length();
        uint8_t* friendly_name(size_t idx = 0);
        bool set_friendly_name(const void* buffer, size_t size);
        uint8_t* manufacturer_name(size_t idx = 0);
        bool set_manufacturer_name(const void* buffer, size_t size);
        uint8_t* manufacturer_model(size_t idx = 0);
        bool set_manufacturer_model(const void* buffer, size_t size);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvType* m_type = nullptr;
        uint16_t* m_length = nullptr;
        uint8_t* m_friendly_name = nullptr;
        size_t m_friendly_name_idx__ = 0;
        int m_lock_order_counter__ = 0;
        uint8_t* m_manufacturer_name = nullptr;
        size_t m_manufacturer_name_idx__ = 0;
        uint8_t* m_manufacturer_model = nullptr;
        size_t m_manufacturer_model_idx__ = 0;
};

}; // close namespace: ieee1905_1

#endif //_TLVF/IEEE_1905_1_TLVDEVICEIDENTIFICATION_H_
