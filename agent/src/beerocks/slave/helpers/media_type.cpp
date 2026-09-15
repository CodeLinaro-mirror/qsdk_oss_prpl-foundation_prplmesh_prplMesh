#include "media_type.h"

#include <bcl/beerocks_wifi_channel.h>
#include <bcl/network/network_utils.h>

#include "../agent_db.h"

// SPEED values
#include <linux/ethtool.h>

namespace beerocks {
ieee1905_1::eMediaType MediaType::get_802_11_media_type(const beerocks::AgentDB::sRadio &radio)
{
    if (radio.eht_supported) {
        return ieee1905_1::eMediaType::IEEE_802_11BE;
    } else if (radio.he_supported) {
        return ieee1905_1::eMediaType::IEEE_802_11AX;
    } else if (radio.vht_supported) {
        return ieee1905_1::eMediaType::IEEE_802_11AC_5_GHZ;
    } else if (radio.ht_supported) {
        if (radio.wifi_channel.get_freq_type() == eFreqType::FREQ_24G) {
            return ieee1905_1::eMediaType::IEEE_802_11N_2_4_GHZ;
        } else if (radio.wifi_channel.get_freq_type() == eFreqType::FREQ_5G) {
            return ieee1905_1::eMediaType::IEEE_802_11N_5_GHZ;
        }
    } else {
        if (radio.wifi_channel.get_freq_type() == eFreqType::FREQ_24G) {
            return ieee1905_1::eMediaType::IEEE_802_11G_2_4_GHZ;
        } else if (radio.wifi_channel.get_freq_type() == eFreqType::FREQ_5G) {
            return ieee1905_1::eMediaType::IEEE_802_11A_5_GHZ;
        }
    }

    return ieee1905_1::eMediaType::UNKNOWN_MEDIA;
}

bool MediaType::get_media_type(const std::string &interface_name,
                               ieee1905_1::eMediaTypeGroup media_type_group,
                               ieee1905_1::eMediaType &media_type)
{
    bool result = false;
    media_type  = ieee1905_1::eMediaType::UNKNOWN_MEDIA;

    if (ieee1905_1::eMediaTypeGroup::IEEE_802_3 == media_type_group) {
        uint32_t link_speed;
        uint32_t max_speed;
        bool is_full_duplex;
        if (net::network_utils::linux_iface_get_link_settings(interface_name, link_speed, max_speed,
                                                              is_full_duplex)) {
            if (SPEED_100 == max_speed) {
                media_type = ieee1905_1::eMediaType::IEEE_802_3U_FAST_ETHERNET;
            } else if (SPEED_1000 <= max_speed) {
                media_type = ieee1905_1::eMediaType::IEEE_802_3AB_GIGABIT_ETHERNET;
            } else {
                LOG(WARNING) << "Interface " << interface_name
                             << " reported max_speed=" << max_speed << " (link_speed=" << link_speed
                             << "), which doesn't map to a known 802.3 media type => UNKNOWN_MEDIA";
            }
        } else {
            LOG(WARNING) << "Failed getting link settings (ethtool) for interface "
                         << interface_name << " => UNKNOWN_MEDIA";
        }
        result = true;
    } else if (ieee1905_1::eMediaTypeGroup::IEEE_802_11 == media_type_group) {

        auto db = AgentDB::get();

        auto radio = db->radio(interface_name);
        if (radio) {
            media_type = get_802_11_media_type(*radio);
            result     = true;
            if (media_type == ieee1905_1::eMediaType::UNKNOWN_MEDIA) {
                LOG(WARNING) << "Interface " << interface_name << " radio capability flags "
                             << "(ht=" << radio->ht_supported << ", vht=" << radio->vht_supported
                             << ", he=" << radio->he_supported << ", eht=" << radio->eht_supported
                             << ") and freq_type=" << int(radio->wifi_channel.get_freq_type())
                             << " didn't resolve to a known 802.11 media type => UNKNOWN_MEDIA";
            }
        } else {
            LOG(WARNING) << "No radio found in AgentDB for interface " << interface_name
                         << " => UNKNOWN_MEDIA";
        }

    } else if (ieee1905_1::eMediaTypeGroup::IEEE_1901 == media_type_group) {
        // TODO: Not supported yet
        LOG(ERROR) << "IEEE_1901 media is not supported yet";
    } else if (ieee1905_1::eMediaTypeGroup::MoCA == media_type_group) {
        // TODO: Not supported yet
        LOG(ERROR) << "MoCA media is not supported yet";
    } else {
        result = true;
    }

    return result;
}
} // namespace beerocks
