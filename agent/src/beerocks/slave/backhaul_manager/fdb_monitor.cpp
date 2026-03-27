/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "fdb_monitor.h"

#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <easylogging++.h>

namespace beerocks {

#ifndef NDA_RTA
#define NDA_RTA(r) ((struct rtattr *)(((char *)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))
#endif

#ifndef NDA_PAYLOAD
#define NDA_PAYLOAD(n) NLMSG_PAYLOAD(n, sizeof(struct ndmsg))
#endif

fdb_monitor::~fdb_monitor() { stop(); }

bool fdb_monitor::initialize()
{
    stop();

    m_iNetlinkFD = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (m_iNetlinkFD < 0) {
        LOG(ERROR) << "Failed creating FDB netlink socket: " << strerror(errno);
        return false;
    }

    sockaddr_nl addr = {};
    addr.nl_family   = AF_NETLINK;
    addr.nl_groups   = RTMGRP_NEIGH;

    if (bind(m_iNetlinkFD, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        LOG(ERROR) << "Failed binding FDB netlink socket: " << strerror(errno);
        stop();
        return false;
    }

    return m_iNetlinkFD != -1;
}

void fdb_monitor::stop()
{
    if (m_iNetlinkFD != -1) {
        close(m_iNetlinkFD);
        m_iNetlinkFD = -1;
    }
}

fdb_monitor::EEvent fdb_monitor::process()
{
    if (m_iNetlinkFD == -1) {
        LOG(ERROR) << "Invalid FDB netlink socket";
        return EEvent::eInvalid;
    }

    char nl_buff[4096] = {};
    sockaddr_nl addr   = {};
    iovec iov          = {nl_buff, sizeof(nl_buff)};

    msghdr msg = {
        .msg_name    = &addr,
        .msg_namelen = sizeof(addr),
        .msg_iov     = &iov,
        .msg_iovlen  = 1,
    };

    auto len = recvmsg(m_iNetlinkFD, &msg, 0);
    if (len < 0) {
        LOG(ERROR) << "Failed reading FDB netlink message: " << strerror(errno);
        return EEvent::eInvalid;
    }

    if (msg.msg_flags & MSG_TRUNC) {
        LOG(WARNING) << "FDB netlink message truncated";
        return EEvent::eFdbChanged;
    }

    auto header = reinterpret_cast<nlmsghdr *>(nl_buff);
    for (; NLMSG_OK(header, len); header = NLMSG_NEXT(header, len)) {
        if (header->nlmsg_type == NLMSG_DONE) {
            break;
        }

        if (header->nlmsg_type == NLMSG_OVERRUN) {
            LOG(WARNING) << "FDB netlink message overrun";
            return EEvent::eFdbChanged;
        }

        if (header->nlmsg_type == NLMSG_ERROR) {
            LOG(ERROR) << "Received NLMSG_ERROR on FDB netlink socket";
            continue;
        }

        if (header->nlmsg_type != RTM_NEWNEIGH && header->nlmsg_type != RTM_DELNEIGH) {
            continue;
        }

        if (header->nlmsg_len < NLMSG_LENGTH(sizeof(ndmsg))) {
            continue;
        }

        auto neighbor = reinterpret_cast<ndmsg *>(NLMSG_DATA(header));
        if (neighbor->ndm_family != AF_BRIDGE) {
            continue;
        }

        bool has_lladdr = false;
        auto len        = NDA_PAYLOAD(header);
        for (auto attr = NDA_RTA(neighbor); RTA_OK(attr, len); attr = RTA_NEXT(attr, len)) {
            if (attr->rta_type == NDA_LLADDR) {
                has_lladdr = true;
                break;
            }
        }

        if (!has_lladdr) {
            continue;
        }

        return EEvent::eFdbChanged;
    }

    return EEvent::eInvalid;
}

} // namespace beerocks
