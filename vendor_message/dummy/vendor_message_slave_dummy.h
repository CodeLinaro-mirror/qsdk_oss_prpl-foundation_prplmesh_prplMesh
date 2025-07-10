/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef VENDOR_MESSAGE_SLAVE_DUMMY_H
#define VENDOR_MESSAGE_SLAVE_DUMMY_H

#include <bcl/beerocks_eventloop_thread.h>
#include <bcl/beerocks_logging.h>
#include <btl/broker_client_factory.h>

using namespace beerocks;

namespace vendor_message {

class VendorMessageSlave : public EventLoopThread {
public:
    /**
     * @brief Initialize the Vendor Message.
     *
     * @return true on success and false otherwise.
     */
    bool thread_init() override;

    /**
     * Broker client to exchange CMDU messages with broker server running in transport process.
     */
    bool send_cmdu_to_controller(const sMacAddr &dst_mac, ieee1905_1::CmduMessageTx &cmdu_tx,
                                 const uint16_t &mid, ieee1905_1::eMessageType msg_type);
};
} // namespace vendor_message
#endif // VENDOR_MESSAGE_SLAVE_DUMMY_H
