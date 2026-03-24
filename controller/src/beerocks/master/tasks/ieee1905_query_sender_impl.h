/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _IEEE1905_QUERY_SENDER_IMPL_H_
#define _IEEE1905_QUERY_SENDER_IMPL_H_

#include "ieee1905_task.h"

namespace son {

/**
 * @brief Production implementation of \ref IEEE1905QuerySender.
 *
 * Builds and sends IEEE1905 query messages through the real CMDU transport layer
 * using \ref son_actions helpers.
 */
class RealIEEE1905QuerySender : public IEEE1905QuerySender {
public:
    /**
     * @brief Construct the sender.
     *
     * @param database controller database, used to resolve the destination agent socket
     */
    explicit RealIEEE1905QuerySender(db &database);

    /**
     * @brief Send an IEEE1905 Topology Query message.
     *
     * @param dest_mac destination AL MAC address
     * @param cmdu_tx  transmit buffer used to build and send the message
     *
     * @return true on success, false otherwise
     */
    bool send_topology_query(const sMacAddr &dest_mac, ieee1905_1::CmduMessageTx &cmdu_tx) override;

    /**
     * @brief Send an IEEE1905 Higher Layer Query message.
     *
     * @param dest_mac destination AL MAC address
     * @param cmdu_tx  transmit buffer used to build and send the message
     *
     * @return true on success, false otherwise
     */
    bool send_higher_layer_query(const sMacAddr &dest_mac,
                                 ieee1905_1::CmduMessageTx &cmdu_tx) override;

    /**
     * @brief Send an IEEE1905 Link Metric Query (all neighbors, TX+RX) message.
     *
     * @param dest_mac destination AL MAC address
     * @param cmdu_tx  transmit buffer used to build and send the message
     *
     * @return true on success, false otherwise
     */
    bool send_link_metric_query(const sMacAddr &dest_mac,
                                ieee1905_1::CmduMessageTx &cmdu_tx) override;

private:
    db &database; ///< Controller database reference.
};

} // namespace son

#endif
