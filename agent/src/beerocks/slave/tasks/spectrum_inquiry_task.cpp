#include "spectrum_inquiry_task.h"
#include "../backhaul_manager/backhaul_manager.h"
#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryRequest.h>
#include <tlvf/wfa_map/tlvAvailableSpectrumInquiryResponse.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <easylogging++.h>
#define RESPONSE_FILE "tempResponse.txt"
#include <json-c/json.h>
#include <nlohmann/json.hpp>


using json = nlohmann::json;
using namespace wfa_map;

namespace beerocks {
SpectrumInquiryTask::SpectrumInquiryTask(slave_thread &btl_ctx,
                                         ieee1905_1::CmduMessageTx &cmdu_tx)
    : Task(eTaskType::SPECTRUM_INQUIRY), m_btl_ctx(btl_ctx), m_cmdu_tx(cmdu_tx)
{
}
void SpectrumInquiryTask::work()
{
    // No periodic work
}

void SpectrumInquiryTask::create_available_spectrum_inquiry_message()
{
    if (!m_cmdu_tx.create(0, ieee1905_1::eMessageType::AVAILABLE_SPECTRUM_INQUIRY_MESSAGE)) {
        LOG(ERROR) << "Failed to create AVAILABLE_SPECTRUM_INQUIRY_MESSAGE";
        return;
    }
    auto request_tlv = m_cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
    if (!request_tlv) {
      LOG(ERROR) << "Failed to add AvailableSpectrumInquiryRequest TLV";
        return;
    }
    if (!prepare_available_spectrum_inquiry_message()) {
        LOG(ERROR) << "AVAILABLE_SPECTRUM_INQUIRY_MESSAGE filling has failed";
        return;
    }
    
    LOG(INFO) << "Sending AVAILABLE_SPECTRUM_INQUIRY_MESSAGE to controller";
    m_btl_ctx.send_cmdu_to_controller({}, m_cmdu_tx);
}

bool SpectrumInquiryTask::prepare_available_spectrum_inquiry_message()
{
	/**
     *  The tlvs created here are defined in the
     * specification as "One" (multi-ap specification v6, 17.2.104 and 17.2.105).
     **/

        auto request_tlv = m_cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
        if (!request_tlv) {
        LOG(ERROR) << "Failed to get tlvAvailableSpectrumInquiryRequest from CmduMessageTx";
        return false;
      }
        
        //auto tlv = m_cmdu_tx->getClass<wfa_map::tlvAvailableSpectrumInquiryRequest>();
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
bool SpectrumInquiryTask::add_available_spectrum_inquiry_response_tlv(ieee1905_1::CmduMessageTx &cmdu_tx)
{
    // Create an empty TLV container
    auto response_tlv = cmdu_tx.addClass<wfa_map::tlvAvailableSpectrumInquiryResponse>();
    if (!response_tlv) {
        LOG(ERROR) << "Error creating Available Spectrum Inquiry Response TLV!";
        return false;
    }
    // Read the AFC JSON response file
    std::ifstream file(RESPONSE_FILE);
    if (!file.is_open()) {
        LOG(ERROR) << "Failed to open AFC Response File: " << RESPONSE_FILE;
        return false;
    }
    std::stringstream buffer;//string based buffer decleration.
    buffer << file.rdbuf();//copies the content of resonse file to buffer.
    std::vector<uint8_t> binary_buffer;
    // Parse the JSON content
    json j;//JSON object decleration
    try {
        j = json::parse(buffer.str());//copying into json object as string,basically structured json object data
    } catch (const std::exception& e) {
        LOG(ERROR) << "JSON parsing error: " << e.what();
        return false;
    }
    // Validate that the AFC response is present.
    if (!j.contains("availableSpectrumInquiryResponses") || !j["availableSpectrumInquiryResponses"].is_array()) {
        LOG(ERROR) << "Invalid AFC response format: missing 'availableSpectrumInquiryResponses'";
        return false;
    }

    // Serialize the JSON back into a compact string and convert to binary
    std::vector<uint8_t> bin_buffer;
    try {
        std::string serialized_str = j.dump(); // compact form
        binary_buffer.assign(serialized_str.begin(), serialized_str.end());
    } catch (const std::exception &e) {
        LOG(ERROR) << "Failed to serialize JSON: " << e.what();
        return false;
    }
    // Set TLV payload
    if (!response_tlv->set_available_spectrum_inquiry_response_obj(binary_buffer.data(), binary_buffer.size())) {
        LOG(ERROR) << "Failed to set available spectrum inquiry response object";
        return false;
    }
    if (!response_tlv->finalize()) {
        LOG(ERROR) << "TLV finalization failed";
        return false;
    }
    LOG(INFO) << "Successfully filled TLV with Available Spectrum Inquiry Response payload (length = "
              << binary_buffer.size() << ")";
    return true;
}


bool SpectrumInquiryTask::add_available_spectrum_inquiry_request_tlv(
    const std::shared_ptr<wfa_map::tlvAvailableSpectrumInquiryRequest> &request_tlv)
{
    // Step 1: Load JSON file
    std::ifstream file("tempRequest.txt");
    if (!file.is_open()) {
        LOG(ERROR) << "Failed to open tempRequest.txt";
        return false;
    }
    std::string json_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    LOG(DEBUG) << "Loaded JSON string: " << json_string;

    // Step 2: Parse JSON
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_string);
    } catch (const std::exception &e) {
        LOG(ERROR) << "Failed to parse JSON: " << e.what();
        return false;
    }

    if (!root.contains("availableSpectrumInquiryRequests")) {
        LOG(ERROR) << "Missing 'availableSpectrumInquiryRequests' array in JSON";
        return false;
    }

    // Step 3: Serialize JSON back to binary form
    std::vector<uint8_t> binary_buffer;
    try {
        std::string serialized_str = root.dump(); // compact form
        binary_buffer.assign(serialized_str.begin(), serialized_str.end());
    } catch (const std::exception &e) {
        LOG(ERROR) << "Failed to serialize JSON: " << e.what();
        return false;
    }

    // Step 4: Set TLV buffer
    if (!request_tlv->set_available_spectrum_inquiry_request_obj(binary_buffer.data(), binary_buffer.size())) {
        LOG(ERROR) << "Failed to set available spectrum inquiry request obj";
        return false;
    }

    LOG(INFO) << "Successfully filled TLV with Available Spectrum Inquiry Request payload (length = "
              << binary_buffer.size() << ")";

    if (!request_tlv->finalize()) {
        LOG(ERROR) << "TLV finalization failed";
        return false;
    }

    return true;
}
} // namespace beerocks
