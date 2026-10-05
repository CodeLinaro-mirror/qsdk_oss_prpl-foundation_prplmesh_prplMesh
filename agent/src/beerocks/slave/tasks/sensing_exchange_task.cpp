/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include "sensing_exchange_task.h"
#include "../agent_db.h"
#include "../son_slave_thread.h"

#include <tlvf/ieee_1905_1/eMessageType.h>
#include <tlvf/wfa_map/tlvSensingExchangeRequest.h>
#include <tlvf/wfa_map/tlvSensingExchangeResponse.h>
#include <easylogging++.h>
#include <cstdint>

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

    uint32_t exchange_id  = 0;
    bool datapath_ok = false; // TODO
    const uint16_t error_code = datapath_ok ? STATUS_SUCCESS : STATUS_DATA_PATH_DOES_NOT_EXIST;

    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::SENSING_EXCHANGE_RESPONSE_MESSAGE)) {
        LOG(ERROR) << "SensingExchange Response: failed to create SENSING_EXCHANGE_RESPONSE_MESSAGE";
        return true;
    }
    auto resp_tlv = m_cmdu_tx.addClass<wfa_map::tlvSensingExchangeResponse>();
    if (!resp_tlv) {
        LOG(ERROR) << "SensingExchange Response: addClass tlvSensingExchangeResponse failed";
        return true;
    }

    resp_tlv->exchange_id() = exchange_id;
    resp_tlv->error_code()  = error_code;

    if (!resp_tlv->finalize()) {
        LOG(ERROR) << "SensingExchange Response: tlvSensingExchangeResponse finalize failed";
        return true;
    }

    return m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

} // namespace beerocks
