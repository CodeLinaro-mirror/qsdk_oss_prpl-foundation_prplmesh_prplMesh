/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "../topology_task_utils.h"
#include <easylogging++.h>
#include <tlvf/CmduMessageTx.h>

bool topology_task_utils::fetch_and_populate_non_1905_neighbor_device_tlv(
    ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    LOG(TRACE) << __func__ << " - NOT IMPLEMENTED";
    return true;
}
