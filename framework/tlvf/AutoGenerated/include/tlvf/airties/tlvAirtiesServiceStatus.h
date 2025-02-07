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

#ifndef _TLVF_AIRTIES_TLVAIRTIESSERVICESTATUS_H_
#define _TLVF_AIRTIES_TLVAIRTIESSERVICESTATUS_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <asm/byteorder.h>

namespace airties {


class tlvAirtiesServiceStatus : public BaseClass
{
    public:
        tlvAirtiesServiceStatus(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesServiceStatus(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesServiceStatus();

        typedef struct sStatus {
            #if defined(__LITTLE_ENDIAN_BITFIELD)
            uint8_t reserved : 3;
            uint8_t bh_traffic : 1;
            uint8_t ethernet : 1;
            uint8_t bh_sta_radios : 1;
            uint8_t bh_ap_radios : 1;
            uint8_t fh_ap_radios : 1;
            #elif defined(__BIG_ENDIAN_BITFIELD)
            uint8_t fh_ap_radios : 1;
            uint8_t bh_ap_radios : 1;
            uint8_t bh_sta_radios : 1;
            uint8_t ethernet : 1;
            uint8_t bh_traffic : 1;
            uint8_t reserved : 3;
            #else
            #error "Bitfield macros are not defined"
            #endif
            void struct_swap(){
            }
            void struct_init(){
            }
        } __attribute__((packed)) sStatus;
        
        typedef struct sServiceStatusTlvPayload {
            sStatus status;
            uint8_t reason;
            uint16_t bh_on_time;
            uint16_t bh_off_time;
            void struct_swap(){
                status.struct_swap();
                tlvf_swap(16, reinterpret_cast<uint8_t*>(&bh_on_time));
                tlvf_swap(16, reinterpret_cast<uint8_t*>(&bh_off_time));
            }
            void struct_init(){
                status.struct_init();
            }
        } __attribute__((packed)) sServiceStatusTlvPayload;
        
        uint16_t& tlv_id();
        sServiceStatusTlvPayload& payload();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint16_t* m_tlv_id = nullptr;
        sServiceStatusTlvPayload* m_payload = nullptr;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVAIRTIESSERVICESTATUS_H_
