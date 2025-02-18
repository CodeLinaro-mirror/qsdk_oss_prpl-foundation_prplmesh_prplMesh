/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "topology_task_utils.h"
#include "../agent_db.h"
#include "topology_task.h"
#include <bcl/network/network_utils.h>
#include <easylogging++.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <tlvf/ieee_1905_1/tlvNon1905neighborDeviceList.h>

using namespace beerocks;
using namespace net;
using namespace son;

std::mutex mtx;

bool topology_task_utils::fetch_and_populate_non_1905_neighbor_device_tlv(
    ieee1905_1::CmduMessageTx &m_cmdu_tx)
{
    topology_task_utils topo_task_utils;

    auto db            = AgentDB::get();
    std::string inface = db->backhaul.selected_iface_name;

    if ((!db->device_conf.local_controller) && inface.empty()) {
        LOG(ERROR) << "Backhaul is not connected";
        return false;
    }

    std::unordered_map<sMacAddr, std::vector<sMacAddr>> iface =
        network_utils::linux_iface_get_pci_info(inface, db->device_conf.local_controller);
    if (iface.empty()) {
        LOG(INFO) << "Interface not found in the list";
        return true;
    }

    topo_task_utils.shm_fd = shm_open("/neighbors", O_RDWR, 0666);
    if (topo_task_utils.shm_fd == -1) {
        LOG(DEBUG) << "Failed to open shared memory" << std::strerror(errno);
        return false;
    }

    // Calculate the size of the serialized data
    auto ptr =
        mmap(0, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, topo_task_utils.shm_fd, 0);
    if (ptr == MAP_FAILED) {
        LOG(DEBUG) << "Failed to map shared memory" << std::strerror(errno);
        close(topo_task_utils.shm_fd);
        return false;
    }

    SharedData *p   = static_cast<SharedData *>(ptr);
    size_t dataSize = p->size;
    munmap(ptr, sizeof(SharedData));
    LOG(DEBUG) << "Data size: " << dataSize;

    ptr = mmap(0, sizeof(SharedData) + dataSize, PROT_READ | PROT_WRITE, MAP_SHARED,
               topo_task_utils.shm_fd, 0);
    if (ptr == MAP_FAILED) {
        LOG(DEBUG) << "Failed to remap shared memory" << std::strerror(errno);
        close(topo_task_utils.shm_fd);
        return false;
    }

    // Deserialize the unordered map
    std::unordered_map<sMacAddr, ieee1905_neighbor> neighbors;
    p = static_cast<SharedData *>(ptr);
    // Check if the mutex is properly initialized
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1; // Wait for 1 seconds
    int mutex_init_check = pthread_mutex_timedlock(&p->mutex, &ts);
    if (mutex_init_check == EOWNERDEAD) {
        LOG(DEBUG) << "Mutex owner is dead, recovering";
        pthread_mutex_consistent(&p->mutex);
    } else if (mutex_init_check != 0) {
        LOG(DEBUG) << "Failed to lock mutex, error: " << mutex_init_check;
        munmap(ptr, sizeof(SharedData) + dataSize);
        close(topo_task_utils.shm_fd);
        return false;
    }

    LOG(DEBUG) << "Shared memory got the lock";
    char *buf_ptr = p->neighbourslist;
    sMacAddr key;
    ieee1905_neighbor value;
    while (buf_ptr < p->neighbourslist + dataSize) {
        memset(&key, 0, sizeof(key));
        value = {0};
        LOG(DEBUG) << "Shared memory deserialising neighbors";
        std::memcpy(&key, buf_ptr, sizeof(sMacAddr));
        buf_ptr += sizeof(sMacAddr);

        std::memcpy(&value.al_mac, buf_ptr, sizeof(value.al_mac));
        buf_ptr += sizeof(value.al_mac);

        std::memcpy(&value.if_index, buf_ptr, sizeof(value.if_index));
        buf_ptr += sizeof(value.if_index);

        int64_t duration_ns;
        std::memcpy(&duration_ns, buf_ptr, sizeof(duration_ns));
        value.last_seen =
            std::chrono::steady_clock::time_point(std::chrono::nanoseconds(duration_ns));
        buf_ptr += sizeof(value.last_seen);

        neighbors.emplace(key, value);
    }
    LOG(DEBUG) << "Shared memory released the lock: ";
    pthread_mutex_unlock(&p->mutex);

    for (auto &neighbor_on_local_iface_entry : neighbors) {
        auto &neighbor_al_mac = neighbor_on_local_iface_entry.first;

        auto str_almac    = tlvf::mac_to_string(neighbor_al_mac);
        auto substr_almac = str_almac.substr(2, str_almac.length());

        for (auto &non1905_list : iface) {
            for (auto &non1905_mac : non1905_list.second) {
                if (neighbor_al_mac == non1905_mac) {
                    auto str    = tlvf::mac_to_string(non1905_mac);
                    auto substr = str.substr(2, str.length());
                    auto iter   = std::find(non1905_list.second.begin(), non1905_list.second.end(),
                                          non1905_mac);
                    if (iter != non1905_list.second.end()) {
                        non1905_list.second.erase(iter);
                    }
                }

                for (auto &non1905_almac : non1905_list.second) {
                    auto matchstr = tlvf::mac_to_string(non1905_almac);
                    if (matchstr.find(substr_almac) != std::string::npos) {
                        auto iter = std::find(non1905_list.second.begin(),
                                              non1905_list.second.end(), non1905_almac);
                        if (iter != non1905_list.second.end()) {
                            non1905_list.second.erase(iter);
                        }
                    }
                }
            }
        }
    }
    if (ftruncate(topo_task_utils.shm_fd, sizeof(SharedData) + dataSize) != 0) {
        LOG(ERROR) << "ftruncate failed : " << std::strerror(errno);
    }
    munmap(ptr, sizeof(SharedData) + dataSize);
    close(topo_task_utils.shm_fd);

    for (auto &non1905_list : iface) {
        std::shared_ptr<ieee1905_1::tlvNon1905neighborDeviceList> tlvNon1905neighborDeviceList =
            nullptr;
        size_t index     = 0;
        int size_of_host = non1905_list.second.size();
        if (size_of_host == 0) {
            continue;
        }

        tlvNon1905neighborDeviceList =
            m_cmdu_tx.addClass<ieee1905_1::tlvNon1905neighborDeviceList>();

        if (!tlvNon1905neighborDeviceList) {
            LOG(ERROR) << "addClass ieee1905_1::tlvNon1905neighborDeviceList failed";
            return false;
        }
        if (!tlvNon1905neighborDeviceList->alloc_mac_non_1905_device(size_of_host)) {
            LOG(ERROR) << "alloc_mac_non_1905_device() has failed";
            return false;
        }
        for (auto &non1905_mac : non1905_list.second) {
            tlvNon1905neighborDeviceList->mac_local_iface() = (sMacAddr &)non1905_list.first;
            auto mac_al_non_1905_device_tuple =
                tlvNon1905neighborDeviceList->mac_non_1905_device(index);
            if (!std::get<0>(mac_al_non_1905_device_tuple)) {
                LOG(ERROR) << "getting mac_non_1905_device element has failed";
                return false;
            }
            auto &mac_non_1905_device = std::get<1>(mac_al_non_1905_device_tuple);
            mac_non_1905_device       = non1905_mac;
            index++;
        }
    }
    return true;
}
