/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#ifndef __BWL_AGENT_SENSING_H__
#define __BWL_AGENT_SENSING_H__

//#include <bwl/nl80211_client.h>

// Ambiorix
#include "ambiorix_connection.h"
#include "ambiorix_client.h"
#include "ambiorix_event.h"
#include <bcl/beerocks_event_loop.h>

#include <memory>
#include <functional>
#include <string>

namespace bwl { 

/**
 * @Class implements NL80211 client methods, using wbapi shared connection for requests
 */
class agent_sensing {

public:
    /**
     * @brief Class constructor.
     */
    agent_sensing();
    bool setup_datapath();
    bool RemoveLayer3Path();
    bool AddExchange();
    bool RemoveExchange();
    bool subscribe_to_exchange_terminated();
    bool init_ambiorix_event_loop(std::shared_ptr<beerocks::EventLoop> event_loop);
    using exchange_terminated_cb =
    std::function<void(uint32_t exchange_id, const std::string &cause)>;
    void set_exchange_terminated_callback(exchange_terminated_cb cb)
    {
        m_exchange_terminated_cb = std::move(cb);
    }
    /**
     * @brief Class destructor.
     */
    virtual ~agent_sensing() = default;

protected:
    beerocks::wbapi::AmbiorixClient m_ambiorix_sensing_cl;
private:
    beerocks::wbapi::AmbiorixConnectionSmartPtr m_connection;
   // beerocks::wbapi::AmbiorixClient m_ambiorix_sensing_cl;
    std::string m_sensing_path;  // Path for WiFi.Sensing (set in agent_sensing.cpp)
    std::shared_ptr<beerocks::wbapi::sAmbiorixEventHandler> m_exchange_term_handler; 
    exchange_terminated_cb m_exchange_terminated_cb;
};

} // namespace bwl

#endif
