/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "ieee1905_task.h"

#include <easylogging++.h>

using namespace son;

ieee1905_task::ieee1905_task(db &database, ieee1905_1::CmduMessageTx &cmdu_tx,
                             std::unique_ptr<IEEE1905QuerySender> query_sender_, now_f now_)
    : task("ieee1905_task"), database(database), cmdu_tx(cmdu_tx),
      query_sender(std::move(query_sender_)), now(std::move(now_))
{
    database.assign_ieee1905_task_id(id);
    LOG_IF(!query_sender, FATAL) << "IEEE1905 query sender is a null pointer!";
    LOG_IF(!now, FATAL) << "IEEE1905 clock callback is empty!";

    bool network_enable = true;
    auto ambiorix       = database.get_ambiorix_obj();

    if (!ambiorix ||
        !ambiorix->read_param(IEEE1905_ROOT_DM ".Network", "Enable", &network_enable)) {
        LOG(WARNING) << "Failed to read " << IEEE1905_ROOT_DM
                     << ".Network.Enable, using default: " << network_enable;
    }

    set_ieee1905_network_enabled(network_enable);
}

void ieee1905_task::work()
{
    // Skeleton: no periodic flow yet.
}

bool ieee1905_task::handle_ieee1905_1_msg(const sMacAddr &, ieee1905_1::CmduMessageRx &)
{
    // Skeleton: message handling is added in follow-up commits.
    return false;
}

void ieee1905_task::set_ieee1905_network_enabled(bool enabled)
{
    if (!enabled) {
        database.ieee1905_network.reset();
        return;
    }

    if (!database.ieee1905_network) {
        database.ieee1905_network = std::make_unique<db::ieee1905_network_db>();
        set_network_status("Incomplete");

        if (!start_local_al_discovery()) {
            LOG(ERROR) << "Failed to start local IEEE1905 discovery";
        }
    }
}

bool ieee1905_task::set_network_status(const std::string &status)
{
    auto ambiorix = database.get_ambiorix_obj();
    if (!ambiorix) {
        LOG(ERROR) << "Failed to update IEEE1905 network status: Ambiorix is not available";
        return false;
    }

    if (!ambiorix->set(std::string(IEEE1905_ROOT_DM) + ".Network", "Status", status)) {
        LOG(ERROR) << "Failed to set " << IEEE1905_ROOT_DM << ".Network.Status to " << status;
        return false;
    }

    return true;
}

bool ieee1905_task::start_local_al_discovery()
{
    if (!query_sender->send_topology_query(database.get_local_bridge_mac(), cmdu_tx)) {
        LOG(ERROR) << "Failed to send topology query to local bridge "
                   << database.get_local_bridge_mac();
        return false;
    }

    return true;
}

void ieee1905_task::handle_event(int event_type, void *obj)
{
    if (event_type != IEEE1905_NETWORK_ENABLE_CHANGED) {
        return;
    }

    if (!obj) {
        LOG(ERROR) << "enable changed event without payload";
        return;
    }

    auto enabled = *static_cast<bool *>(obj);
    LOG(INFO) << IEEE1905_ROOT_DM << ".Network.Enable changed to " << enabled;
    set_ieee1905_network_enabled(enabled);
}
