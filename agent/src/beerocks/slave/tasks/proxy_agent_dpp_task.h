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
};
} // namespace beerocks

#endif // _PROXY_AGENT_DPP_TASK_H_
