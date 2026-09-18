/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "afc_spectrum_helper.h"

#include <bcl/beerocks_string_utils.h>
#include <easylogging++.h>

#ifdef USE_PRPLMESH_WHM
#include <ambiorix_client.h>
#include <wbapi_utils.h>
#endif

namespace beerocks {
namespace afc_spectrum_helper {

#ifdef USE_PRPLMESH_WHM
namespace {
bool connect_ambiorix(wbapi::AmbiorixClient &ambiorix_cl)
{
    if (!ambiorix_cl.connect()) {
        LOG(ERROR) << "Failed to connect ambiorix client for AFC stats read";
        return false;
    }
    return true;
}
} // namespace
#endif

bool read_available_spectrum_inquiry_data(std::string &request, std::string &response)
{
    request.clear();
    response.clear();

#ifdef USE_PRPLMESH_WHM
    wbapi::AmbiorixClient ambiorix_cl;
    if (!connect_ambiorix(ambiorix_cl)) {
        return false;
    }

    if (!ambiorix_cl.get_param(request, AFC_STATS_PATH, AFC_REQUEST_PARAM)) {
        LOG(ERROR) << "Failed to read " << AFC_STATS_PATH << AFC_REQUEST_PARAM;
        return false;
    }

    if (!ambiorix_cl.get_param(response, AFC_STATS_PATH, AFC_RESPONSE_PARAM)) {
        LOG(ERROR) << "Failed to read " << AFC_STATS_PATH << AFC_RESPONSE_PARAM;
        return false;
    }

    return true;
#else
    LOG(ERROR) << "AFC spectrum inquiry read requires USE_PRPLMESH_WHM";
    return false;
#endif
}

bool is_afc_grant_successful()
{
#ifdef USE_PRPLMESH_WHM
    wbapi::AmbiorixClient ambiorix_cl;
    if (!connect_ambiorix(ambiorix_cl)) {
        return false;
    }

    std::string grant_status;
    if (!ambiorix_cl.get_param(grant_status, AFC_STATS_PATH, AFC_GRANT_STATUS_PARAM)) {
        LOG(ERROR) << "Failed to read " << AFC_STATS_PATH << AFC_GRANT_STATUS_PARAM;
        return false;
    }

    if (grant_status != "Success") {
        LOG(INFO) << "AFC grant status is not Success: " << grant_status;
        return false;
    }

    return true;
#else
    return false;
#endif
}

bool is_standard_power_mode(const std::string &radio_iface_name)
{
#ifdef USE_PRPLMESH_WHM
    if (radio_iface_name.empty()) {
        return false;
    }

    wbapi::AmbiorixClient ambiorix_cl;
    if (!connect_ambiorix(ambiorix_cl)) {
        return false;
    }

    const auto radio_path = wbapi::wbapi_utils::search_path_radio_by_iface(radio_iface_name);
    std::string power_type;
    if (!ambiorix_cl.get_param(power_type, radio_path, RADIO_POWER_TYPE_PARAM)) {
        LOG(DEBUG) << "Failed to read PowerType for radio " << radio_iface_name;
        return false;
    }

    LOG(DEBUG) << "Radio " << radio_iface_name << " PowerType=" << power_type;
    return power_type == "StandardPower";
#else
    (void)radio_iface_name;
    return false;
#endif
}

bool read_possible_channels(const std::string &radio_iface_name,
                            std::unordered_set<uint8_t> &channels)
{
    channels.clear();

#ifdef USE_PRPLMESH_WHM
    if (radio_iface_name.empty()) {
        return false;
    }

    wbapi::AmbiorixClient ambiorix_cl;
    if (!connect_ambiorix(ambiorix_cl)) {
        return false;
    }

    const auto radio_path = wbapi::wbapi_utils::search_path_radio_by_iface(radio_iface_name);
    std::string possible_channels_str;
    if (!ambiorix_cl.get_param(possible_channels_str, radio_path, RADIO_POSSIBLE_CHANNELS_PARAM)) {
        LOG(DEBUG) << "Failed to read PossibleChannels for radio " << radio_iface_name;
        return false;
    }

    for (const auto &chan_str : beerocks::string_utils::str_split(possible_channels_str, ',')) {
        if (chan_str.empty()) {
            continue;
        }
        channels.insert(static_cast<uint8_t>(beerocks::string_utils::stoi(chan_str)));
    }

    return !channels.empty();
#else
    (void)radio_iface_name;
    return false;
#endif
}

bool is_afc_regulatory_preference_applicable(const std::string &radio_iface_name)
{
    if (radio_iface_name.empty()) {
        return false;
    }

#ifdef USE_PRPLMESH_WHM
    // Single Ambiorix session: avoid three separate connect()/get_param() round-trips.
    wbapi::AmbiorixClient ambiorix_cl;
    if (!connect_ambiorix(ambiorix_cl)) {
        return false;
    }

    const auto radio_path = wbapi::wbapi_utils::search_path_radio_by_iface(radio_iface_name);
    std::string power_type;
    if (!ambiorix_cl.get_param(power_type, radio_path, RADIO_POWER_TYPE_PARAM) ||
        power_type != "StandardPower") {
        LOG(DEBUG) << "AFC regulatory channel preferences not applicable for " << radio_iface_name
                   << ": PowerType is not StandardPower";
        return false;
    }

    std::string grant_status;
    if (!ambiorix_cl.get_param(grant_status, AFC_STATS_PATH, AFC_GRANT_STATUS_PARAM) ||
        grant_status != "Success") {
        LOG(DEBUG) << "AFC regulatory channel preferences not applicable for " << radio_iface_name
                   << ": AFC grant is not successful";
        return false;
    }

    std::string request;
    std::string response;
    if (!ambiorix_cl.get_param(request, AFC_STATS_PATH, AFC_REQUEST_PARAM) ||
        !ambiorix_cl.get_param(response, AFC_STATS_PATH, AFC_RESPONSE_PARAM) || request.empty() ||
        response.empty()) {
        LOG(DEBUG) << "AFC regulatory channel preferences not applicable for " << radio_iface_name
                   << ": inquiry payloads missing in DM";
        return false;
    }

    return true;
#else
    (void)radio_iface_name;
    return false;
#endif
}

} // namespace afc_spectrum_helper
} // namespace beerocks
