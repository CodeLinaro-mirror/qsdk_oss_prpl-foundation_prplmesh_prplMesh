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

#ifndef _TLVF_WFA_MAP_TLVBEACONMETRICSRESPONSE_H_
#define _TLVF_WFA_MAP_TLVBEACONMETRICSRESPONSE_H_

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
#include <vector>
#include "tlvf/association_frame/eElementID.h"
#include <tlvf/MisalignedProxy.h>
#include <ostream>

namespace wfa_map {

class cMeasurementReportElement;

class tlvBeaconMetricsResponse : public BaseClass
{
    public:
        tlvBeaconMetricsResponse(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvBeaconMetricsResponse(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvBeaconMetricsResponse();

        const eTlvTypeMap& type();
        const uint16_t& length();
        sMacAddr& associated_sta_mac();
        uint8_t& reserved();
        uint8_t& measurement_report_list_length();
        //Contains a Measurement Report element that was received from the STA
        //since the corresponding Beacon Metrics Query message was received by the Multi-AP Agent
        std::tuple<bool, cMeasurementReportElement&> measurement_report_list(size_t idx);
        std::shared_ptr<cMeasurementReportElement> create_measurement_report_list();
        bool add_measurement_report_list(std::shared_ptr<cMeasurementReportElement> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sMacAddr* m_associated_sta_mac = nullptr;
        uint8_t* m_reserved = nullptr;
        uint8_t* m_measurement_report_list_length = nullptr;
        cMeasurementReportElement* m_measurement_report_list = nullptr;
        size_t m_measurement_report_list_idx__ = 0;
        std::vector<std::shared_ptr<cMeasurementReportElement>> m_measurement_report_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cMeasurementReportElement : public BaseClass
{
    public:
        cMeasurementReportElement(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cMeasurementReportElement(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cMeasurementReportElement();

        enum eElementID: uint8_t {
            ID_MEASUREMENT_REPORT = 0x27,
        };
        // Enum AutoPrint generated code snippet begining- DON'T EDIT!
        // clang-format off
        static const char *eElementID_str(eElementID enum_value) {
            switch (enum_value) {
            case ID_MEASUREMENT_REPORT: return "ID_MEASUREMENT_REPORT";
            }
            static std::string out_str = std::to_string(int(enum_value));
            return out_str.c_str();
        }
        friend inline std::ostream &operator<<(std::ostream &out, eElementID value) { return out << eElementID_str(value); }
        // clang-format on
        // Enum AutoPrint generated code snippet end
        
        enum eMeasurementType: uint8_t {
            TYPE_BEACON = 0x5,
        };
        // Enum AutoPrint generated code snippet begining- DON'T EDIT!
        // clang-format off
        static const char *eMeasurementType_str(eMeasurementType enum_value) {
            switch (enum_value) {
            case TYPE_BEACON: return "TYPE_BEACON";
            }
            static std::string out_str = std::to_string(int(enum_value));
            return out_str.c_str();
        }
        friend inline std::ostream &operator<<(std::ostream &out, eMeasurementType value) { return out << eMeasurementType_str(value); }
        // clang-format on
        // Enum AutoPrint generated code snippet end
        
        enum eBeaconReportStatusCode: uint8_t {
            BEACON_REPORT_RESP_SUCCESS = 0x0,
            BEACON_REPORT_RESP_NO_REPORT = 0x40,
            BEACON_REPORT_RESP_NO_SUPPORT = 0x80,
            BEACON_REPORT_RESP_UNSPECIFIED = 0xc0,
        };
        // Enum AutoPrint generated code snippet begining- DON'T EDIT!
        // clang-format off
        static const char *eBeaconReportStatusCode_str(eBeaconReportStatusCode enum_value) {
            switch (enum_value) {
            case BEACON_REPORT_RESP_SUCCESS:     return "BEACON_REPORT_RESP_SUCCESS";
            case BEACON_REPORT_RESP_NO_REPORT:   return "BEACON_REPORT_RESP_NO_REPORT";
            case BEACON_REPORT_RESP_NO_SUPPORT:  return "BEACON_REPORT_RESP_NO_SUPPORT";
            case BEACON_REPORT_RESP_UNSPECIFIED: return "BEACON_REPORT_RESP_UNSPECIFIED";
            }
            static std::string out_str = std::to_string(int(enum_value));
            return out_str.c_str();
        }
        friend inline std::ostream &operator<<(std::ostream &out, eBeaconReportStatusCode value) { return out << eBeaconReportStatusCode_str(value); }
        // clang-format on
        // Enum AutoPrint generated code snippet end
        
        const eElementID& element_id();
        uint8_t& length();
        uint8_t& measurement_token();
        uint8_t& measurement_req_mode();
        const eMeasurementType& measurement_type();
        uint8_t& op_class();
        uint8_t& channel();
        tlvf_uint64_t start_time();
        uint16_t& duration();
        uint8_t& phy_type();
        uint8_t& rcpi();
        uint8_t& rsni();
        sMacAddr& bssid();
        uint8_t& antenna_id();
        uint32_t& parent_tsf();
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eElementID* m_element_id = nullptr;
        uint8_t* m_length = nullptr;
        uint8_t* m_measurement_token = nullptr;
        uint8_t* m_measurement_req_mode = nullptr;
        eMeasurementType* m_measurement_type = nullptr;
        uint8_t* m_op_class = nullptr;
        uint8_t* m_channel = nullptr;
        uint64_t* m_start_time = nullptr;
        uint16_t* m_duration = nullptr;
        uint8_t* m_phy_type = nullptr;
        uint8_t* m_rcpi = nullptr;
        uint8_t* m_rsni = nullptr;
        sMacAddr* m_bssid = nullptr;
        uint8_t* m_antenna_id = nullptr;
        uint32_t* m_parent_tsf = nullptr;
};

}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_TLVBEACONMETRICSRESPONSE_H_
