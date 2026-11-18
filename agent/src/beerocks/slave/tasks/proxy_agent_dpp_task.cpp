#include "proxy_agent_dpp_task.h"
#include "../son_slave_thread.h"
#include <tlvf/wfa_map/tlvDppCceIndication.h>
#include <tlvf/wfa_map/tlvProfile2ErrorCode.h>

namespace beerocks {

ProxyAgentDppTask::ProxyAgentDppTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::PROXY_AGENT_DPP), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx)
{
}

bool ProxyAgentDppTask::handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                                    const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                                    std::shared_ptr<beerocks_header> beerocks_header)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::DPP_CCE_INDICATION_MESSAGE: {
        handle_dpp_cce_indication(cmdu_rx);
        return true;
    }
    case ieee1905_1::eMessageType::CHIRP_NOTIFICATION_MESSAGE:
        // Chirp uplink is owned by DppAgentTask.
        return false;
    case ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE:
        // Proxied Encap is owned by DppAgentTask.
        return false;
    default: {
        // Message was not handled, therefore return false.
        return false;
    }
    }
}

void ProxyAgentDppTask::handle_dpp_cce_indication(ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto db = AgentDB::get();

    // EasyMesh 5.3.4: an Agent without DPP Onboarding support answers a CCE ENABLE with an
    // Error Response (Reason Code 0x0D). Send it once here, not once per radio from the AP
    // managers, and do not apply the CCE. A DISABLE is still forwarded.
    auto cce_tlv = cmdu_rx.getClass<wfa_map::tlvDppCceIndication>();
    if (cce_tlv && cce_tlv->advertise_cee() && !db->device_conf.enable_dpp_onboarding) {
        send_dpp_onboarding_not_supported_error(cmdu_rx.getMessageId());
        return;
    }

    for (auto radio : db->get_radios_list()) {
        auto ap_manager_fd = m_btl_ctx.get_ap_manager_fd(radio->front.iface_name);
        if (!m_btl_ctx.forward_cmdu_to_uds(ap_manager_fd, cmdu_rx)) {
            LOG(ERROR) << "Failed to forward message to ap_manager";
            return;
        }
    }
}

void ProxyAgentDppTask::send_dpp_onboarding_not_supported_error(uint16_t mid)
{
    LOG(WARNING) << "DPP CCE Indication mid=" << std::hex << mid << std::dec
                 << ": CCE ENABLE while EnableDPPOnboarding=0, sending Error Response"
                 << " (Reason Code 0x0D)";
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::ERROR_RESPONSE_MESSAGE)) {
        LOG(ERROR) << "cmdu creation of type ERROR_RESPONSE_MESSAGE failed";
        return;
    }
    auto error_code_tlv = m_cmdu_tx.addClass<wfa_map::tlvProfile2ErrorCode>();
    if (!error_code_tlv) {
        LOG(ERROR) << "addClass wfa_map::tlvProfile2ErrorCode failed";
        return;
    }
    error_code_tlv->reason_code() =
        wfa_map::tlvProfile2ErrorCode::eReasonCode::DPP_ONBOARDING_NOT_SUPPORTED;
    if (!m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx)) {
        LOG(ERROR) << "Failed to send ERROR_RESPONSE_MESSAGE (Reason Code 0x0D)";
    }
}

} // namespace beerocks
