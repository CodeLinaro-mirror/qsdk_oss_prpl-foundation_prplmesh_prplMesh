/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#ifndef _SENSING_EXCHANGE_TASK_H_
#define _SENSING_EXCHANGE_TASK_H_

#include "task.h"
#include <tlvf/CmduMessageTx.h>

namespace beerocks {

class slave_thread;

class SensingExchangeTask : public Task {
public:
    SensingExchangeTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);

    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;

enum eEvent : uint8_t {
    EXCHANGE_TERMINATED,
};

struct sExchangeTerminatedEvent {
    uint32_t exchange_id;
    std::string cause;
};

void handle_event(uint8_t event_enum_value, const void *event_obj) override;

private:
    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    bool send_unsolicited_exchange_response(uint32_t exchange_id, uint16_t error_code);
};

} // namespace beerocks

#endif
