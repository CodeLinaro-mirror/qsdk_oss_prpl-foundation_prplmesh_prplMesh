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

#include <algorithm>
#include <cstring>
#include <errno.h>           // errno
#include <linux/netlink.h>   // Netlink
#include <linux/rtnetlink.h> // Netlink
#include <net/if.h>          // IFF_*, ifreq
#include <netinet/in.h>      // IPPROTO_IP
#include <unistd.h>          // close

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

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Implementation ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

wan_monitor::wan_monitor() : m_iNetlinkFD(-1) {}

wan_monitor::~wan_monitor()
{
    if (m_iNetlinkFD != -1)
        close(m_iNetlinkFD);
}

const char *wan_monitor::link_state_to_string(ELinkState link_state)
{
    switch (link_state) {
    case ELinkState::eInvalid:
        return "invalid";
    case ELinkState::eUp:
        return "up";
    case ELinkState::eDown:
        return "down";
    }

    return "unknown";
}

bool wan_monitor::initialize(const std::vector<std::string> &wan_iface_names,
                             const std::string &bridge_iface_name)
{
    if (wan_iface_names.empty()) {
        LOG(ERROR) << "WAN Monitor initialized with empty interface list!";
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
    if ((m_iNetlinkFD = netlink_open_socket()) == -1)
        return false;

    m_bridge_ifindex = bridge_iface_name.empty() ? -1 : if_nametoindex(bridge_iface_name.c_str());
    if (m_bridge_ifindex <= 0) {
        LOG(WARNING) << "WAN Monitor initialized without a valid bridge interface index for '"
                     << bridge_iface_name << "'";
        m_bridge_ifindex = -1;
    }

    m_wan_iface_state.clear();
    m_wan_iface_bridge_membership.clear();
    auto bridge_ifaces = bridge_iface_name.empty()
                             ? std::vector<std::string>{}
                             : network_utils::linux_get_iface_list_from_bridge(bridge_iface_name);
    for (const auto &iface_name : wan_iface_names) {
        if (iface_name.empty()) {
            continue;
        }

        m_wan_iface_state[iface_name] = network_utils::linux_iface_is_up_and_running(iface_name)
                                            ? ELinkState::eUp
                                            : ELinkState::eDown;
        m_wan_iface_bridge_membership[iface_name] =
            std::find(bridge_ifaces.begin(), bridge_ifaces.end(), iface_name) !=
            bridge_ifaces.end();
    }

    return !m_wan_iface_state.empty();
}

bool wan_monitor::process(std::vector<LinkEvent> &events)
{
    events.clear();

    if (m_iNetlinkFD == -1) {
        LOG(ERROR) << "Invalid netlink socket!";
        return false;
    }

    struct sockaddr_nl addr = {0};
    struct iovec iov        = {m_arrNLBuff, sizeof m_arrNLBuff};

    struct msghdr msg  = {0};
    msg.msg_name       = (void *)&addr;
    msg.msg_namelen    = sizeof(addr);
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = nullptr;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // Read a message from the netlink socket
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

        // Completed reading - exit gracefully (as no error occurred,
        // but this is not a valid state)
        if (hnl->nlmsg_type == NLMSG_DONE)
            return true;

        // Error in the Message
        if (hnl->nlmsg_type == NLMSG_ERROR) {
            LOG(ERROR) << "NLMSG_ERROR - Invalid netlink message!";
            return false;
        }

        // LINK related message
        if (hnl->nlmsg_type == RTM_NEWLINK || hnl->nlmsg_type == RTM_DELLINK) {

            struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(hnl);

            // Convert the interface index to name
            auto iface_name = network_utils::linux_get_iface_name(ifi->ifi_index);

            // Skip events for other interfaces
            auto iface_it = m_wan_iface_state.find(iface_name);
            if (iface_it == m_wan_iface_state.end()) {
                LOG(DEBUG) << "Link detected for non-WAN interface '" << iface_name
                           << "'. Skipping...";

                continue;
            }

            LOG(DEBUG) << "Interface '" << iface_name << "', msg_type: " << int(hnl->nlmsg_type)
                       << ", running: " << int(ifi->ifi_flags & IFF_RUNNING);

            bool in_bridge  = false;
            bool saw_master = false;
            int attr_len    = IFLA_PAYLOAD(hnl);
            for (auto attr = IFLA_RTA(ifi); RTA_OK(attr, attr_len);
                 attr      = RTA_NEXT(attr, attr_len)) {
                if (attr->rta_type != IFLA_MASTER || RTA_PAYLOAD(attr) < int(sizeof(int))) {
                    continue;
                }

                saw_master         = true;
                int master_ifindex = 0;
                std::memcpy(&master_ifindex, RTA_DATA(attr), sizeof(master_ifindex));
                in_bridge = (m_bridge_ifindex > 0) && (master_ifindex == m_bridge_ifindex);
                break;
            }

            if (!saw_master) {
                auto bridge_it = m_wan_iface_bridge_membership.find(iface_name);
                in_bridge = (bridge_it != m_wan_iface_bridge_membership.end()) && bridge_it->second;
            }

            // Return WAN interface link state
            auto link_state = ((hnl->nlmsg_type == RTM_NEWLINK && ifi->ifi_flags & IFF_RUNNING))
                                  ? ELinkState::eUp
                                  : ELinkState::eDown;

            iface_it->second                          = link_state;
            m_wan_iface_bridge_membership[iface_name] = in_bridge;

            LOG(INFO) << "WAN link event: iface=" << iface_name
                      << " monitored_wan_ifaces=" << m_wan_iface_state.size()
                      << " nlmsg_type=" << int(hnl->nlmsg_type)
                      << " running=" << int(bool(ifi->ifi_flags & IFF_RUNNING))
                      << " in_bridge=" << in_bridge
                      << " interpreted_state=" << wan_monitor::link_state_to_string(link_state);

            LinkEvent event;
            event.iface_name = iface_name;
            event.link_state = link_state;
            event.in_bridge  = in_bridge;
            event.nlmsg_type = int(hnl->nlmsg_type);
            events.push_back(event);
        }
    }

    return true;
}

wan_monitor::ELinkState wan_monitor::get_link_state(const std::string &iface_name) const
{
    auto it = m_wan_iface_state.find(iface_name);
    if (it == m_wan_iface_state.end()) {
        return ELinkState::eInvalid;
    }

    return it->second;
}

bool wan_monitor::is_in_bridge(const std::string &iface_name) const
{
    auto it = m_wan_iface_bridge_membership.find(iface_name);
    if (it == m_wan_iface_bridge_membership.end()) {
        return false;
    }

    return it->second;
}

} // namespace beerocks
