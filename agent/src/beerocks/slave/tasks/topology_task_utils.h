/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef _TOPOLOGY_TASK_UTILS_H_
#define _TOPOLOGY_TASK_UTILS_H_

#include <tlvf/CmduMessageTx.h>
#include <tlvf/common/sMacAddr.h>

class topology_task_utils {
    // reliable multicast neighbours map
    struct ieee1905_neighbor {
        sMacAddr al_mac;
        unsigned int if_index;
        std::chrono::steady_clock::time_point last_seen;
    };
    struct SharedData {
        size_t size;
        pthread_mutex_t mutex;
        char neighbourslist[];
    };

    int shm_fd = -1;

public:
    static bool
    fetch_and_populate_non_1905_neighbor_device_tlv(ieee1905_1::CmduMessageTx &m_cmdu_tx);
};
#endif // _TOPOLOGY_TASK_UTILS_H_
