/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _HIGHER_LAYER_COLLECTION_TASK_H_
#define _HIGHER_LAYER_COLLECTION_TASK_H_

#include "task.h"

#include <tlvf/CmduMessageTx.h>
#include <tlvf/ieee_1905_1/eIpv4AddressType.h>
#include <tlvf/ieee_1905_1/eIpv6AddressType.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace beerocks {

/**
 * @brief Task responsible for responding to IEEE 1905.1 Higher Layer Queries.
 *
 * This task collects IPv4 and IPv6 address information across all available
 * network interfaces and encapsulates it into 1905.1 Response messages.
 */
class HigherLayerCollectionTask : public Task {
public:
    /**
     * @brief Structure holding network information for a specific interface.
     */
    struct sInterfaceNetworkStatus {
        sMacAddr mac_address;             ///< MAC address of the interface.
        uint8_t ipv6_link_local[16] = {}; ///< Link-local IPv6 address (mandatory for 1905.1).

        /**
         * @brief Represents a single IPv4 address entry.
         */
        struct sIpv4Entry {
            ieee1905_1::eIpv4AddressType ipv4_address_type; ///< STATIC or DHCP.
            uint32_t ipv4_address;                          ///< IPv4 address in host byte order.
            uint32_t ipv4_dhcp_server; ///< DHCP server address in host byte order.
        };
        std::vector<sIpv4Entry> ipv4_list; ///< List of IPv4 addresses on this interface.

        /**
         * @brief Represents a single IPv6 address entry.
         */
        struct sIpv6Entry {
            ieee1905_1::eIpv6AddressType ipv6_address_type; ///< STATIC, DHCP, or AUTO.
            uint8_t ipv6_address[16];                       ///< 128-bit IPv6 address.
            uint8_t ipv6_origin[16]; ///< Originator of the address (e.g., prefix advertiser).
        };
        std::vector<sIpv6Entry> ipv6_list; ///< List of IPv6 addresses on this interface.
    };

    /**
     * @brief Abstract interface for providing network status updates.
     *
     * Decouples the task logic from the specific OS mechanism (like getifaddrs or ioctl).
     */
    class InterfaceStatusProvider {
    public:
        virtual ~InterfaceStatusProvider() = default;

        /**
         * @brief Refreshes the internal map of network interfaces and their statuses.
         * @param[out] interfaces_network_status Map to be populated.
         * @return true if refresh was successful, false otherwise.
         */
        virtual bool refresh(std::unordered_map<std::string, sInterfaceNetworkStatus>
                                 &interfaces_network_status) = 0;
    };

    /** @brief Callback type for sending CMDU responses to a specific destination MAC. */
    using send_cmdu_to_mac_f = std::function<bool(const sMacAddr &, ieee1905_1::CmduMessageTx &)>;

    /** @brief Callback type for retrieving the Bridge (AL) MAC address. */
    using get_bridge_mac_f = std::function<sMacAddr()>;

    /**
     * @brief Constructor for HigherLayerCollectionTask.
     * @param send_cmdu_to_mac Callback to send responses.
     * @param get_bridge_mac Callback to get AL MAC.
     * @param interface_status_provider Data source for network info.
     * @param cmdu_tx Reference to the transmitter buffer.
     */
    HigherLayerCollectionTask(send_cmdu_to_mac_f send_cmdu_to_mac, get_bridge_mac_f get_bridge_mac,
                              std::unique_ptr<InterfaceStatusProvider> interface_status_provider,
                              ieee1905_1::CmduMessageTx &cmdu_tx);

    virtual ~HigherLayerCollectionTask() {}

    /**
     * @brief Handles incoming CMDU messages from the controller or other nodes.
     */
    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header);

    /**
     * @brief Specifically handles the IEEE 1905.1 Higher Layer Query.
     * @param src_mac MAC address of the query sender.
     * @param cmdu_rx The received query message.
     * @return true if response was successfully sent.
     */
    bool handle_higher_layer_query(const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx);

private:
    ieee1905_1::CmduMessageTx &m_cmdu_tx;                                 ///< Transmitter buffer.
    send_cmdu_to_mac_f m_send_cmdu_to_mac;                                ///< Send callback.
    get_bridge_mac_f m_get_bridge_mac;                                    ///< Bridge MAC getter.
    std::unique_ptr<InterfaceStatusProvider> m_interface_status_provider; ///< Data source.
    std::unordered_map<std::string, sInterfaceNetworkStatus>
        m_interfaces_network_status; ///< Internal cache.
};

} // namespace beerocks

#endif // _HIGHER_LAYER_COLLECTION_TASK_H_
