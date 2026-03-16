/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _IEEE1905_TASK_H_
#define _IEEE1905_TASK_H_

#include "../db/db.h"
#include "task.h"

#include <chrono>
#include <functional>
#include <memory>

namespace son {

class IEEE1905QuerySender {
public:
    virtual ~IEEE1905QuerySender() = default;

    virtual bool send_topology_query(const sMacAddr &dest_mac,
                                     ieee1905_1::CmduMessageTx &cmdu_tx)     = 0;
    virtual bool send_higher_layer_query(const sMacAddr &dest_mac,
                                         ieee1905_1::CmduMessageTx &cmdu_tx) = 0;
};

class ieee1905_task : public task {
public:
    using steady_clock = std::chrono::steady_clock;
    using time_point   = steady_clock::time_point;
    using now_f        = std::function<time_point()>;

    enum eEventType {
        IEEE1905_NETWORK_ENABLE_CHANGED = 1,
    };

    ieee1905_task(db &database, ieee1905_1::CmduMessageTx &cmdu_tx,
                  std::unique_ptr<IEEE1905QuerySender> query_sender, now_f now = steady_clock::now);

    bool handle_ieee1905_1_msg(const sMacAddr &src_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx) override;

protected:
    void work() override;
    void handle_event(int event_type, void *obj) override;

    void set_ieee1905_network_enabled(bool enabled);
    bool set_network_status(const std::string &status);
    bool start_local_al_discovery();

    db &database;
    ieee1905_1::CmduMessageTx &cmdu_tx;
    std::unique_ptr<IEEE1905QuerySender> query_sender;
    now_f now;
};

} // namespace son

#endif
