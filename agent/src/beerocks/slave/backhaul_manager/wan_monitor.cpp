/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "wan_monitor.h"

#include <bcl/beerocks_logging.h>
#include <bcl/network/network_utils.h>
#include <easylogging++.h>

#include <errno.h>           // errno
#include <linux/netlink.h>   // Netlink
#include <linux/rtnetlink.h> // Netlink
#include <net/if.h>          // IFF_*, ifreq
#include <netinet/in.h>      // IPPROTO_IP
#include <unistd.h>          // close

#include <utility>

using namespace beerocks::net;

namespace beerocks {

//////////////////////////////////////////////////////////////////////////////
/////////////////////////// Local Module Functions ///////////////////////////
//////////////////////////////////////////////////////////////////////////////

static int netlink_open_socket()
{
    // Open a netlink socket
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        LOG(ERROR) << "Failed creating netlink socket: " << strerror(errno);
        return -1;
    }

    // Initialize the netlink address (mainly groups)
    struct sockaddr_nl addr = {0};

    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK;
    // RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_NEIGH;

    // Bind the socket
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG(ERROR) << "Failed binding the netlink socket";
        close(fd);

        return (-1);
    }

    return (fd);
}

static const char *link_state_to_string(beerocks::wan_monitor::ELinkState state)
{
    switch (state) {
    case beerocks::wan_monitor::ELinkState::eUp:
        return "up";
    case beerocks::wan_monitor::ELinkState::eDown:
        return "down";
    case beerocks::wan_monitor::ELinkState::eInvalid:
    default:
        return "invalid";
    }
}

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Implementation ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

wan_monitor::wan_monitor() : m_iNetlinkFD(-1) {}

wan_monitor::~wan_monitor()
{
    if (m_iNetlinkFD != -1)
        close(m_iNetlinkFD);
}

bool wan_monitor::initialize(const std::vector<std::string> &iface_names)
{
    if (iface_names.empty()) {
        LOG(ERROR) << "WAN Monitor initialized on empty interface name!";
        return false;
    }

    // Validate first.
    // This avoids destroying previous state if list is empty or invalid
    std::unordered_set<std::string> monitored_ifaces;
    for (const auto &iface_name : iface_names) {
        if (!iface_name.empty()) {
            monitored_ifaces.insert(iface_name);
        }
    }

    if (monitored_ifaces.empty()) {
        LOG(ERROR) << "WAN Monitor initialized on invalid interface names!";
        return false;
    }

    // Close the previous FD
    if (m_iNetlinkFD != -1) {
        // NOTE: If the FD is used in an external select(), it's up to the
        //       user to remove it before calling this method.

        close(m_iNetlinkFD);
        m_iNetlinkFD = -1;
    }

    // Open a new netlink socket
    if ((m_iNetlinkFD = netlink_open_socket()) == -1) {
        return false;
    }

    m_monitored_ifaces = std::move(monitored_ifaces);

    return true;
}

// For compatibility
wan_monitor::ELinkState wan_monitor::initialize(const std::string &strWanIfaceName)
{
    if (!initialize(std::vector<std::string>{strWanIfaceName})) {
        return ELinkState::eInvalid;
    }

    return network_utils::linux_iface_is_up_and_running(strWanIfaceName) ? ELinkState::eUp
                                                                         : ELinkState::eDown;
}

bool wan_monitor::process(std::vector<LinkEvent> &events)
{
    // Return only events from this recvmsg() call
    events.clear();

    if (m_iNetlinkFD == -1) {
        LOG(ERROR) << "Invalid netlink socket!";
        return false;
    }

    struct sockaddr_nl addr = {0};
    struct iovec iov        = {m_arrNLBuff, sizeof m_arrNLBuff};
    struct msghdr msg       = {0};
    msg.msg_name            = (void *)&addr;
    msg.msg_namelen         = sizeof(addr);
    msg.msg_iov             = &iov;
    msg.msg_iovlen          = 1;
    msg.msg_control         = nullptr;
    msg.msg_controllen      = 0;
    msg.msg_flags           = 0;

    // Read a message from the netlink socket
    // The buffer can contain more than one nlmsg, so keep iterating below.
    ssize_t len = recvmsg(m_iNetlinkFD, &msg, 0);

    if (len < 0) {
        LOG(ERROR) << "recvmsg error: " << strerror(errno);
        return false;
    } else if (len == 0) {
        LOG(DEBUG) << "recvmsg EOF";
        return false;
    }

    // Process received message(s)
    struct nlmsghdr *hnl = nullptr;
    for (hnl = (struct nlmsghdr *)m_arrNLBuff; NLMSG_OK(hnl, uint32_t(len));
         hnl = NLMSG_NEXT(hnl, len)) {

        // Completed reading multipart netlink response.
        if (hnl->nlmsg_type == NLMSG_DONE) {
            return true;
        }

        // Error in the Message
        if (hnl->nlmsg_type == NLMSG_ERROR) {
            LOG(ERROR) << "NLMSG_ERROR - Invalid netlink message!";
            return false;
        }

        // WAN monitor currently cares only about link state notification.
        if (hnl->nlmsg_type != RTM_NEWLINK && hnl->nlmsg_type != RTM_DELLINK) {
            continue;
        }

        struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(hnl);

        // Convert the interface index to name so
        // BackhaulManager can update its per candidate state by iface name.
        auto iface_name = network_utils::linux_get_iface_name(ifi->ifi_index);

        // Skip events for other interfaces.
        // Keep only the configured or discovered wired backhaul candidates.
        if (m_monitored_ifaces.find(iface_name) == m_monitored_ifaces.end()) {
            LOG(DEBUG) << "Link detected for non-monitored interface '" << iface_name
                       << "'. Skipping...";
            continue;
        }

        LinkEvent event;
        event.iface_name = std::move(iface_name);
        event.nlmsg_type = int(hnl->nlmsg_type);

        // RTM_NEWLINK with IFF_RUNNING means link is detected.
        // RTM_DELLINK or RTM_NEWLINK without IFF_RUNNING is treated as down.
        event.link_state = (hnl->nlmsg_type == RTM_NEWLINK && (ifi->ifi_flags & IFF_RUNNING))
                               ? ELinkState::eUp
                               : ELinkState::eDown;

        LOG(DEBUG) << "Interface '" << event.iface_name << "', msg_type: " << event.nlmsg_type
                   << ", link_state: " << link_state_to_string(event.link_state);

        events.push_back(std::move(event));
    }

    return true;
}

// For compatibility
// BTW it seems that noone calls process() at all...
wan_monitor::ELinkState wan_monitor::process()
{
    std::vector<LinkEvent> events;

    // Old API behavior: return one link state.
    if (!process(events) || events.empty()) {
        return ELinkState::eInvalid;
    }

    return events.front().link_state;
}

} // namespace beerocks
