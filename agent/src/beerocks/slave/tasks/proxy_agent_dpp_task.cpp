#include "proxy_agent_dpp_task.h"
#include "../son_slave_thread.h"
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
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
        // OTA chirps from an ap_manager are forwarded by slave_thread; chirps received
        // through the TCP relay are sent by DppAgentTask.
        return false;
    case ieee1905_1::eMessageType::PROXIED_ENCAP_DPP_MESSAGE:
        return handle_proxied_encap_dpp(fd, src_mac, cmdu_rx);
    default: {
        // Message was not handled, therefore return false.
        return false;
    }
    }
}

void ProxyAgentDppTask::handle_dpp_cce_indication(ieee1905_1::CmduMessageRx &cmdu_rx)
{
    auto db = AgentDB::get();
    if (!db->device_conf.enable_dpp_onboarding) {
        // Without DPP onboarding no CCE is advertised. EasyMesh: an Agent that cannot
        // advertise the CCE on ENABLE answers with an Error Response, reason 0x0D.
        auto cce_tlv = cmdu_rx.getClass<wfa_map::tlvDppCceIndication>();
        if (cce_tlv && cce_tlv->advertise_cee()) {
            LOG(INFO) << "DPP CCE Indication ENABLE while DPP onboarding is disabled";
            send_dpp_onboarding_not_supported_error();
        }
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

void ProxyAgentDppTask::send_dpp_onboarding_not_supported_error()
{
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

    LOG(INFO) << "Sending Error Response (DPP onboarding not supported) to the Controller";
    m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

bool ProxyAgentDppTask::handle_proxied_encap_dpp(int fd, const sMacAddr &src_mac,
                                                 ieee1905_1::CmduMessageRx &cmdu_rx)
{
    const bool from_ap_manager = (fd != beerocks::net::FileDescriptor::invalid_descriptor);
    const bool relay_active    = m_btl_ctx.is_dpp_relay_active();

    // Controller downlink is owned by DppAgentTask while the TCP relay is active.
    if (!from_ap_manager && relay_active) {
        return false;
    }

    auto encap_1905_dpp_tlv = cmdu_rx.getClass<wfa_map::tlv1905EncapDpp>();
    if (!encap_1905_dpp_tlv) {
        LOG(ERROR) << "Proxied encap dpp message doesn't contain 1905 encap dpp tlv";
        return true;
    }
    const auto frame_type = encap_1905_dpp_tlv->frame_type();

    if (from_ap_manager) {
        // Uplink from a local ap_manager (OTA path).
        if (relay_active) {
            // hostapd also hands the frame to the TCP relay, DppAgentTask forwards that copy.
            LOG(DEBUG) << "DPP: dropping ap_manager OTA Proxied Encap DPP (frame_type="
                       << int(frame_type) << "): TCP relay is active";
            return true;
        }
        // Remember the ap_manager serving the Enrollee, for the downlink of this session.
        if (frame_type == wfa_map::tlv1905EncapDpp::eFrameType::DPP_AUTHENTICATION_RESPONSE) {
            m_active_onboarding_ap_manager_fd = fd;
        }
        if (!m_btl_ctx.forward_cmdu_to_controller(cmdu_rx)) {
            LOG(ERROR) << "DPP: failed to forward ap_manager Proxied Encap DPP to the Controller";
        }
        return true;
    }

    // Downlink while the TCP relay is not active: OTA path through the ap_manager.
    auto db = AgentDB::get();
    if (src_mac != db->controller_info.bridge_mac) {
        LOG(DEBUG) << "DPP: ignoring Proxied Encap DPP from " << tlvf::mac_to_string(src_mac)
                   << ": not the Controller";
        return true;
    }

    if (frame_type == wfa_map::tlv1905EncapDpp::eFrameType::DPP_AUTHENTICATION_REQUEST) {
        // The Enrollee location is not known yet: send it to every ap_manager.
        for (auto radio : db->get_radios_list()) {
            auto ap_manager_fd = m_btl_ctx.get_ap_manager_fd(radio->front.iface_name);
            if (!m_btl_ctx.forward_cmdu_to_uds(ap_manager_fd, cmdu_rx)) {
                LOG(ERROR) << "Failed to forward message to ap_manager";
                return true;
            }
        }
        return true;
    }

    if (m_active_onboarding_ap_manager_fd == beerocks::net::FileDescriptor::invalid_descriptor) {
        LOG(WARNING) << "DPP: no ap_manager serves an onboarding Enrollee, dropping Proxied Encap"
                     << " DPP (frame_type=" << int(frame_type) << ")";
        return true;
    }
    if (!m_btl_ctx.forward_cmdu_to_uds(m_active_onboarding_ap_manager_fd, cmdu_rx)) {
        LOG(ERROR) << "Failed to forward message to ap_manager";
    }
    return true;
}

} // namespace beerocks
