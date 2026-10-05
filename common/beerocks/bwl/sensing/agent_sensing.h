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
    bool AddExchange();
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
};

} // namespace bwl

#endif
