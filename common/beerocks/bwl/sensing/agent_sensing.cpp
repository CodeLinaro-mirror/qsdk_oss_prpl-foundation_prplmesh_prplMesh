/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "agent_sensing.h"
#include <../../../../agent/src/beerocks/slave/agent_db.h>
#include <bcl/beerocks_utils.h>
//#include <bcl/network/network_utils.h>

// Ambiorix
#include "ambiorix_connection_manager.h"
#include "wbapi_utils.h"

#include <cstdint>
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

    LOG(ERROR) << "GAYATHRI:agent_sensing:  connect AmbiorixClient to sensing USP";
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
    AmbiorixVariant sensing_exchange_entries(AMXC_VAR_ID_HTABLE);
    const auto tx_mac = entry.tx_mac_valid ? tlvf::mac_to_string(entry.tx_mac) : std::string{};
    const auto rx_mac = entry.rx_mac_valid ? tlvf::mac_to_string(entry.rx_mac) : std::string{};

    sensing_exchange_entries.add_child("exchange_id", entry.exchange_id);
    sensing_exchange_entries.add_child("add_exchange", entry.add_exchange);
    sensing_exchange_entries.add_child("exchange_type", entry.exchange_type);
    sensing_exchange_entries.add_child("exchange_type_str", entry.exchange_type_str);
    sensing_exchange_entries.add_child("rate_tu10", entry.rate_tu10);
    sensing_exchange_entries.add_child("bandwidth_mhz", entry.bandwidth_mhz);
    sensing_exchange_entries.add_child("ntx", entry.ntx);
    sensing_exchange_entries.add_child("nrx", entry.nrx);
    sensing_exchange_entries.add_child("data_type", entry.data_type);
    sensing_exchange_entries.add_child("csi_threshold", entry.csi_threshold);
    sensing_exchange_entries.add_child("tx_mac_valid", entry.tx_mac_valid);
    sensing_exchange_entries.add_child("rx_mac_valid", entry.rx_mac_valid);
    sensing_exchange_entries.add_child("tx_mac", tx_mac);
    sensing_exchange_entries.add_child("rx_mac", rx_mac);

    args.add_child<AmbiorixVariant &>("sensing_exchange_entries", sensing_exchange_entries);
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

}
