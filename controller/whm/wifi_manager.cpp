/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "wifi_manager.h"

#include <bcl/beerocks_timer_factory_impl.h>
#include <bcl/beerocks_timer_manager_impl.h>
#include <beerocks/tlvf/beerocks_message_bml.h>
#include <bpl/bpl_cfg.h>

#include <regex>
#include <string>

using namespace beerocks;
using namespace wbapi;

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Implementation ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

namespace prplmesh {
namespace controller {
namespace whm {

namespace {
constexpr std::chrono::milliseconds CONFIG_RENEW_TIMEOUT{3000};
}

WifiManager::WifiManager(std::shared_ptr<beerocks::EventLoop> event_loop, son::db *ctx_wifi_db)
{

    m_event_loop  = event_loop;
    m_ctx_wifi_db = ctx_wifi_db;

    m_ambiorix_cl = std::make_shared<beerocks::wbapi::AmbiorixClient>();
    LOG_IF(!m_ambiorix_cl, FATAL) << "Unable to create ambiorix client object!";
    LOG_IF(!m_ambiorix_cl->connect(AMBIORIX_USP_BACKEND_PATH, AMBIORIX_PWHM_USP_BACKEND_URI), FATAL)
        << "Unable to connect to the ambiorix backend!";

    m_ambiorix_cl->init_event_loop(m_event_loop);
    m_ambiorix_cl->init_signal_loop(m_event_loop);

    auto timer_factory = std::make_shared<beerocks::TimerFactoryImpl>();
    LOG_IF(!timer_factory, FATAL) << "Unable to create timer factory!";
    if (timer_factory) {
        m_timer_manager = std::make_shared<beerocks::TimerManagerImpl>(timer_factory, event_loop);
        LOG_IF(!m_timer_manager, FATAL) << "Unable to create timer manager!";
    }
}

bool WifiManager::bss_info_config_change()
{
    if (!m_timer_manager) {
        send_ap_config_renew_msg();
    }

    if (m_timer != beerocks::net::FileDescriptor::invalid_descriptor) {
        m_timer_manager->remove_timer(m_timer);
    }
    m_timer =
        m_timer_manager->add_timer("ap_config_renew", CONFIG_RENEW_TIMEOUT, CONFIG_RENEW_TIMEOUT,
                                   [this](int /*fd*/, beerocks::EventLoop & /*loop*/) {
                                       m_timer_manager->remove_timer(m_timer);
                                       m_timer = beerocks::net::FileDescriptor::invalid_descriptor;
                                       send_ap_config_renew_msg();
                                       return true;
                                   });
    if (m_timer == beerocks::net::FileDescriptor::invalid_descriptor) {
        LOG(WARNING) << "Failed to create a timer for send_renew_msg(), calling it immediately.";
        send_ap_config_renew_msg();
        return false;
    }

    return true;
}

bool WifiManager::send_ap_config_renew_msg()
{
    m_ctx_wifi_db->clear_bss_info_configuration();
    m_ctx_wifi_db->clear_mld_info_configuration();

    std::list<son::wireless_utils::sBssInfoConf> wireless_settings;
    if (beerocks::bpl::bpl_cfg_get_wireless_settings(wireless_settings)) {
        for (const son::wireless_utils::sBssInfoConf &bss_config : wireless_settings) {
            m_ctx_wifi_db->add_bss_info_configuration(bss_config);
            if (!bss_config.mld_id.empty() &&
                bss_config.mld_id != std::to_string(DISABLED_MLDUNIT)) {
                son::wireless_utils::sMldInfoConf mld_config;
                if (!beerocks::bpl::bpl_cfg_get_mld_info_config(
                        bss_config.ssid, std::stoi(bss_config.mld_id), mld_config)) {
                    LOG(ERROR) << "Failed to read MLD configuartion from APMLD for mld id="
                               << bss_config.mld_id;
                    continue;
                }

                m_ctx_wifi_db->add_mld_info_configuration(mld_config, bss_config.mld_id);
            }
        }
    } else {
        LOG(WARNING) << "failed to read wireless settings";
        return false;
    }

    // Update wifi credentials
    uint8_t m_tx_buffer[beerocks::message::MESSAGE_BUFFER_LENGTH];
    ieee1905_1::CmduMessageTx cmdu_tx(m_tx_buffer, sizeof(m_tx_buffer));
    auto connected_agents = m_ctx_wifi_db->get_all_connected_agents();

    if (!connected_agents.empty()) {
        if (!son_actions::send_ap_config_renew_msg(cmdu_tx, *m_ctx_wifi_db)) {
            LOG(ERROR) << "Failed son_actions::send_ap_config_renew_msg!";
            return false;
        }
    }
    return true;
}

void WifiManager::subscribe_to_bss_info_config_change()
{
    auto event_handler        = std::make_shared<sAmbiorixEventHandler>();
    event_handler->event_type = AMX_CL_OBJECT_CHANGED_EVT;

    event_handler->callback_fn = [this](AmbiorixVariant &event_data) -> void {
        LOG(INFO) << "Detect change in wireless settings: propagate to agents";
        bss_info_config_change();
    };

    std::string filter;
    filter = "(path matches '" + wbapi_utils::search_path_radio() +
             "[0-9]+.$')"
             " && (notification == '" +
             AMX_CL_OBJECT_CHANGED_EVT +
             "')"
             " && (contains('parameters.OperatingClass') || contains('parameters.Channel')"
             " || contains('parameters.AP_Mode') || contains('parameters.MultiAPType'))";

    if (!m_ambiorix_cl->subscribe_to_object_event(wbapi_utils::search_path_radio(), event_handler,
                                                  filter)) {
        LOG(ERROR) << "Failed to subscribe to Device.WiFi.Radio changes";
    }

    filter = "(path matches '" + wbapi_utils::search_path_ssid() +
             "[0-9]+.$')"
             " && (notification == '" +
             AMX_CL_OBJECT_CHANGED_EVT +
             "')"
             " && (contains('parameters.SSID') || contains('parameters.MLDUnit'))";

    if (!m_ambiorix_cl->subscribe_to_object_event(wbapi_utils::search_path_ssid(), event_handler,
                                                  filter)) {
        LOG(ERROR) << "Failed to subscribe to Device.WiFi.SSID changes";
    }

    filter = "(path matches '" + wbapi_utils::search_path_ap() +
             "[0-9]+.Security.$')"
             " && (notification == '" +
             AMX_CL_OBJECT_CHANGED_EVT +
             "')"
             " && (contains('parameters.ModeEnabled') || contains('parameters.EncryptionMode')"
             " || contains('parameters.KeyPassPhrase'))";

    if (!m_ambiorix_cl->subscribe_to_object_event(wbapi_utils::search_path_ap(), event_handler,
                                                  filter)) {
        LOG(ERROR) << "Failed to subscribe to Device.WiFi.AccessPoint.Security changes";
    }

    // subscribe for VAPs enabling to re-trigger autoConf when new BSS is enabled
    // and potentially resume previously timeouted agent configuration
    filter = "(path matches '" + wbapi_utils::search_path_ap() +
             "[0-9]+.$')"
             " && (notification == '" +
             AMX_CL_OBJECT_CHANGED_EVT +
             "')"
             " && (contains('parameters.Enable'))";

    if (!m_ambiorix_cl->subscribe_to_object_event(wbapi_utils::search_path_ap(), event_handler,
                                                  filter)) {
        LOG(ERROR) << "Failed to subscribe to Device.WiFi.AccessPoint changes";
    }

    filter = "(path matches '" + wbapi_utils::search_path_apmld() +
             "[0-9]+.APMLDConfig.$')"
             " && (notification == '" +
             AMX_CL_OBJECT_CHANGED_EVT +
             "')"
             " && (contains('parameters.STREnabled') || contains('parameters.NSTREnabled') || "
             "contains('parameters.EMLSREnabled') || contains('parameters.EMLMREnabled'))";

    if (!m_ambiorix_cl->subscribe_to_object_event(wbapi_utils::search_path_apmld(), event_handler,
                                                  filter)) {
        LOG(ERROR) << "Failed to subscribe to Device.WiFi.APMLD.*.APMLDConfig changes";
    }
}

WifiManager::~WifiManager()
{
    m_ambiorix_cl->remove_event_loop(m_event_loop);
    m_ambiorix_cl->remove_signal_loop(m_event_loop);
}

} // namespace whm
} // namespace controller
} // namespace prplmesh
