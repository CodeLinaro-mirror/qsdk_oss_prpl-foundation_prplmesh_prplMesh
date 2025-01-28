/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "vendor_message_slave.h"
#include "agent_db.h"
#include "multi_vendor.h"

#include <bcl/beerocks_cmdu_client_factory.h>
#include <bcl/beerocks_cmdu_server_factory.h>
#include <bcl/beerocks_eventloop_thread.h>
#include <beerocks/tlvf/beerocks_message.h>
#include <btl/broker_client_factory_factory.h>

#define AIRTIES_OUI 0X8841FC

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
    {
        auto db = AgentDB::get();

        std::string iface_mac;
        if (!network_utils::linux_iface_get_mac(db->bridge.iface_name, iface_mac)) {
            LOG(ERROR) << "Failed reading addresses from the bridge!";
        }
        bridge_mac = tlvf::mac_from_string(iface_mac);

        // Filter messages which are not destined to this agent
        if (dst_mac != beerocks::net::network_utils::MULTICAST_1905_MAC_ADDR &&
            dst_mac != bridge_mac) {
            LOG(DEBUG) << "handle_cmdu() - dropping msg, dst_mac=" << dst_mac
                       << ", local_bridge_mac=" << bridge_mac;
            return true;
        }
    }

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
        for (const auto &first_entry : multi_vendor::tlvf_handler::tlv_function_table) {
            switch (first_entry.first) {
            case AIRTIES_OUI:
                //Placeholder for airties tlv parsing
                break;
            default:
                break;
            }
        }
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

    // Subscribe for the reception of CMDU messages that this process is interested in
    if (!m_broker_client->subscribe(std::set<ieee1905_1::eMessageType>{
            ieee1905_1::eMessageType::VENDOR_SPECIFIC_MESSAGE,
        })) {
        LOG(FATAL) << "Failed subscribing to the Bus";
    }

    return true;
}

bool VendorMessageSlave::send_cmdu_to_controller(const sMacAddr &dst_mac,
                                                 ieee1905_1::CmduMessageTx &cmdu_tx,
                                                 const uint16_t &mid,
                                                 ieee1905_1::eMessageType msg_type)
{
    auto db             = AgentDB::get();
    auto cmdu_tx_header = cmdu_tx.create(mid, msg_type);
    if (!cmdu_tx_header) {
        LOG(ERROR) << "cmdu creation of MESSAGE TYPE: " << msg_type << ", has failed";
        return false;
    }

    if (tlvf::mac_to_string(db->bridge.mac).empty()) {
        return m_broker_client->send_cmdu(cmdu_tx, dst_mac, db->bridge.mac);
    } else {
        return m_broker_client->send_cmdu(cmdu_tx, dst_mac, bridge_mac);
    }
}
