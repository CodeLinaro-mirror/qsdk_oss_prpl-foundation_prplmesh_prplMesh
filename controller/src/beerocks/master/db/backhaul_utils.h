/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef BACKHAUL_UTILS_H
#define BACKHAUL_UTILS_H

#include "agent.h"

#include <bcl/beerocks_utils.h>

namespace prplmesh {
namespace controller {
namespace db {

// A backhaul endpoint may identify an AP MLD rather than an individual BSS. Keep this lookup
// separate from get_agent_by_bssid(), whose callers expect a link-level BSSID.
inline std::shared_ptr<Agent>
resolve_wireless_backhaul_parent(const beerocks::mac_map<Agent> &agents,
                                 const sMacAddr &reporting_agent, const sMacAddr &upstream_address)
{
    if (upstream_address == beerocks::net::network_utils::ZERO_MAC) {
        return {};
    }

    for (const auto &entry : agents) {
        const auto &parent = entry.second;
        if (!parent || parent->al_mac == reporting_agent) {
            continue;
        }
        if (parent->ap_mlds.count(upstream_address)) {
            return parent;
        }
        for (const auto &radio : parent->radios) {
            if (radio.second && radio.second->bsses.get(upstream_address)) {
                return parent;
            }
        }
        for (const auto &ap_mld : parent->ap_mlds) {
            for (const auto &link : ap_mld.second.affiliated_aps) {
                if (link.second.bssid == upstream_address) {
                    return parent;
                }
            }
        }
    }
    return {};
}

// A logical MLD endpoint is insufficient to identify a selected link, even when its address
// equals an affiliated BSSID. Only an exact affiliated-STA match supplies that link information.
inline sMacAddr resolve_wireless_backhaul_connection_bssid(const Agent &agent,
                                                           size_t reported_wireless_interfaces)
{
    const auto &backhaul = agent.backhaul;
    const auto parent    = backhaul.parent_agent.lock();
    if (reported_wireless_interfaces != 1 ||
        !beerocks::utils::is_device_wireless(backhaul.backhaul_iface_type) || !parent ||
        backhaul.backhaul_interface == beerocks::net::network_utils::ZERO_MAC ||
        backhaul.parent_interface == beerocks::net::network_utils::ZERO_MAC) {
        return beerocks::net::network_utils::ZERO_MAC;
    }

    const auto mld_it = parent->ap_mlds.find(backhaul.parent_interface);
    if (mld_it != parent->ap_mlds.end()) {
        for (const auto &station : mld_it->second.sta_mlds) {
            if (!station.second || station.second->mac == backhaul.backhaul_interface) {
                continue;
            }
            const auto link = station.second->affiliated_stas.get(backhaul.backhaul_interface);
            if (link) {
                return link->bssid;
            }
        }
        return beerocks::net::network_utils::ZERO_MAC;
    }

    for (const auto &radio : parent->radios) {
        if (radio.second && radio.second->bsses.get(backhaul.parent_interface)) {
            return backhaul.parent_interface;
        }
    }
    for (const auto &ap_mld : parent->ap_mlds) {
        for (const auto &link : ap_mld.second.affiliated_aps) {
            if (link.second.bssid == backhaul.parent_interface) {
                return backhaul.parent_interface;
            }
        }
    }
    return beerocks::net::network_utils::ZERO_MAC;
}

// A one-sided Ethernet neighbor report may identify the parent without identifying its port.
// Reuse the previous port only for the same Ethernet parent; a wireless BSSID is not a LAN port.
inline sMacAddr resolve_ethernet_parent_interface(const Agent::sBackhaul &previous_backhaul,
                                                  const sMacAddr &parent_al_mac,
                                                  const sMacAddr &reported_parent_interface)
{
    if (reported_parent_interface != beerocks::net::network_utils::ZERO_MAC) {
        return reported_parent_interface;
    }
    const auto previous_parent = previous_backhaul.parent_agent.lock();
    if (previous_parent && previous_backhaul.backhaul_iface_type == beerocks::IFACE_TYPE_ETHERNET &&
        previous_parent->al_mac == parent_al_mac) {
        return previous_backhaul.parent_interface;
    }
    return beerocks::net::network_utils::ZERO_MAC;
}

// Resolve only a local radio. Missing capability data must not erase a known mapping for the
// same wireless connection, and an old pointer must not keep a removed radio in use.
inline std::shared_ptr<Agent::sRadio>
resolve_wireless_backhaul_radio(const Agent &agent, const Agent::sBackhaul &previous_backhaul)
{
    const auto &backhaul = agent.backhaul;
    if (!beerocks::utils::is_device_wireless(backhaul.backhaul_iface_type) ||
        backhaul.backhaul_interface == beerocks::net::network_utils::ZERO_MAC) {
        return {};
    }

    for (const auto &radio : agent.radios) {
        if (radio.second && radio.second->backhaul_station_mac == backhaul.backhaul_interface) {
            return radio.second;
        }
    }

    // The Backhaul STA MLD Configuration TLV also maps each affiliated bSTA MAC to its RUID.
    // Do not select an arbitrary affiliated radio when the reported MAC identifies the MLD.
    for (const auto &affiliated_bsta : agent.bsta_mld.affiliated_bstas) {
        if (affiliated_bsta.second.bssid == backhaul.backhaul_interface) {
            return agent.radios.get(affiliated_bsta.first);
        }
    }

    const auto parent = backhaul.parent_agent.lock();
    if (parent && parent->ap_mlds.count(backhaul.parent_interface) &&
        backhaul.connection_bssid == beerocks::net::network_utils::ZERO_MAC) {
        // An unchanged logical MLD endpoint does not prove that the selected radio is unchanged.
        return {};
    }

    const auto &previous_radio = previous_backhaul.wireless_backhaul_radio;
    if (previous_radio &&
        beerocks::utils::is_device_wireless(previous_backhaul.backhaul_iface_type) &&
        backhaul.parent_interface != beerocks::net::network_utils::ZERO_MAC &&
        previous_backhaul.backhaul_interface == backhaul.backhaul_interface &&
        previous_backhaul.parent_interface == backhaul.parent_interface) {
        return agent.radios.get(previous_radio->radio_uid);
    }

    return {};
}

} // namespace db
} // namespace controller
} // namespace prplmesh

#endif // BACKHAUL_UTILS_H
