/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "tlvf_airties_utils_dummy.h"
#include <easylogging++.h>

using namespace airties;

bool tlvf_airties_utils::add_airties_deviceinfo_tlv(ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}

bool tlvf_airties_utils::add_airties_version_reporting_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}

bool tlvf_airties_utils::add_device_metrics(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return false;
}
