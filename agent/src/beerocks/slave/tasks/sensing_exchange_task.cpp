/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include "sensing_exchange_task.h"
#include "../agent_db.h"
#include "../son_slave_thread.h"

#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/wfa_map/tlvSensingExchangeRequest.h>
#include <tlvf/wfa_map/tlvSensingExchangeResponse.h>
#include <easylogging++.h>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace beerocks {
namespace {
static const char *sensing_exchange_type_wire_to_name(uint8_t wire)
{
  switch (wire) {
  case 1:  return "qosnull";
  case 2:  return "opportunistic";
  case 3:  return "probe";
  case 4:  return "tb-sr2si";
  case 5:  return "tb-sr2sr";
  case 6:  return "tb-si2sr";
  case 7:  return "cts2self";
  case 8:  return "nontb-si2sr";
  case 9:  return "nontb-sr2si";
  case 10: return "sbp-si2si";
  case 11: return "sbp-sr2si";
  case 12: return "sbp-sr2sr";
  default: return "unknown";
  }
}
}
namespace {
constexpr uint16_t STATUS_SUCCESS                  = 0x0000;
constexpr uint16_t STATUS_DATA_PATH_DOES_NOT_EXIST = 0x1001;

/** Map wifi-sensing / AgentDB ErrorCode string to MAP TLV uint16 (decimal or 0x hex). */
uint16_t sensing_exchange_error_code_from_db_string(const std::string &s)
{
    if (s.empty()) {
        return STATUS_SUCCESS;
    }
    const char *p    = s.c_str();
    char *endptr     = nullptr;
    unsigned long v  = 0;
    if (s.size() >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        v = std::strtoul(p, &endptr, 16);
    } else {
        v = std::strtoul(p, &endptr, 10);
    }
    if (endptr == p) {
        return STATUS_SUCCESS;
    }
    return static_cast<uint16_t>(v & 0xffffUL);
}
}

