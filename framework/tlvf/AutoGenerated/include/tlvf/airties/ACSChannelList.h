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

#ifndef _TLVF_AIRTIES_ACSCHANNELLIST_H_
#define _TLVF_AIRTIES_ACSCHANNELLIST_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <tuple>
#include <vector>

namespace airties {

class cExcludeList;

class cACSChannelList : public BaseClass
{
    public:
        cACSChannelList(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cACSChannelList(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cACSChannelList();

        uint8_t& acs_list_length();
        std::tuple<bool, cExcludeList&> acs_list(size_t idx);
        std::shared_ptr<cExcludeList> create_acs_list();
        bool add_acs_list(std::shared_ptr<cExcludeList> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint8_t* m_acs_list_length = nullptr;
        cExcludeList* m_acs_list = nullptr;
        size_t m_acs_list_idx__ = 0;
        std::vector<std::shared_ptr<cExcludeList>> m_acs_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cExcludeList : public BaseClass
{
    public:
        cExcludeList(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cExcludeList(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cExcludeList();

        uint8_t& opclass();
        uint8_t& exclude_channels_length();
        uint8_t* exclude_channels(size_t idx = 0);
        bool set_exclude_channels(const void* buffer, size_t size);
        bool alloc_exclude_channels(size_t count = 1);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint8_t* m_opclass = nullptr;
        uint8_t* m_exclude_channels_length = nullptr;
        uint8_t* m_exclude_channels = nullptr;
        size_t m_exclude_channels_idx__ = 0;
        int m_lock_order_counter__ = 0;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_ACSCHANNELLIST_H_
