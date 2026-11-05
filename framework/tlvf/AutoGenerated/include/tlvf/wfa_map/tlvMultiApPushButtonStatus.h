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

#ifndef _TLVF_WFA_MAP_TLVMULTIAPPUSHBUTTONSTATUS_H_
#define _TLVF_WFA_MAP_TLVMULTIAPPUSHBUTTONSTATUS_H_

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
#include <ostream>

namespace wfa_map {


class tlvMultiApPushButtonStatus : public BaseClass
{
    public:
        tlvMultiApPushButtonStatus(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvMultiApPushButtonStatus(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvMultiApPushButtonStatus();

        enum eResult: uint8_t {
            SUCCESS = 0x0,
            FAILURE_ABORTED = 0x1,
            NOT_STARTED = 0x2,
            STARTED = 0x3,
        };
        // Enum AutoPrint generated code snippet begining- DON'T EDIT!
        // clang-format off
        static const char *eResult_str(eResult enum_value) {
            switch (enum_value) {
            case SUCCESS:         return "SUCCESS";
            case FAILURE_ABORTED: return "FAILURE_ABORTED";
            case NOT_STARTED:     return "NOT_STARTED";
            case STARTED:         return "STARTED";
            }
            static std::string out_str = std::to_string(int(enum_value));
            return out_str.c_str();
        }
        friend inline std::ostream &operator<<(std::ostream &out, eResult value) { return out << eResult_str(value); }
        // clang-format on
        // Enum AutoPrint generated code snippet end
        
        const eTlvTypeMap& type();
        const uint16_t& length();
        //Same value as in the Multi-AP Push-Button Trigger TLV that armed the BSS
        sPbcSessionId& pbc_session_id();
        sMacAddr& bssid();
        eResult& result();
        bool alloc_failure_reason();
        uint16_t* failure_reason();
        bool set_failure_reason(const uint16_t failure_reason);
        //MAC address of the new device, all zeros if result is not 0x00
        sMacAddr& enrollee_mac();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sPbcSessionId* m_pbc_session_id = nullptr;
        sMacAddr* m_bssid = nullptr;
        eResult* m_result = nullptr;
        uint16_t* m_failure_reason = nullptr;
        bool m_failure_reason_allocated = false;
        sMacAddr* m_enrollee_mac = nullptr;
};

}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_TLVMULTIAPPUSHBUTTONSTATUS_H_
