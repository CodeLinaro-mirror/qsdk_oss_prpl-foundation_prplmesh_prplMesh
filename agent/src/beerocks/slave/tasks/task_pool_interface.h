/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _TASK_POOL_INTERFACE_H_
#define _TASK_POOL_INTERFACE_H_

#include <bpl/bpl_cfg.h>

#include <easylogging++.h>

#include <memory>
#include <type_traits>

namespace beerocks {

class Task;

/**
 * @brief All possible events in the system are defined here
 */
enum class eTaskEvent {
    CAC_STARTED_NOTIFICATION,          // 0
    CAC_COMPLETED_NOTIFICATION,        // 1
    SWITCH_CHANNEL_NOTIFICATION_EVENT, // 2
    SWITCH_CHANNEL_DURATION_TIME,      // 3
    SWITCH_CHANNEL_REQUEST,            // 4
    SWITCH_CHANNEL_REPORT,             // 5
};

std::ostream &operator<<(std::ostream &o, const eTaskEvent &task_event);

// helper for hashing the event type
struct TaskEventHash {
    std::size_t operator()(eTaskEvent event) const { return static_cast<std::size_t>(event); }
};

/**
 * @brief The TaskPoolInterface provides interface for adding tasks
 * and sending messages between tasks
 */
class TaskPoolInterface {
public:
    /**
     * @brief Add new task to the task pool with 
     * a list of messages this task wants to handle
     * 
     * @param new_task Shared pointer to the task.
     */
    virtual void add_task(const std::shared_ptr<Task> new_task) = 0;

    /**
     * @brief Add task to pool with checking of current management mode: if
     * task for EasyMesh functionality, and currently NMAP mode is
     * configured, task would not be added.
     *
     * @tparam T type of taks, must be derived from 'task'
     * @tparam E variadic template for task constructor arguments
     * @param task_name name of the task, used for logging
     * @param management_mode current management mode, used for checking if task should be added
     * @param args variadic arguments for task constructor
     *
     * @return true if task was added successfully, false otherwise
     */
    template <typename T, typename... E>
    std::shared_ptr<T> add_task_check_mode(const std::string &task_name, int management_mode,
                                           E &&... args);

    /**
     * @brief Send an event to all registered tasks
     * 
     * @param event the id of the event - unique system wide
     * @param event_obj shared pointer to some chunk of memory used to pass data to the task
     */
    virtual void send_event(eTaskEvent event, std::shared_ptr<void> event_obj = nullptr) = 0;
};

template <typename T, typename... E>
std::shared_ptr<T> TaskPoolInterface::add_task_check_mode(const std::string &task_name,
                                                          int management_mode, E &&... args)
{
    static_assert(std::is_base_of<Task, T>::value, "T must be a subclass of 'Task'");

    if (T::easymesh_task && management_mode == BPL_MGMT_MODE_NOT_MULTIAP) {
        LOG(DEBUG) << "EasyMesh task '" << task_name
                   << "' was not added as Non-Multi-AP mode is used";
        return nullptr;
    }

    std::shared_ptr<T> task_ptr = std::make_shared<T>(std::forward<E>(args)...);
    add_task(task_ptr);

    return task_ptr;
}

} // namespace beerocks

#endif
