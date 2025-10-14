#ifndef SPECTRUM_INQUIRY_TASK_H
#define SPECTRUM_INQUIRY_TASK_H
#include "../son_slave_thread.h"
#include "task.h"
#include <memory>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryRequest.h>
namespace beerocks {
class slave_thread;
class SpectrumInquiryTask : public Task {
public:
    enum class eEvent : uint8_t { AVAILABLE_SPECTRUM_INQUIRY = 0 };
    SpectrumInquiryTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);
    void work() override;

private:
    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    void create_available_spectrum_inquiry_message();
    bool prepare_available_spectrum_inquiry_message();
    bool add_available_spectrum_inquiry_request_tlv(
        const std::shared_ptr<wfa_map::tlvAvailableSpectrumInquiryRequest> &request_tlv);
    bool add_available_spectrum_inquiry_response_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);
};
} // namespace beerocks
#endif // SPECTRUM_INQUIRY_TASK_H
