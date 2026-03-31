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

#ifndef _TLVF_IEEE_1905_1_TLV1905PROFILEVERSION_H_
#define _TLVF_IEEE_1905_1_TLV1905PROFILEVERSION_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include "tlvf/ieee_1905_1/eTlvType.h"
#include "tlvf/ieee_1905_1/e1905ProfileVersion.h"

namespace ieee1905_1 {


class tlv1905ProfileVersion : public BaseClass
{
    public:
        tlv1905ProfileVersion(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlv1905ProfileVersion(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlv1905ProfileVersion();

        const eTlvType& type();
        const uint16_t& length();
        e1905ProfileVersion& version();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvType* m_type = nullptr;
        uint16_t* m_length = nullptr;
        e1905ProfileVersion* m_version = nullptr;
};

}; // close namespace: ieee1905_1

#endif //_TLVF/IEEE_1905_1_TLV1905PROFILEVERSION_H_
