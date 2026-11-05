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

#ifndef _TLVF_WFA_MAP_TLVRSNDIAGNOSTICREPORT_H_
#define _TLVF_WFA_MAP_TLVRSNDIAGNOSTICREPORT_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include "tlvf/wfa_map/eTlvTypeMap.h"
#include "tlvf/common/sMacAddr.h"

namespace wfa_map {


class tlvRsnDiagnosticReport : public BaseClass
{
    public:
        tlvRsnDiagnosticReport(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvRsnDiagnosticReport(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvRsnDiagnosticReport();

        const eTlvTypeMap& type();
        const uint16_t& length();
        sMacAddr& bssid();
        sMacAddr& sta_mac();
        uint16_t& rsn_error_type();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sMacAddr* m_bssid = nullptr;
        sMacAddr* m_sta_mac = nullptr;
        uint16_t* m_rsn_error_type = nullptr;
};

}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_TLVRSNDIAGNOSTICREPORT_H_
