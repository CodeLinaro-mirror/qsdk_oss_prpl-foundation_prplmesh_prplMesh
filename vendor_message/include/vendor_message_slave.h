/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef VENDOR_MESSAGE_SLAVE_H
#define VENDOR_MESSAGE_SLAVE_H

#include "ambiorix_client.h"
#include "wbapi_utils.h"
#include <bcl/beerocks_event_loop.h>
#include <bcl/beerocks_logging.h>
#include <btl/broker_client_factory.h>

using namespace beerocks;

namespace beerocks {
namespace bpl {
extern beerocks::wbapi::AmbiorixClient m_ambiorix_cl;
} // namespace bpl
} // namespace beerocks

namespace vendor_message {
class VendorMessageSlave {
public:
    VendorMessageSlave(std::string broker_uds_path,
                       std::shared_ptr<beerocks::EventLoop> event_loop);
    virtual ~VendorMessageSlave();

    /**
     * @brief Initialize the Vendor Message.
     *
     * @return true on success and false otherwise.
     */
    bool init();

    /**
     * Broker client to exchange CMDU messages with broker server running in transport process.
     */
    std::shared_ptr<btl::BrokerClient> m_broker_client;

    bool send_cmdu_to_controller(const sMacAddr &dst_mac, ieee1905_1::CmduMessageTx &cmdu_tx,
                                 const uint16_t &mid, ieee1905_1::eMessageType msg_type);

    bool should_stop() const { return m_should_stop; };

private:
    sMacAddr agent_almac = {0};
    /**
     * Buffer to hold CMDU to be transmitted.
     */
    uint8_t m_tx_buffer[message::MESSAGE_BUFFER_LENGTH];

    /**
     * CMDU to be transmitted.
     */
    ieee1905_1::CmduMessageTx cmdu_tx;

    std::string m_broker_uds_path;

    std::shared_ptr<beerocks::EventLoop> m_event_loop;

    bool m_should_stop = false;

    /**
     * @brief Handles CMDU message received from broker.
     *
     * This handler is slightly different than the handler for CMDU messages received from other
     * processes as it checks the source and destination MAC addresses set by the original sender.
     * It also filters out messages that are not addressed to the controller.
     *
     * @param iface_index Index of the network interface that the CMDU message was received on.
     * @param dst_mac Destination MAC address.
     * @param src_mac Source MAC address.
     * @param cmdu_rx Received CMDU to be handled.
     * @return true on success and false otherwise.
     */
    bool handle_cmdu_from_broker(uint32_t iface_index, const sMacAddr &dst_mac,
                                 const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);
    /**
     * Factory to create broker client instances connected to broker server.
     * Broker client instances are used to exchange CMDU messages with remote processes running in
     * other devices in the network via the broker server running in the transport process.
     */
    std::unique_ptr<btl::BrokerClientFactory> m_broker_client_factory;

    /**
     * @brief Handles received CMDU message.
     *
     * @param dst_mac Destination MAC address.
     * @param cmdu_rx Received CMDU to be handled.
     * @return true on success and false otherwise.
     */
    bool handle_cmdu(const sMacAddr &dst_mac, ieee1905_1::CmduMessageRx &cmdu_rx);
};
} // namespace vendor_message
#endif // VENDOR_MESSAGE_SLAVE_H
