/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "ieee1905_transport.h"

namespace beerocks {
namespace transport {

void Ieee1905Transport ::update_non1905_neighbours()
{
    auto mapSize = neighbors_map_.size() * (sizeof(sMacAddr) + sizeof(ieee1905_neighbor));
    auto shm_fd  = shm_open("/neighbors", O_CREAT | O_RDWR, 0666);
    auto ptr     = mmap(0, sizeof(SharedData) + mapSize, PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (ptr == MAP_FAILED) {
        std::cerr << "Failed to map shared memory" << std::endl;
        close(shm_fd);
        return;
    }

    SharedData *p = static_cast<SharedData *>(ptr);

    // Initialize the mutex only once
    static bool is_mutex_initialized = false;
    if (!is_mutex_initialized) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&p->mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        is_mutex_initialized = true;
    }

    p->size = mapSize;

    // Serialize the map to a buffer
    char *buffer  = new char[mapSize];
    char *buf_ptr = buffer;
    for (const auto &pair : neighbors_map_) {
        std::memcpy(buf_ptr, &pair.first, sizeof(sMacAddr));
        buf_ptr += sizeof(sMacAddr);

        std::memcpy(buf_ptr, &pair.second.al_mac, sizeof(sMacAddr));
        buf_ptr += sizeof(sMacAddr);

        std::memcpy(buf_ptr, &pair.second.if_index, sizeof(unsigned int));
        buf_ptr += sizeof(unsigned int);

        auto last_seen_duration = pair.second.last_seen.time_since_epoch().count();
        std::memcpy(buf_ptr, &last_seen_duration, sizeof(last_seen_duration));

        buf_ptr += sizeof(last_seen_duration);
    }
    // Check if the mutex is properly initialized
    int mutex_init_check = pthread_mutex_trylock(&p->mutex);
    if (mutex_init_check == EOWNERDEAD) {
        LOG(DEBUG) << "Mutex owner is dead, recovering";
        pthread_mutex_consistent(&p->mutex);
    } else if (mutex_init_check != 0) {
        LOG(DEBUG) << "Failed to lock mutex, error: " << mutex_init_check;
        munmap(ptr, sizeof(SharedData) + mapSize);
        close(shm_fd);
        return;
    }

    // Copy the buffer to shared memory
    MAPF_INFO("Wirting to shared memory");
    std::memcpy(p->neighbourslist, buffer, mapSize);
    pthread_mutex_unlock(&p->mutex);

    MAPF_INFO("unlocked shared memory");

    delete[] buffer;
    munmap(ptr, sizeof(SharedData) + mapSize);
    close(shm_fd);
}
} // namespace transport
} // namespace beerocks
