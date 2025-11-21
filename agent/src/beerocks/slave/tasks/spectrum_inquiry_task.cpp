#include "spectrum_inquiry_task.h"
#include "../son_slave_thread.h"

#include <easylogging++.h>

#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryRequest.h>

#include <backhaul_manager/backhaul_manager.h>

namespace beerocks {
SpectrumInquiryTask::SpectrumInquiryTask(slave_thread &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::SPECTRUM_INQUIRY), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx)
{
}

bool SpectrumInquiryTask::create_available_spectrum_inquiry_message()
{
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE)) {
        LOG(ERROR) << "Failed to create AVAILABLE_SPECTRUM_INQUIRY_MESSAGE";
        return false;
    }

    if (!prepare_available_spectrum_inquiry_message()) {
        LOG(ERROR) << "AVAILABLE_SPECTRUM_INQUIRY_MESSAGE filling has failed";
        return false;
    }

    LOG(INFO) << "Sending AVAILABLE_SPECTRUM_INQUIRY_MESSAGE to controller";
    return m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

bool SpectrumInquiryTask::prepare_available_spectrum_inquiry_message()
{
    /**
     *  The tlvs created here are defined in the
     * specification as "One" (multi-ap specification v6, 17.2.104 and 17.2.105).
     */

    auto request_tlv = m_cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
    if (!request_tlv) {
        LOG(ERROR) << "Failed to get tlvAvailableSpectrumInquiryRequest from CmduMessageTx";
        return false;
    }

    if (!add_available_spectrum_inquiry_request_tlv(request_tlv)) {
        LOG(ERROR) << "Error filling AVAILABLE SPECTRUM INQUIRY REQUEST TLV";
        return false;
    }
    if (!add_available_spectrum_inquiry_response_tlv(m_cmdu_tx)) {
        LOG(ERROR) << "Error filling AVAILABLE SPECTRUM INQUIRY RESPONSE TLV";
        return false;
    }
    return true;
}

bool SpectrumInquiryTask::add_available_spectrum_inquiry_response_tlv(
    ieee1905_1::CmduMessageTx &cmdu_tx)
{
    return false;
}

bool SpectrumInquiryTask::add_available_spectrum_inquiry_request_tlv(
    const std::shared_ptr<wfa_map::tlvAvailableSpectrumInquiryRequest> &request_tlv)
{
    return false;
}
} // namespace beerocks
