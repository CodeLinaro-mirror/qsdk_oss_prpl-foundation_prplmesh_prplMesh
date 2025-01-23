/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "vendor_message_slave.h"
#include "agent_db.h"

#include <bcl/beerocks_cmdu_client_factory.h>
#include <bcl/beerocks_cmdu_server_factory.h>
#include <bcl/beerocks_eventloop_thread.h>
#include <beerocks/tlvf/beerocks_message.h>
#include <btl/broker_client_factory_factory.h>

using namespace multi_vendor;
using namespace beerocks;
using namespace net;
using namespace son;

VendorMessageSlave::VendorMessageSlave(sVendorMessageConfig conf, beerocks::logging &logger_)
    : cmdu_tx(m_tx_buffer, sizeof(m_tx_buffer)), config(conf), logger(logger_)
{
    thread_name = BEEROCKS_V_MESSAGE;

    //Set configuration on Agent database.
    auto db               = AgentDB::get();
    db->bridge.iface_name = conf.bridge_iface;
}

VendorMessageSlave::~VendorMessageSlave() { LOG(DEBUG) << "destructor - VendorMessageSlave"; }

bool VendorMessageSlave::handle_cmdu_from_broker(uint32_t iface_index, const sMacAddr &dst_mac,
                                                 const sMacAddr &src_mac,
                                                 ieee1905_1::CmduMessageRx &cmdu_rx)
{
    //placeholder for handling 1905 messages.
    if (!handle_cmdu(src_mac, cmdu_rx)) {
        LOG(DEBUG) << "multi_vendor handle_cmdu failed";
        return false;
    }

    return true;
}

bool VendorMessageSlave::handle_cmdu(const sMacAddr &dst_mac, ieee1905_1::CmduMessageRx &cmdu_rx)
{

    if (cmdu_rx.getMessageType() == ieee1905_1::eMessageType::VENDOR_SPECIFIC_MESSAGE) {

        //The Vendor-Specific TLV will be handle here so each vendor has to register under
        //multi_vendor::tlvf_handler::tlv_function_table this table.
    }
    return true;
}

bool VendorMessageSlave::thread_init()
{
    /** Logger Initialization **/
    logger.set_thread_name(logger.get_module_name());
    logger.attach_current_thread_to_logger_id();

    /**  Broker Client  **/

    // Create broker client factory to create broker clients when requested
    std::string broker_uds_path = config.temp_path + std::string(BEEROCKS_BROKER_UDS);
    m_broker_client_factory =
        beerocks::btl::create_broker_client_factory(broker_uds_path, m_event_loop);
    LOG_IF(!m_broker_client_factory, FATAL) << "Unable to create broker client factory!";

    // Create an instance of a broker client connected to the broker server that is running in the
    // transport process
    m_broker_client = m_broker_client_factory->create_instance();
    LOG_IF(!m_broker_client, FATAL) << "Failed to create instance of broker client";

    beerocks::btl::BrokerClient::EventHandlers broker_client_handlers;
    // Install a CMDU-received event handler for CMDU messages received from the transport process.
    // These messages are actually been sent by a remote process and the broker server running in
    // the transport process just forwards them to the broker client.
    broker_client_handlers.on_cmdu_received = [&](uint32_t iface_index, const sMacAddr &dst_mac,
                                                  const sMacAddr &src_mac,
                                                  ieee1905_1::CmduMessageRx &cmdu_rx) {
        handle_cmdu_from_broker(iface_index, dst_mac, src_mac, cmdu_rx);
    };

    // Install a connection-closed event handler.
    // Currently there is no recovery mechanism if connection with broker server gets interrupted
    // (something that happens if the transport process dies). Just log a message and exit
    broker_client_handlers.on_connection_closed = [&]() {
        LOG(ERROR) << "Broker client got disconnected!";
        return false;
    };

    m_broker_client->set_handlers(broker_client_handlers);

    return true;
}
