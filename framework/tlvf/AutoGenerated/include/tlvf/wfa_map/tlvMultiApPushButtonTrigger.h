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

#ifndef _TLVF_WFA_MAP_TLVMULTIAPPUSHBUTTONTRIGGER_H_
#define _TLVF_WFA_MAP_TLVMULTIAPPUSHBUTTONTRIGGER_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include "tlvf/wfa_map/eTlvTypeMap.h"
#include "tlvf/wfa_map/sPbcSessionId.h"
#include "tlvf/common/sMacAddr.h"
#include <tuple>
#include <asm/byteorder.h>
#include <ostream>

namespace wfa_map {


class tlvMultiApPushButtonTrigger : public BaseClass
{
    public:
        tlvMultiApPushButtonTrigger(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvMultiApPushButtonTrigger(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvMultiApPushButtonTrigger();

        typedef struct sPushButtonMethod {
            #if defined(__LITTLE_ENDIAN_BITFIELD)
            uint8_t dpp : 1;
            uint8_t wps : 1;
            uint8_t reserved : 6;
            #elif defined(__BIG_ENDIAN_BITFIELD)
            uint8_t reserved : 6;
            uint8_t wps : 1;
            uint8_t dpp : 1;
            #else
            #error "Bitfield macros are not defined"
            #endif
            void struct_swap(){
            }
            void struct_init(){
            }
        } __attribute__((packed)) sPushButtonMethod;
        
        enum eState: uint8_t {
            STOP = 0x0,
            START = 0x1,
        };
        // Enum AutoPrint generated code snippet begining- DON'T EDIT!
        // clang-format off
        static const char *eState_str(eState enum_value) {
            switch (enum_value) {
            case STOP:  return "STOP";
            case START: return "START";
            }
            static std::string out_str = std::to_string(int(enum_value));
            return out_str.c_str();
        }
        friend inline std::ostream &operator<<(std::ostream &out, eState value) { return out << eState_str(value); }
        // clang-format on
        // Enum AutoPrint generated code snippet end
        
        const eTlvTypeMap& type();
        const uint16_t& length();
        sPbcSessionId& pbc_session_id();
        //Method(s) to start or stop listening for
        sPushButtonMethod& push_button_method();
        //Whether to arm or disarm listening on the listed BSSIDs
        eState& state();
        uint8_t& bssid_list_length();
        std::tuple<bool, sMacAddr&> bssid_list(size_t idx);
        bool alloc_bssid_list(size_t count = 1);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sPbcSessionId* m_pbc_session_id = nullptr;
        sPushButtonMethod* m_push_button_method = nullptr;
        eState* m_state = nullptr;
        uint8_t* m_bssid_list_length = nullptr;
        sMacAddr* m_bssid_list = nullptr;
        size_t m_bssid_list_idx__ = 0;
        int m_lock_order_counter__ = 0;
};

}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_TLVMULTIAPPUSHBUTTONTRIGGER_H_