SensingExchangeTask::SensingExchangeTask(slave_thread &btl_ctx,
                                         ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::SENSING_EXCHANGE), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx)
{
}
bool SensingExchangeTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                      const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                                      std::shared_ptr<beerocks_header> beerocks_header)
{
    if (cmdu_rx.getMessageType() != ieee1905_1::eMessageType::SENSING_EXCHANGE_REQUEST_MESSAGE) {
        return false;
    }

    const auto mid = cmdu_rx.getMessageId();

    auto tlv = cmdu_rx.getClass<wfa_map::tlvSensingExchangeRequest>();
    if (!tlv) {
        LOG(ERROR) << "Sensing Exchange Request: getClass<tlvSensingExchangeRequest> failed";
        return true;
    }

    auto db = AgentDB::get();
    AgentDB::sSensingExchangeEntry entry{};
    entry.exchange_id   = tlv->ExchangeID();
    entry.add_exchange  = tlv->AddRemoveFlags().AddRemove ? 1 : 0;
    entry.exchange_type = tlv->ExchangeType();
    entry.exchange_type_str = sensing_exchange_type_wire_to_name(entry.exchange_type);
    entry.rate_tu10     = tlv->Rate();
    entry.bandwidth_mhz = tlv->Bandwidth();
    entry.ntx           = tlv->NTx();
    entry.nrx           = tlv->NRx();
    entry.data_type     = tlv->DataType();
    entry.csi_threshold = tlv->CSI_Threshold();
    entry.tx_mac_valid  = tlv->MacAddrValid().Transmitter_MAC_Address_Valid;
    entry.rx_mac_valid  = tlv->MacAddrValid().Receiver_MAC_Address_Valid;
    if (entry.tx_mac_valid) entry.tx_mac = tlv->Transmitter_MAC_Address();
    if (entry.rx_mac_valid) entry.rx_mac = tlv->Receiver_MAC_Address();

    db->sensing_exchange_entries.push_back(entry); // Use push_back() to add elements

    // Invoke AddExchange / RemoveExchange via sensing BWL (AddRemove flag from TLV)
    bool exchange_ok = false;
    if (auto *sensing = m_btl_ctx.get_agent_sensing()) {
        if (entry.add_exchange) {
            exchange_ok = sensing->AddExchange();
            if (!exchange_ok) {
                LOG(ERROR) << "Sensing Exchange: agent_sensing::AddExchange failed";
            }
        } else {
            exchange_ok = sensing->RemoveExchange();
            if (!exchange_ok) {
                LOG(ERROR) << "Sensing Exchange: agent_sensing::RemoveExchange failed";
            }
        }
    } else {
        LOG(ERROR) << "Sensing Exchange: agent_sensing is null";
    }

    // Send ACK back to controller
    if (!m_cmdu_tx.create(mid, ieee1905_1::eMessageType::ACK_MESSAGE)) {
        LOG(ERROR) << "Sensing Exchange Request: failed to create ACK_MESSAGE";
        return false;
    }
    LOG(DEBUG) << "sending ACK";
    if (!m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx)) {
       LOG(ERROR) << "failed sending ACK";
       return false;
    }
    //return m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);

    const uint32_t req_exchange_id = entry.exchange_id;
    uint16_t response_error_code     = STATUS_DATA_PATH_DOES_NOT_EXIST;
    if (exchange_ok) {
        std::string err_from_db;
        for (const auto &e : db->sensing_exchange_entries) {
            if (e.exchange_id == req_exchange_id) {
                err_from_db = e.error_code;
                break;
            }
        }
        response_error_code = sensing_exchange_error_code_from_db_string(err_from_db);
    }

    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::SENSING_EXCHANGE_RESPONSE_MESSAGE)) {
        LOG(ERROR) << "SensingExchange Response: failed to create SENSING_EXCHANGE_RESPONSE_MESSAGE";
        return true;
    }
    auto resp_tlv = m_cmdu_tx.addClass<wfa_map::tlvSensingExchangeResponse>();
    if (!resp_tlv) {
        LOG(ERROR) << "SensingExchange Response: addClass tlvSensingExchangeResponse failed";
        return true;
    }

    resp_tlv->exchange_id() = req_exchange_id;
    resp_tlv->error_code()  = response_error_code;

    if (!resp_tlv->finalize()) {
        LOG(ERROR) << "SensingExchange Response: tlvSensingExchangeResponse finalize failed";
        return true;
    }

    return m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

namespace {
uint16_t cause_to_error_code(const std::string &cause)
{
    // ODL Causes: STAUnavailable, RadioUnavailable, Completed,
    //             STARejected, ResourceUnavailable
    if (cause.empty() || cause == "Completed") {
        LOG(DEBUG) << "cause_to_error_code: cause='" << cause
                   << "' -> STATUS_SUCCESS";
        return STATUS_SUCCESS; // 0x0000
    }
    // Map other causes as needed; until then treat as failure
    LOG(INFO) << "cause_to_error_code: cause='" << cause
              << "' -> STATUS_DATA_PATH_DOES_NOT_EXIST";
    return STATUS_DATA_PATH_DOES_NOT_EXIST; // or dedicated status values
}
} // namespace

