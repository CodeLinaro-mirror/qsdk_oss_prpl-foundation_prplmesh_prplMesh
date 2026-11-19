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
     * Controller decides CCE advertise/withdraw; the Agent applies it on
     * fronthaul beacons via ap_manager / HAL.
     */
    void handle_dpp_cce_indication(ieee1905_1::CmduMessageRx &cmdu_rx);

    /**
     * @brief Send an Error Response with a Profile-2 Error Code TLV, Reason Code 0x0D
     * (EasyMesh 5.3.4), for a CCE ENABLE received while EnableDPPOnboarding=0.
     *
     * @param mid Message ID of the DPP CCE Indication, for logging.
     */
    void send_dpp_onboarding_not_supported_error(uint16_t mid);
};
} // namespace beerocks

#endif // _PROXY_AGENT_DPP_TASK_H_
