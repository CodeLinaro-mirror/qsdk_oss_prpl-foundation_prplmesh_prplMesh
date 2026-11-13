/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef BCL_NETWORK_INTERFACE_STATE_MONITOR_H_
#define BCL_NETWORK_INTERFACE_STATE_MONITOR_H_

#include <functional>
#include <string>

namespace beerocks {
namespace net {

class InterfaceStateMonitor {
public:
    /**
     * Network interface state-changed handler function.
     *
     * @param iface_name Interface name.
     * @param iface_state Interface state (true if it is up and running).
     */
    using StateChangedHandler =
        std::function<void(const std::string &iface_name, bool iface_state)>;

    /**
     * Events-lost handler function.
     */
    using EventsLostHandler = std::function<void()>;

    /**
     * @brief Class destructor
     */
    virtual ~InterfaceStateMonitor() = default;

    /**
     * @brief Sets the state-changed event handler function.
     *
     * Sets the callback function to handle network interface state changes.
     * Use nullptr to remove previously installed callback function.
     *
     * If a handler is set, it will be called back whenever any of the network interfaces changes
     * its state to or from the up-and-running value.
     *
     * @param handler State change handler function (or nullptr).
     */
    void set_handler(const StateChangedHandler &handler) { m_handler = handler; }

    /**
     * @brief Clears previously set state-changed event handler function.
     *
     * Clears callback function previously set.
     * Behaves like set_handler(nullptr)
     */
    void clear_handler() { m_handler = nullptr; }

    /**
     * @brief Sets the events-lost handler function.
     *
     * The handler is called back when state-changed events may have been lost (e.g. on a Netlink
     * socket receive buffer overrun). The network interface state must then be read again.
     * Use nullptr to remove previously installed callback function.
     *
     * @param handler Events-lost handler function (or nullptr).
     */
    void set_events_lost_handler(const EventsLostHandler &handler)
    {
        m_events_lost_handler = handler;
    }

protected:
    /**
     * @brief Notifies a network interface state-changed event.
     *
     * @param iface_name Name of the network interface that changed state.
     * @param iface_state New state of the network interface (true means up-and-running).
     */
    void notify_state_changed(const std::string &iface_name, bool iface_state) const
    {
        if (m_handler) {
            m_handler(iface_name, iface_state);
        }
    }

    /**
     * @brief Notifies that state-changed events may have been lost.
     */
    void notify_events_lost() const
    {
        if (m_events_lost_handler) {
            m_events_lost_handler();
        }
    }

private:
    /**
     * Network interface state-changed handler function that is called back whenever any network
     * interface changes its state.
     */
    StateChangedHandler m_handler;

    /**
     * Events-lost handler function.
     */
    EventsLostHandler m_events_lost_handler;
};

} // namespace net
} // namespace beerocks

#endif /* BCL_NETWORK_INTERFACE_STATE_MONITOR_H_ */
