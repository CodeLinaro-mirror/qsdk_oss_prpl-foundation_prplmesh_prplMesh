#ifndef _PROXY_AGENT_DPP_TASK_H_
#define _PROXY_AGENT_DPP_TASK_H_

#include "task.h"
#include <tlvf/CmduMessageTx.h>

namespace beerocks {

// Forward declaration for Agent context saving
class slave_thread;

class ProxyAgentDppTask : public Task {
public:
    ProxyAgentDppTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);
    ~ProxyAgentDppTask() {}

    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;

private:
    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;

    /**
     * @brief Forward DPP CCE Indication to all local AP managers.
     *
     * Still required for FEAT-68: Controller decides CCE advertise/withdraw;
     * Agent applies it on fronthaul beacons via ap_manager / HAL.
     */
    void handle_dpp_cce_indication(ieee1905_1::CmduMessageRx &cmdu_rx);

#if 0
    // --- Legacy OTA Proxy Agent path (disabled; FEAT-68 TCP relay) -----------------
    // Chirp uplink: DppAgentTask::handle_presence_announcement() builds
    // CHIRP_NOTIFICATION from hostapd frames on WiFi.DPPRelay (TCP).
    // Encap: DppAgentTask owns PROXIED_ENCAP via slave_wlan_hal::dpp_send_frame().
    // See .documentation/DPP-over-tcp.md and feat68-dpp-doc.md "DPP Relay Service".
    int active_onboarding_ap_manager_fd = beerocks::net::FileDescriptor::invalid_descriptor;
    void handle_chirp_notification(ieee1905_1::CmduMessageRx &cmdu_rx);
    void handle_proxied_encap_dpp(int fd, const sMacAddr &src_mac,
                                  ieee1905_1::CmduMessageRx &cmdu_rx);
#endif
};
} // namespace beerocks

#endif // _PROXY_AGENT_DPP_TASK_H_
