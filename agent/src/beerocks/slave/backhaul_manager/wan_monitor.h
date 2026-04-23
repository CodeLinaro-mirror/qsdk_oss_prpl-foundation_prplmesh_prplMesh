/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef __WAN_MONITOR_H__
#define __WAN_MONITOR_H__

#include <string>
#include <unordered_map>
#include <vector>

namespace beerocks {

/*!
 * \brief Monitors the wired WAN port.
 */
class wan_monitor {

    /*
 * Public definitions
 */
public:
    enum class ELinkState {
        eInvalid, //!< Invalid
        eUp,      //!< Link detected on the wired WAN interface
        eDown     //!< Link NOT detected on the wired WAN interface
    };

    struct LinkEvent {
        std::string iface_name;
        ELinkState link_state = ELinkState::eInvalid;
        int nlmsg_type        = 0;
    };

public:
    wan_monitor();
    ~wan_monitor();

    static const char *link_state_to_string(ELinkState link_state);

    // Initialize the WAN monitor
    bool initialize(const std::vector<std::string> &wan_iface_names);

    // Process incoming netlink message
    bool process(std::vector<LinkEvent> &events);

    ELinkState get_link_state(const std::string &iface_name) const;

    int get_netlink_fd() const { return (m_iNetlinkFD); }

private:
    // WAN interface names and their last observed link state
    std::unordered_map<std::string, ELinkState> m_wan_iface_state;

    // Netlink socket file descriptor
    int m_iNetlinkFD;

    // Netlink receive buffer
    char m_arrNLBuff[4096];
};

} // namespace beerocks

#endif