void SensingExchangeTask::handle_event(uint8_t event_enum_value, const void *event_obj)
{
    LOG(INFO) << "SensingExchangeTask::handle_event: enter"
              << " event_enum=" << static_cast<unsigned>(event_enum_value);

    if (eEvent(event_enum_value) != eEvent::EXCHANGE_TERMINATED || !event_obj) {
        LOG(DEBUG) << "SensingExchangeTask::handle_event: ignore"
                   << " event_enum=" << static_cast<unsigned>(event_enum_value)
                   << " event_obj=" << (event_obj ? "set" : "null");
        return;
    }

    const auto *ev = static_cast<const sExchangeTerminatedEvent *>(event_obj);
    LOG(INFO) << "SensingExchangeTask::handle_event: EXCHANGE_TERMINATED received"
              << " ExchangeID=" << ev->exchange_id << " Cause=" << ev->cause
              << " — will send unsolicited SensingExchange Response";

    const uint16_t error_code = cause_to_error_code(ev->cause);
    LOG(INFO) << "SensingExchangeTask::handle_event: mapped Cause='" << ev->cause
              << "' to TLV error_code=0x" << std::hex << error_code << std::dec;

    // Optional: update AgentDB
    auto db          = AgentDB::get();
    bool found_in_db = false;
    for (auto &e : db->sensing_exchange_entries) {
        if (e.exchange_id == ev->exchange_id) {
            e.error_code = ev->cause;
            found_in_db  = true;
            LOG(INFO) << "SensingExchangeTask::handle_event: updated AgentDB"
                      << " ExchangeID=" << ev->exchange_id << " error_code=" << ev->cause;
            break;
        }
    }
    if (!found_in_db) {
        LOG(WARNING) << "SensingExchangeTask::handle_event: ExchangeID="
                     << ev->exchange_id
                     << " not found in AgentDB sensing_exchange_entries"
                     << " (still sending unsolicited response)";
    }

    LOG(INFO) << "SensingExchangeTask::handle_event: invoking"
              << " send_unsolicited_exchange_response ExchangeID=" << ev->exchange_id
              << " error_code=0x" << std::hex << error_code << std::dec;
    if (!send_unsolicited_exchange_response(ev->exchange_id, error_code)) {
        LOG(ERROR) << "SensingExchangeTask::handle_event: failed to send"
                   << " unsolicited SensingExchange Response ExchangeID="
                   << ev->exchange_id;
    } else {
        LOG(INFO) << "SensingExchangeTask::handle_event: unsolicited"
                  << " SensingExchange Response sent ExchangeID=" << ev->exchange_id;
    }
    LOG(INFO) << "SensingExchangeTask::handle_event: exit";
}

bool SensingExchangeTask::send_unsolicited_exchange_response(uint32_t exchange_id,
                                                             uint16_t error_code)
{
    LOG(INFO) << "send_unsolicited_exchange_response: enter"
              << " ExchangeID=" << exchange_id << " error_code=0x" << std::hex
              << error_code << std::dec;

    // mid=0 => unsolicited (not tied to a request)
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::SENSING_EXCHANGE_RESPONSE_MESSAGE)) {
        LOG(ERROR) << "send_unsolicited_exchange_response: failed to create"
                   << " SENSING_EXCHANGE_RESPONSE_MESSAGE";
        return false;
    }
    LOG(INFO) << "send_unsolicited_exchange_response: CMDU created"
              << " msg=SENSING_EXCHANGE_RESPONSE_MESSAGE mid=" << std::hex
              << m_cmdu_tx.getMessageId() << std::dec
              << " (0 => transport may assign mid)";

    auto resp_tlv = m_cmdu_tx.addClass<wfa_map::tlvSensingExchangeResponse>();
    if (!resp_tlv) {
        LOG(ERROR) << "send_unsolicited_exchange_response:"
                   << " addClass tlvSensingExchangeResponse failed";
        return false;
    }

    resp_tlv->exchange_id() = exchange_id;
    resp_tlv->error_code()  = error_code;
    LOG(INFO) << "send_unsolicited_exchange_response: TLV filled"
              << " ExchangeID=" << exchange_id << " error_code=0x" << std::hex
              << error_code << std::dec;

    if (!resp_tlv->finalize()) {
        LOG(ERROR) << "send_unsolicited_exchange_response:"
                   << " tlvSensingExchangeResponse finalize failed";
        return false;
    }

    LOG(INFO) << "send_unsolicited_exchange_response: sending unsolicited"
              << " SensingExchange Response CMDU to controller"
              << " ExchangeID=" << exchange_id << " error_code=0x" << std::hex
              << error_code << std::dec;

    const bool sent = m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
    if (sent) {
        LOG(INFO) << "send_unsolicited_exchange_response: exit (true)"
                  << " ExchangeID=" << exchange_id
                  << " — unsolicited response sent to controller";
    } else {
        LOG(ERROR) << "send_unsolicited_exchange_response: exit (false)"
                   << " send_cmdu_to_controller failed ExchangeID=" << exchange_id;
    }
    return sent;
}
} // namespace beerocks
