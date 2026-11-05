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

#ifndef _TLVF_WFA_MAP_TLVPUSHBUTTONCONFIG_H_
#define _TLVF_WFA_MAP_TLVPUSHBUTTONCONFIG_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include "tlvf/wfa_map/eTlvTypeMap.h"
#include "tlvf/common/sMacAddr.h"
#include <tuple>

namespace wfa_map {


class tlvPushButtonConfig : public BaseClass
{
    public:
        tlvPushButtonConfig(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvPushButtonConfig(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvPushButtonConfig();

        const eTlvTypeMap& type();
        const uint16_t& length();
        sMacAddr& bssid();
        //Ignored if bssid is non-zero
        uint8_t& bss_index();
        //WPS Walk Time in seconds
        uint16_t& listen_duration();
        uint8_t* uuid_e(size_t idx = 0);
        bool set_uuid_e(const void* buffer, size_t size);
        //WPS Device Password ID, 0x0004 for Pushbutton
        uint16_t& device_password_id();
        //Present only if device_password_id indicates a PIN (length inferred from tlvLength field)
        size_t device_password_length() { return m_device_password_idx__ * sizeof(uint8_t); }
        uint8_t* device_password(size_t idx = 0);
        bool set_device_password(const void* buffer, size_t size);
        bool alloc_device_password(size_t count = 1);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sMacAddr* m_bssid = nullptr;
        uint8_t* m_bss_index = nullptr;
        uint16_t* m_listen_duration = nullptr;
        uint8_t* m_uuid_e = nullptr;
        size_t m_uuid_e_idx__ = 0;
        int m_lock_order_counter__ = 0;
        uint16_t* m_device_password_id = nullptr;
        uint8_t* m_device_password = nullptr;
        size_t m_device_password_idx__ = 0;
};

}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_TLVPUSHBUTTONCONFIG_H_
