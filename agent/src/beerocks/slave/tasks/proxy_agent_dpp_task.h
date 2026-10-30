#ifndef _PROXY_AGENT_DPP_TASK_H_
#define _PROXY_AGENT_DPP_TASK_H_

#include "task.h"
#include <bcl/network/sockets_impl.h>
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

    /** ap_manager which sent the last OTA DPP Authentication Response. */
    int m_active_onboarding_ap_manager_fd = beerocks::net::FileDescriptor::invalid_descriptor;

    /**
     * @brief Forward DPP CCE Indication to all local AP managers.
     *
     * Still required for FEAT-68: Controller decides CCE advertise/withdraw;
     * Agent applies it on fronthaul beacons via ap_manager / HAL.
     */
    void handle_dpp_cce_indication(ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Route a Proxied Encap DPP message on the OTA (ap_manager) path.
     *
     * - From an ap_manager (fd valid): forward to the Controller, or drop it while the
     *   TCP relay is active (DppAgentTask forwards the copy received from hostapd).
     * - From the Controller while the TCP relay is not active: Authentication Request to
     *   every ap_manager, other frames to the ap_manager serving the Enrollee.
     * Controller downlink while the TCP relay is active is left to DppAgentTask.
     *
     * @param fd ap_manager socket, or invalid for a message received from the broker.
     * @param src_mac 1905 source AL MAC (broker messages only).
     * @param cmdu_rx Received CMDU.
     * @return true if handled here, false to leave the message to another task.
     */
    bool handle_proxied_encap_dpp(int fd, const sMacAddr &src_mac,
                                  ieee1905_1::CmduMessageRx &cmdu_rx);
};
} // namespace beerocks

#endif // _PROXY_AGENT_DPP_TASK_H_
