#ifndef _SPECTRUM_INQUIRY_TASK_H_

#include "task.h"
#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryRequest.h>

namespace beerocks {

class slave_thread;

class SpectrumInquiryTask : public Task {

public:
    SpectrumInquiryTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx);

    /**
    * @brief Creates and packs the Available Spectrum Inquiry message into the CmduMessageTx.
    *
    * This method prepares the IEEE 1905.1 message for spectrum inquiry and populates
    * the required TLVs before sending it to the controller.
    *
    * @return true if the message was successfully created, false otherwise.
    */
    bool create_available_spectrum_inquiry_message();

private:
    slave_thread &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;

    bool prepare_available_spectrum_inquiry_message();
    bool add_available_spectrum_inquiry_request_tlv(
        const std::shared_ptr<wfa_map::tlvAvailableSpectrumInquiryRequest> &request_tlv);
    bool add_available_spectrum_inquiry_response_tlv(ieee1905_1::CmduMessageTx &cmdu_tx);
};

} // namespace beerocks

#endif // _SPECTRUM_INQUIRY_TASK_H_
