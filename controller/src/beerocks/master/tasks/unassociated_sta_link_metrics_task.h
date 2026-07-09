/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2021 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#pragma once
#include "../db/db.h"
#include "task.h"

namespace son {
class UnassociatedStaLinkMetricsTask : public task {
public:
    UnassociatedStaLinkMetricsTask(db &database_, ieee1905_1::CmduMessageTx &cmdu_tx_);
    virtual ~UnassociatedStaLinkMetricsTask() {}

    bool handle_ieee1905_1_msg(const sMacAddr &src_mac,
                               ieee1905_1::CmduMessageRx &cmdu_rx) override;

    /**
     * @brief Handles CMDU of 1905 Unassoc Sta Link Metrics Response
     *
     * This handler is written to handle Unassoc Link Metrics Response for the unassociated
     * stations. It will update the map.
     *
     * @param src_mac Source MAC address.
     * @param cmdu_rx Received CMDU to be handled.
     * @return true on success and false otherwise.
     */
    bool
    handle_cmdu_1905_unassociated_station_link_metric_response(const sMacAddr &src_mac,
                                                               ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Method is used for handling event like sending unassoc sta link metrics to supported agent(s)
     * who announced support in capability info.
     *
     * @return none
     */
    void handle_event(int event_enum_value, void *event_obj) override;

    struct sUnAssociatedLinkMetricsQueryEvent {
        uint8_t opClass;
        uint8_t channel;
        sMacAddr unassoc_sta_mac;
    };

    enum eEvent : uint8_t { UNASSOC_STA_LINK_METRICS_QUERY };

protected:
    virtual void work() override;

private:
    db &database;
    ieee1905_1::CmduMessageTx &cmdu_tx;
    std::chrono::steady_clock::time_point last_query_request{};
};

} // namespace son
