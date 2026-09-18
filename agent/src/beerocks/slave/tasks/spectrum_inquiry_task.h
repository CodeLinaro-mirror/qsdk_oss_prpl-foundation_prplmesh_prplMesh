#ifndef _SPECTRUM_INQUIRY_TASK_H_
#define _SPECTRUM_INQUIRY_TASK_H_

#include "../agent_db.h"
#include "task.h"
#include <memory>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryRequest.h>
#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryResponse.h>

namespace beerocks {

class BackhaulManager;
class ChannelSelectionTask;

class SpectrumInquiryTask : public Task {

public:
    SpectrumInquiryTask(BackhaulManager &btl_ctx, ieee1905_1::CmduMessageTx &cmdu_tx,
                        const std::shared_ptr<ChannelSelectionTask> &channel_selection_task);

    bool handle_cmdu(ieee1905_1::CmduMessageRx &cmdu_rx, uint32_t iface_index,
                     const sMacAddr &dst_mac, const sMacAddr &src_mac, int fd,
                     std::shared_ptr<beerocks_header> beerocks_header) override;

    /**
     * @brief Creates and sends the Available Spectrum Inquiry message to the controller.
     *
     * Builds the IEEE 1905.1 AVAILABLE_SPECTRUM_INQUIRY_MESSAGE (EasyMesh §17.1.67) with
     * Request/Response TLVs (§17.2.104 / §17.2.105) and optional Channel Preference TLVs.
     *
     * @return true if the message was successfully created and sent, false otherwise.
     */
    bool create_available_spectrum_inquiry_message();

private:
    /**
     * @brief Handle ACTION_BACKHAUL_AFC_UPDATE_NOTIFICATION from the fronthaul/AP manager path.
     *
     * Reads AFC inquiry payloads from the platform DM, builds optional inquiry Channel Preference
     * state (reason 0xD), then sends AVAILABLE_SPECTRUM_INQUIRY_MESSAGE (§8.2.5).
     *
     * @return true on success, false on hard failure.
     */
    bool handle_afc_update_notification();

    /**
     * @brief Populate m_cmdu_tx with Required Request/Response TLVs and optional Preference TLVs.
     *
     * @return true if all required TLVs were added successfully.
     */
    bool prepare_available_spectrum_inquiry_message();

    /**
     * @brief Fill Available Spectrum Inquiry Request TLV (§17.2.104) with opaque AFC request JSON.
     *
     * @param[in,out] request_tlv TLV already added to the CMDU.
     * @param[in] request_data Opaque AvailableSpectrumInquiryRequestMessage payload.
     * @return true on success.
     */
    bool add_available_spectrum_inquiry_request_tlv(
        const std::shared_ptr<wfa_map::tlvAvailableSpectrumInquiryRequest> &request_tlv,
        const std::string &request_data);

    /**
     * @brief Add Available Spectrum Inquiry Response TLV (§17.2.105) with opaque AFC response JSON.
     *
     * @param[in,out] cmdu_tx Outgoing CMDU.
     * @param[in] response_data Opaque AvailableSpectrumInquiryResponseMessage payload.
     * @return true on success.
     */
    bool add_available_spectrum_inquiry_response_tlv(ieee1905_1::CmduMessageTx &cmdu_tx,
                                                     const std::string &response_data);

    /**
     * @brief Optionally append Channel Preference TLVs (reason 0xD) for 6 GHz radios with AFC deltas.
     *
     * Per EasyMesh §8.2.5 / §17.1.67 these TLVs are optional; returning true with no TLV added
     * is valid when there is no channel delta to report.
     *
     * @return true on success (including when no optional TLV is needed), false on encode failure.
     */
    bool add_afc_channel_preference_tlvs();

    BackhaulManager &m_btl_ctx;
    ieee1905_1::CmduMessageTx &m_cmdu_tx;
    std::shared_ptr<ChannelSelectionTask> m_channel_selection_task;
};

} // namespace beerocks

#endif // _SPECTRUM_INQUIRY_TASK_H_
