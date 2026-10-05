/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_sensing.h"
#include "agent_db.h"
#include <bcl/beerocks_utils.h>
//#include <bcl/network/network_utils.h>

// Ambiorix
#include "ambiorix_connection_manager.h"
#include "wbapi_utils.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <tlvf/tlvftypes.h>

using namespace beerocks;
using namespace wbapi;

namespace bwl {
namespace {

/** Drop IPv6 zone / scope ID (e.g. %br-lan) so the address is stored without the interface tag. */
void strip_ipv6_zone_id(std::string &ip)
{
    const auto pct = ip.find('%');
    if (pct != std::string::npos) {
        ip.erase(pct);
    }
}

} // namespace

agent_sensing::agent_sensing()
    : m_connection(AmbiorixConnectionManager::get_instance()->get_connection(
          AMBIORIX_WBAPI_BACKEND_PATH, AMBIORIX_WBAPI_BUS_URI))
{
    // Native DM path on wifi-sensing USP socket (see wsn_main.c / wifi-sensing_definition.odl).
    // Device.WiFi.Sensing. only works if wifi-sensing loads wifi-sensing_usp.odl (USP translate).
    m_sensing_path = "Device.WiFi.X_PRPLWARE-COM_WiFiSensing";
    if (!m_ambiorix_sensing_cl.connect(AMBIORIX_WBAPI_BACKEND_PATH, AMBIORIX_WBAPI_BUS_URI)) {
        LOG(ERROR) << "agent_sensing: failed to connect AmbiorixClient to sensing USP";
    }

    LOG(INFO) << "agent_sensing: connected AmbiorixClient sensing_path=" << m_sensing_path;
    if (!subscribe_to_exchange_terminated()) {
        LOG(ERROR) << "agent_sensing: subscribe_to_exchange_terminated failed";
    } else {
        LOG(INFO) << "agent_sensing: ExchangeTerminated! subscription active";
    }
}

bool agent_sensing::setup_datapath()
{
    if (m_sensing_path.empty()) {
        LOG(ERROR) << "setup_datapath: no sensing path";
        return false;
    }
    auto db = beerocks::AgentDB::get();
    if (db->data_path_entries.empty()) {
       LOG(ERROR) << "setup_datapath: no datapath entry in AgentDB";
       return false;
    }
    const auto &entry = db->data_path_entries.back();
    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    // Use AgentDB values when available; AgentDB has no dest_ip/dest_port/add_path so use placeholders or add to AgentDB
    //std::string dest_ip   = "0.0.0.0";
    //std::string dest_port = "0";
    //int add_remove       = 1;
    // Must match wifi-sensing ODL: setupDatapath(string DestIP, uint16 DestPort, uint8 AddPath)
    args.add_child("DestIP", entry.dest_ip);
    args.add_child("DestPort", entry.dest_port);
    args.add_child("AddPath", static_cast<uint8_t>(entry.add_path)); 
    if (!m_ambiorix_sensing_cl.call(m_sensing_path, "setupDatapath", args, result)) {
        LOG(ERROR) << "setup_datapath: call failed for dest=" << entry.dest_ip << ":"
                   << entry.dest_port << " AddPath=" << int(entry.add_path) << " path=" << m_sensing_path;
        return false;
    }

    // wifi-sensing _setupDatapath fills retval htable: srcIp (string), srcPort (uint16).
    // USP amxb backend wraps that htable as the first element of a list (see amxb_usp_convert_out_args);
    // ubus/local backends typically return the htable at the top level.
    const AmbiorixVariant *ret_htable = &result;
    AmbiorixVariantSmartPtr list_head;
    if (result.get_type() == AMXC_VAR_ID_LIST) {
        list_head = result.find_child(0U);
        if (list_head && list_head->get_type() == AMXC_VAR_ID_HTABLE) {
            ret_htable = list_head.get();
        }
    }

    std::string src_ip;
    uint16_t src_port = 0;
    if (!ret_htable->read_child(src_ip, "srcIp") || !ret_htable->read_child(src_port, "srcPort")) {
        LOG(ERROR) << "setup_datapath: result missing srcIp/srcPort (result type=" << result.get_type()
                   << ")";
        return false;
    }
    if (src_port == 0) {
        LOG(ERROR) << "setup_datapath: sensing reported error (srcPort=0) srcIp=" << src_ip;
        return false;
    }

    strip_ipv6_zone_id(src_ip);

    auto &path_entry        = db->data_path_entries.back();
    path_entry.source_addr  = std::move(src_ip);
    path_entry.source_port  = src_port;

    LOG(DEBUG) << "setup_datapath: srcIp=" << path_entry.source_addr
               << " srcPort=" << path_entry.source_port;

    LOG(ERROR) << "GAYATHRI:setup_datapath: call success for dest=" << entry.dest_ip << ":"
                   << entry.dest_port << " AddPath=" << int(entry.add_path) << " path=" << m_sensing_path;

    return true;
}

bool agent_sensing::RemoveLayer3Path()
{
    if (m_sensing_path.empty()) {
        LOG(ERROR) << "RemoveLayer3Path: no sensing path";
        return false;
    }
    auto db = beerocks::AgentDB::get();
    if (db->data_path_entries.empty()) {
        LOG(ERROR) << "RemoveLayer3Path: no datapath entry in AgentDB";
        return false;
    }
    const auto &entry = db->data_path_entries.back();
    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    // Must match wifi-sensing ODL:
    // void RemoveLayer3Path(string DestinationAddress, uint16 DestinationPort);
    args.add_child("DestinationAddress", entry.dest_ip);
    args.add_child("DestinationPort", entry.dest_port);
    if (!m_ambiorix_sensing_cl.call(m_sensing_path, "RemoveLayer3Path", args, result)) {
        LOG(ERROR) << "RemoveLayer3Path: call failed for dest=" << entry.dest_ip << ":"
                   << entry.dest_port << " path=" << m_sensing_path;
        return false;
    }

    LOG(DEBUG) << "RemoveLayer3Path: call success for dest=" << entry.dest_ip << ":"
               << entry.dest_port << " path=" << m_sensing_path;
    return true;
}

bool agent_sensing::AddExchange()
{
    if (m_sensing_path.empty()) {
        LOG(ERROR) << "AddExchange: no sensing path";
        return false;
    }
    auto db = beerocks::AgentDB::get();
    if (db->sensing_exchange_entries.empty()) {
        LOG(ERROR) << "AddExchange: no sensing exchange entry in AgentDB";
        return false;
    }
    const auto &entry = db->sensing_exchange_entries.back();
    AmbiorixVariant create_session_args(AMXC_VAR_ID_HTABLE);
    AmbiorixVariant create_session_result;
    if (!m_ambiorix_sensing_cl.call(m_sensing_path, "CreateSession", create_session_args,
                                    create_session_result)) {
        LOG(ERROR) << "AddExchange: CreateSession failed path=" << m_sensing_path;
        return false;
    }

    AmbiorixVariant result;
    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    const auto tx_mac = entry.tx_mac_valid ? tlvf::mac_to_string(entry.tx_mac) : std::string{};
    const auto rx_mac = entry.rx_mac_valid ? tlvf::mac_to_string(entry.rx_mac) : std::string{};

    char data_type_str[16];
    snprintf(data_type_str, sizeof(data_type_str), "%08X", entry.data_type);

    // Must match wifi-sensing ODL: AddExchange(uint8 Rate, uint16 Bandwidth, uint16 NTx, uint16 NRx,
  // string ExchangeType, string DataType, string Transmitter, string Receiver, uint32 ExchangeID)
    args.add_child("Rate", static_cast<uint8_t>(entry.rate_tu10));
    args.add_child("Bandwidth", entry.bandwidth_mhz);
    args.add_child("NTx", entry.ntx);
    args.add_child("NRx", entry.nrx);
    args.add_child("ExchangeType", entry.exchange_type_str);
    args.add_child("DataType", std::string(data_type_str));
    args.add_child("Transmitter", tx_mac);
    if (entry.rx_mac_valid && !rx_mac.empty()) {
        args.add_child("Receiver", rx_mac);
    }
    args.add_child("ExchangeID", entry.exchange_id);
    const std::string add_exchange_path = m_sensing_path + ".Session.1";
    if (!m_ambiorix_sensing_cl.call(add_exchange_path, "AddExchange", args, result)) {
        LOG(ERROR) << "AddExchange: call failed for exchange_id=" << entry.exchange_id
                   << " path=" << m_sensing_path;
        return false;
    }

    // AddExchange retval: ExchangeID (uint32), ErrorCode (string); same list-wrap as setup_datapath.
    const AmbiorixVariant *ret_htable = &result;
    AmbiorixVariantSmartPtr list_head;
    if (result.get_type() == AMXC_VAR_ID_LIST) {
        list_head = result.find_child(0U);
        if (list_head && list_head->get_type() == AMXC_VAR_ID_HTABLE) {
            ret_htable = list_head.get();
        }
    }

    uint32_t result_exchange_id = entry.exchange_id;
    uint32_t tmp_exchange_id    = 0;
    if (ret_htable->read_child(tmp_exchange_id, "ExchangeID") ||
        ret_htable->read_child(tmp_exchange_id, "exchange_id")) {
        result_exchange_id = tmp_exchange_id;
    }

    std::string result_error_code;
    if (!ret_htable->read_child(result_error_code, "ErrorCode")) {
        ret_htable->read_child(result_error_code, "error_code");
    }

    bool updated_db = false;
    for (auto &list_entry : db->sensing_exchange_entries) {
        if (list_entry.exchange_id == result_exchange_id) {
            list_entry.error_code = std::move(result_error_code);
            updated_db            = true;
            LOG(DEBUG) << "AddExchange: updated AgentDB exchange_id=" << result_exchange_id
                       << " error_code=" << list_entry.error_code;
            break;
        }
    }
    if (!updated_db) {
        LOG(WARNING) << "AddExchange: no AgentDB sensing_exchange_entries match exchange_id="
                     << result_exchange_id;
    }

    LOG(DEBUG) << "AddExchange: call success for exchange_id=" << entry.exchange_id
               << " path=" << m_sensing_path;
    return true;

}

bool agent_sensing::RemoveExchange()
{
    if (m_sensing_path.empty()) {
        LOG(ERROR) << "RemoveExchange: no sensing path";
        return false;
    }

    auto db = beerocks::AgentDB::get();
    if (db->sensing_exchange_entries.empty()) {
        LOG(ERROR) << "RemoveExchange: no sensing exchange entry in AgentDB";
        return false;
    }

    const auto &entry = db->sensing_exchange_entries.back();
    if (entry.exchange_id == 0) {
        LOG(ERROR) << "RemoveExchange: invalid ExchangeID=0";
        return false;
    }

    /* wifi-sensing: RemoveExchange() is on Session.{i}.Exchange.{j} (no args). */
    const std::string search_path = m_sensing_path + ".Session.1.Exchange.[ExchangeID == " +
                                    std::to_string(entry.exchange_id) + "].";
    std::string exchange_path;
    if (!m_ambiorix_sensing_cl.resolve_path(search_path, exchange_path) || exchange_path.empty()) {
        LOG(ERROR) << "RemoveExchange: no Exchange instance for exchange_id=" << entry.exchange_id
                   << " search=" << search_path;
        return false;
    }

    AmbiorixVariant args(AMXC_VAR_ID_HTABLE);
    AmbiorixVariant result;
    if (!m_ambiorix_sensing_cl.call(exchange_path, "RemoveExchange", args, result)) {
        LOG(ERROR) << "RemoveExchange: call failed for exchange_id=" << entry.exchange_id
                   << " path=" << exchange_path;
        return false;
    }

    bool updated_db = false;
    for (auto &list_entry : db->sensing_exchange_entries) {
        if (list_entry.exchange_id == entry.exchange_id) {
            /* Empty ErrorCode maps to STATUS_SUCCESS in sensing_exchange_task. */
            list_entry.error_code.clear();
            updated_db = true;
            LOG(DEBUG) << "RemoveExchange: updated AgentDB exchange_id=" << entry.exchange_id;
            break;
        }
    }
    if (!updated_db) {
        LOG(WARNING) << "RemoveExchange: no AgentDB sensing_exchange_entries match exchange_id="
                     << entry.exchange_id;
    }

    LOG(DEBUG) << "RemoveExchange: call success for exchange_id=" << entry.exchange_id
               << " path=" << exchange_path;
    return true;
}

bool agent_sensing::subscribe_to_exchange_terminated()
{
    LOG(INFO) << "subscribe_to_exchange_terminated: enter";

    if (m_sensing_path.empty()) {
        LOG(ERROR) << "subscribe_to_exchange_terminated: no sensing path";
        LOG(INFO) << "subscribe_to_exchange_terminated: exit (false)";
        return false;
    }

    m_exchange_term_handler             = std::make_shared<sAmbiorixEventHandler>();
    m_exchange_term_handler->event_type = "ExchangeTerminated!";
    m_exchange_term_handler->callback_fn =
        [this](AmbiorixVariant &event_data) {
            std::string notification;
            std::string path;
            event_data.read_child(notification, "notification");
            event_data.read_child(path, "path");

            LOG(INFO) << "ExchangeTerminated!: event received in bwl"
                      << " notification=" << notification << " path=" << path;

            std::string exchange_id_str;
            std::string cause;
            if (!event_data.read_child(exchange_id_str, "ExchangeID")) {
                LOG(WARNING) << "ExchangeTerminated!: missing ExchangeID, drop event"
                             << " path=" << path;
                return;
            }
            if (!event_data.read_child(cause, "Cause")) {
                LOG(WARNING) << "ExchangeTerminated!: Cause missing, using empty"
                             << " ExchangeID=" << exchange_id_str;
            }

            const char *p   = exchange_id_str.c_str();
            char *endptr    = nullptr;
            unsigned long v = std::strtoul(p, &endptr, 10);
            if (endptr == p || *endptr != '\0' || v > 0xffffffffUL) {
                LOG(ERROR) << "ExchangeTerminated!: bad ExchangeID=" << exchange_id_str
                           << " path=" << path;
                return;
            }
            const uint32_t exchange_id = static_cast<uint32_t>(v);

            LOG(INFO) << "ExchangeTerminated!: parsed ExchangeID=" << exchange_id
                      << " Cause=" << cause << " path=" << path;

            if (m_exchange_terminated_cb) {
                LOG(INFO) << "ExchangeTerminated!: forwarding to agent callback"
                          << " ExchangeID=" << exchange_id << " Cause=" << cause;
                m_exchange_terminated_cb(exchange_id, cause);
                LOG(INFO) << "ExchangeTerminated!: agent callback returned"
                          << " ExchangeID=" << exchange_id;
            } else {
                LOG(WARNING) << "ExchangeTerminated!: no callback registered,"
                             << " event not forwarded ExchangeID=" << exchange_id;
            }
        };

    // Subscribe on Session.* — event is defined on Exchange objects
    const std::string object_path = m_sensing_path + ".Session.";
    const std::string filter =
        "(path matches '" + m_sensing_path +
        ".Session.[0-9]+.Exchange.[0-9]+.$')"
        " && (notification == 'ExchangeTerminated!')";

    LOG(INFO) << "subscribe_to_exchange_terminated: path=" << object_path
              << " filter=" << filter;

    if (!m_ambiorix_sensing_cl.subscribe_to_object_event(object_path,
                                                         m_exchange_term_handler, filter)) {
        LOG(ERROR) << "subscribe_to_exchange_terminated: subscribe failed on "
                   << object_path;
        LOG(INFO) << "subscribe_to_exchange_terminated: exit (false)";
        return false;
    }

    LOG(INFO) << "subscribe_to_exchange_terminated: subscribed successfully";
    LOG(INFO) << "subscribe_to_exchange_terminated: exit (true)";
    return true;
}

bool agent_sensing::init_ambiorix_event_loop(std::shared_ptr<beerocks::EventLoop> event_loop)
{
    LOG(INFO) << "init_ambiorix_event_loop: enter";

    if (!event_loop) {
        LOG(ERROR) << "init_ambiorix_event_loop: null event_loop";
        LOG(INFO) << "init_ambiorix_event_loop: exit (false)";
        return false;
    }
    if (!m_ambiorix_sensing_cl.init_event_loop(event_loop)) {
        LOG(ERROR) << "init_ambiorix_event_loop: init_event_loop failed";
        LOG(INFO) << "init_ambiorix_event_loop: exit (false)";
        return false;
    }
    LOG(INFO) << "init_ambiorix_event_loop: event loop attached";

    if (!m_ambiorix_sensing_cl.init_signal_loop(event_loop)) {
        LOG(ERROR) << "init_ambiorix_event_loop: init_signal_loop failed";
        LOG(INFO) << "init_ambiorix_event_loop: exit (false)";
        return false;
    }
    LOG(INFO) << "init_ambiorix_event_loop: signal loop attached";
    LOG(INFO) << "init_ambiorix_event_loop: exit (true)";
    return true;
}

}
