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

#ifndef _TLVF_WFA_MAP_TLVSUPPORTEDCIPHERSUITES_H_
#define _TLVF_WFA_MAP_TLVSUPPORTEDCIPHERSUITES_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <tuple>
#include <ostream>
#include "tlvf/ieee_1905_1/sVendorOUI.h"
#include "tlvf/wfa_map/eTlvTypeMap.h"

namespace wfa_map {


class tlvSupportedCipherSuites : public BaseClass
{
    public:
        tlvSupportedCipherSuites(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvSupportedCipherSuites(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvSupportedCipherSuites();

        typedef struct sCipherSuiteSelector {
            //Cipher suite selector OUI (typically IEEE 802.11 OUI 00:0F:AC).
            sVendorOUI oui;
            //Cipher suite type (e.g. CCMP=4, GCMP-128=8, BIP-CMAC-128=6, etc.).
            uint8_t cipher_suite_type;
            void struct_swap(){
                oui.struct_swap();
            }
            void struct_init(){
            }
        } __attribute__((packed)) sCipherSuiteSelector;
        
        enum eCipherSuiteOUI: uint32_t {
            IEEE80211 = 0xfac,
        };
        // Enum AutoPrint generated code snippet begining- DON'T EDIT!
        // clang-format off
        static const char *eCipherSuiteOUI_str(eCipherSuiteOUI enum_value) {
            switch (enum_value) {
            case IEEE80211: return "IEEE80211";
            }
            static std::string out_str = std::to_string(int(enum_value));
            return out_str.c_str();
        }
        friend inline std::ostream &operator<<(std::ostream &out, eCipherSuiteOUI value) { return out << eCipherSuiteOUI_str(value); }
        // clang-format on
        // Enum AutoPrint generated code snippet end
        
        const eTlvTypeMap& type();
        const uint16_t& length();
        uint8_t& number_of_cipher_suite_selectors();
        std::tuple<bool, sCipherSuiteSelector&> cipher_suite_selectors(size_t idx);
        bool alloc_cipher_suite_selectors(size_t count = 1);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        uint8_t* m_number_of_cipher_suite_selectors = nullptr;
        sCipherSuiteSelector* m_cipher_suite_selectors = nullptr;
        size_t m_cipher_suite_selectors_idx__ = 0;
        int m_lock_order_counter__ = 0;
};

}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_TLVSUPPORTEDCIPHERSUITES_H_
