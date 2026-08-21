/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "ieee1905_transport.h"

#include <bcl/beerocks_backport.h>
#include <bcl/beerocks_defines.h>
#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/network/bridge_state_manager_impl.h>
#include <bcl/network/bridge_state_monitor_impl.h>
#include <bcl/network/bridge_state_reader_impl.h>
#include <bcl/network/interface_flags_reader_impl.h>
#include <bcl/network/interface_state_manager_impl.h>
#include <bcl/network/interface_state_monitor_impl.h>
#include <bcl/network/interface_state_reader_impl.h>
#include <bcl/network/netlink_event_listener_impl.h>
#include <bcl/network/sockets_impl.h>
#include <bpl/bpl.h>

#include <net/if.h>
#include <unistd.h>

#ifdef INCLUDE_BREAKPAD
#include "breakpad_wrapper.h"
#endif

using namespace beerocks;
using namespace beerocks::net;
using namespace beerocks::transport;

static bool g_running = true;
static int s_signal   = 0;

static void handle_signal()
{
    if (!s_signal)
        return;

    switch (s_signal) {

    // Terminate
    case SIGTERM:
    case SIGINT:
        LOG(INFO) << "Caught signal '" << strsignal(s_signal) << "' Exiting...";
        g_running = false;
        break;

    default:
        LOG(WARNING) << "Unhandled Signal: '" << strsignal(s_signal) << "' Ignoring...";
        break;
    }

    s_signal = 0;
}

static void init_signals()
{
    // Signal handler function
    auto signal_handler = [](int signum) { s_signal = signum; };

    struct sigaction sigterm_action;
    sigterm_action.sa_handler = signal_handler;
    sigemptyset(&sigterm_action.sa_mask);
    sigterm_action.sa_flags = 0;
    sigaction(SIGTERM, &sigterm_action, NULL);

    struct sigaction sigint_action;
    sigint_action.sa_handler = signal_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);
}

static std::shared_ptr<EventLoop> create_event_loop()
{
    // Create application event loop to wait for blocking I/O operations.
    return std::make_shared<EventLoopImpl>();
}

static std::shared_ptr<broker::BrokerServer>
create_broker_server(std::shared_ptr<EventLoop> event_loop)
{
    // UDS path for broker server socket;
    std::string broker_uds_path = std::string(TMP_PATH) + "/" + BEEROCKS_BROKER_UDS;

    // Number of concurrent connections on the server socket
    constexpr int listen_buffer_size = 10;

    // Create the server UDS socket for the message broker
    auto server_socket = std::make_shared<SocketServer>(broker_uds_path, listen_buffer_size);

    // Create the broker server
    return std::make_shared<broker::BrokerServer>(server_socket, event_loop);
}

static std::shared_ptr<NetlinkEventListener>
create_netlink_event_listener(std::shared_ptr<EventLoop> event_loop)
{
    // Create NETLINK_ROUTE netlink socket for kernel/user-space communication
    auto socket = std::make_shared<NetlinkRouteSocket>();

    // Create client socket
    ClientSocketImpl<NetlinkRouteSocket> client(socket);

    // Bind client socket to "route netlink" multicast group to listen for multicast packets sent
    // from the kernel containing network interface create/delete/up/down events
    if (!client.bind(NetlinkAddress(RTMGRP_LINK))) {
        return nullptr;
    }

    // Create connection to send/receive data using this socket
    auto connection = std::make_shared<SocketConnectionImpl>(socket);

    // Create the Netlink event listener
    return std::make_shared<NetlinkEventListenerImpl>(connection, event_loop);
}

static std::shared_ptr<InterfaceStateManager>
create_interface_state_manager(std::shared_ptr<NetlinkEventListener> netlink_event_listener)
{
    // Create the interface state monitor
    auto interface_state_monitor =
        std::make_unique<InterfaceStateMonitorImpl>(netlink_event_listener);

    // Create the interface flags reader
    auto interface_flags_reader = std::make_shared<InterfaceFlagsReaderImpl>();

    // Create the interface state reader
    auto interface_state_reader =
        std::make_unique<InterfaceStateReaderImpl>(interface_flags_reader);

    // Create the interface state manager
    return std::make_shared<InterfaceStateManagerImpl>(std::move(interface_state_monitor),
                                                       std::move(interface_state_reader));
}

static std::shared_ptr<BridgeStateManager>
create_bridge_state_manager(std::shared_ptr<NetlinkEventListener> netlink_event_listener)
{
    // Create the bridge state monitor
    auto bridge_state_monitor = std::make_unique<BridgeStateMonitorImpl>(netlink_event_listener);
    LOG_IF(!bridge_state_monitor, FATAL) << "Unable to create bridge state monitor!";

    // Create the bridge state reader
    auto bridge_state_reader = std::make_unique<BridgeStateReaderImpl>();
    LOG_IF(!bridge_state_reader, FATAL) << "Unable to create bridge state reader!";

    // Create the bridge state manager
    return std::make_shared<BridgeStateManagerImpl>(std::move(bridge_state_monitor),
                                                    std::move(bridge_state_reader));
}

int main(int argc, char *argv[])
{
    std::cout << "IEEE1905 Transport Process Start" << std::endl;

    ELPP_INITIALIZE_SYSLOG("prplmesh", 0, LOG_USER);

#ifdef INCLUDE_BREAKPAD
    breakpad_ExceptionHandler();
#endif

    init_signals();

    mapf::Logger::Instance().LoggerInit("transport");

    if (bpl::bpl_init() < 0) {
        LOG(ERROR) << "Failed to initialize BPL!";
        return 1;
    }

    /**
     * Create required objects in the order defined by the dependency tree.
     */
    auto event_loop = create_event_loop();
    LOG_IF(!event_loop, FATAL) << "Unable to create event loop!";

    auto broker = create_broker_server(event_loop);
    LOG_IF(!broker, FATAL) << "Unable to create message broker!";

    auto netlink_event_listener = create_netlink_event_listener(event_loop);
    LOG_IF(!netlink_event_listener, FATAL) << "Unable to create Netlink event listener!";

    auto interface_state_manager = create_interface_state_manager(netlink_event_listener);
    LOG_IF(!interface_state_manager, FATAL) << "Unable to create interface state manager!";

    auto bridge_state_manager = create_bridge_state_manager(netlink_event_listener);
    LOG_IF(!bridge_state_manager, FATAL) << "Unable to create bridge state manager!";

    /**
     * Create the IEEE1905 transport process.
     */
    Ieee1905Transport ieee1905_transport(interface_state_manager, bridge_state_manager, broker,
                                         event_loop);

    /**
     * Start the message broker
     */
    LOG_IF(!broker->start(), FATAL) << "Unable to start message broker!";

    /**
     * Start the IEEE1905 transport process
     */
    LOG_IF(!ieee1905_transport.start(), FATAL) << "Unable to start transport process!";

    /**
     * Run the application event loop
     */
    MAPF_INFO("starting main loop...");
    while (g_running) {
        // Handle signals
        if (s_signal) {
            handle_signal();
            continue;
        }

        if (event_loop->run() < 0) {
            LOG(ERROR) << "Broker event loop failure!";
            break;
        }
    }
    MAPF_INFO("done");

    /**
     * Stop running components and clean resources
     */
    ieee1905_transport.stop();
    broker->stop();
    bpl::bpl_close();

    return 0;
}
