/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "multi_vendor.h"
#include "tlvf_airties_utils_dummy.h"

using namespace airties;
using namespace ieee1905_1;

/*
 * Dummy vendor-specific TLV registration for legacy platforms.
 * The TLV functions are placeholders and not implemented.
 * This file exists only to preserve the structure for future extensions.
 */
class MultiVendorAirties : public multi_vendor::tlvf_handler, public airties::tlvf_airties_utils {
public:
    MultiVendorAirties()
    {
        LOG(INFO) << "MultiVendorAirties (dummy): registering handlers";

        multi_vendor::tlvf_handler::register_handler(eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE,
                                                     add_airties_version_reporting_tlv);
        multi_vendor::tlvf_handler::register_handler(eMessageType::AP_AUTOCONFIGURATION_WSC_MESSAGE,
                                                     add_airties_deviceinfo_tlv);

        multi_vendor::tlvf_handler::register_handler(
            eMessageType::AP_AUTOCONFIGURATION_SEARCH_MESSAGE, add_airties_version_reporting_tlv);

        multi_vendor::tlvf_handler::register_handler(eMessageType::AP_CAPABILITY_REPORT_MESSAGE,
                                                     add_airties_version_reporting_tlv);

        multi_vendor::tlvf_handler::register_handler(eMessageType::AP_METRICS_RESPONSE_MESSAGE,
                                                     add_device_metrics);

        multi_vendor::tlvf_handler::register_handler(eMessageType::TOPOLOGY_RESPONSE_MESSAGE,
                                                     add_airties_ethernet_interface_tlv);
    }
};

namespace {
MultiVendorAirties g_airties_vendor_init;
}
