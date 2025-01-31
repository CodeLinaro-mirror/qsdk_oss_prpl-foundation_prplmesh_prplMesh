/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "airties_utils.h"
#include "agent_db.h"
#include <common_utility.h>
#include <vendor_message_slave.h>

#include <bcl/beerocks_utils.h>
#include <tlvf/airties/eAirtiesTLVId.h>
#include <tlvf/airties/tlvAirtiesMsgType.h>

#define REBOOT_ACTION_VALUE 0x00
#define FR_VALUE 0x01
#define RESET_VALUE 0x00
#define RESET_PLUS_REBOOT 0x01
#define REBOOT_MESSAGE_ID 0x01
#define DEVICE_MANAGEMENT_MESSAGE_ID 0x07

using namespace airties;
using namespace vendor_message;

std::vector<std::shared_ptr<ieee1905_1::tlvVendorSpecific>> AirtiesUtils::airties_vs_tlv_list;

bool AirtiesUtils::handle_reboot_and_factory_reset(uint8_t action_value)
{
    bool ret_val = false;
    beerocks::wbapi::AmbiorixVariant args, result;

    //Check for Reboot Value.
    if (action_value == REBOOT_ACTION_VALUE) {
        //DM_ENG_Device_Reboot_DoReboot
        ret_val = airties_instance.m_ambiorix_cl.call("Device.", "Reboot", args, result);
        if (!ret_val) {
            return false;
        }
        //Check for FR.
    } else if (action_value == FR_VALUE) {
        //DM_ENG_Device_FactoryReset_DoFactoryReset
        ret_val = airties_instance.m_ambiorix_cl.call("Device.", "FactoryReset", args, result);
        if (!ret_val) {
            return false;
        }
    } else {
        LOG(INFO) << "\n Neither Reboot Nor Factory Reset";
    }
    return ret_val;
}

bool AirtiesUtils::parse_airties_tlv(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                     ieee1905_1::CmduMessageRx &cmdu_rx,
                                     ieee1905_1::CmduMessageTx &cmdu_tx)
{
    //Extract the Device Message Type Airties TLV ID.
    uint16_t message_type_Airties_tlv_id =
        vendor_message::extract_uint16(airties::AirtiesUtils::airties_vs_tlv_list[0]->payload(), 0);

    switch (message_type_Airties_tlv_id) {
    case (static_cast<int>(airties::eAirtiesTlVId::AIRTIES_MSG_TYPE)):
        if (!parse_airties_message_type_tlv(btl_ctx, dst_mac, cmdu_rx, cmdu_tx)) {
            LOG(DEBUG) << "Failed to parse Airties Device Message Type TLV ";
            return false;
        }
        break;
    default:
        LOG(DEBUG) << "No other Airties ID currently supported";
        break;
    }
    return true;
}

bool AirtiesUtils::parse_airties_message_type_tlv(VendorMessageSlave &btl_ctx,
                                                  const sMacAddr &dst_mac,
                                                  ieee1905_1::CmduMessageRx &cmdu_rx,
                                                  ieee1905_1::CmduMessageTx &cmdu_tx)
{

    //Extract the last two octet of Device Msg Type TLV to get the message id for next TLV
    uint16_t message_type_message_id =
        vendor_message::extract_uint16(airties::AirtiesUtils::airties_vs_tlv_list[0]->payload(), 2);

    switch (message_type_message_id) {
    case REBOOT_MESSAGE_ID:
        if (!AirtiesUtils::parse_tlv_reboot_reset(btl_ctx, dst_mac, cmdu_tx, cmdu_rx)) {
            LOG(DEBUG) << "Failed to verify the Airties Message ID: " << std::hex
                       << message_type_message_id;
            return false;
        }
        break;
    case DEVICE_MANAGEMENT_MESSAGE_ID:
        LOG(DEBUG) << "not implemented";
        break;
    default:
        LOG(DEBUG) << "Neither reboot/reset nor led tlv";
        break;
    }
    return true;
}

bool AirtiesUtils::parse_tlv_reboot_reset(VendorMessageSlave &btl_ctx, const sMacAddr &dst_mac,
                                          ieee1905_1::CmduMessageTx &cmdu_tx,
                                          ieee1905_1::CmduMessageRx &cmdu_rx)
{
    for (auto tlv : AirtiesUtils::airties_vs_tlv_list) {

        //extract the tlv_id either Airties Reboot Request TLV ID or Device Management TLV ID.
        uint16_t tlv_id = vendor_message::extract_uint16(tlv->payload(), 0);
        if (!tlv_id) {
            LOG(DEBUG) << "Failed to get the TLV ID";
            return false;
        }

        switch (tlv_id) {
        case static_cast<int>(airties::eAirtiesTlVId::AIRTIES_REBOOT_REQUEST): {
            //Extract the boot action value from the Airties Reboot Request TLV payload.
            uint8_t action_value           = vendor_message::extract_uint8(tlv->payload(), 2);
            auto mid                       = cmdu_rx.getMessageId();
            static bool reboot_in_progress = false;

            switch (action_value) {
            //Reboot
            case REBOOT_ACTION_VALUE: {
                LOG(DEBUG) << "Sending ack to controller";
                btl_ctx.send_cmdu_to_controller(dst_mac, cmdu_tx, mid,
                                                ieee1905_1::eMessageType::ACK_MESSAGE);

                // Check if reboot is already in progress
                if (reboot_in_progress) {
                    LOG(WARNING) << "Reboot already in progress.";
                }

                // Set flag to indicate reboot is in progress
                reboot_in_progress = true;

                //Trigger the handle_reboot_and_factory_reset to perform the Reboot.
                if (!airties_instance.handle_reboot_and_factory_reset(action_value)) {
                    LOG(ERROR) << "\n Failed to Reboot the device";
                    // Clear flag if reboot failed
                    reboot_in_progress = false;
                    break;
                }
                return true;
            }
            //reboot+reset
            case RESET_PLUS_REBOOT: {
                uint8_t reset_type_value = vendor_message::extract_uint8(tlv->payload(), 3);
                if (reset_type_value == RESET_VALUE) {
                    LOG(DEBUG) << "Reset is not supported";
                    return true;
                } else if (reset_type_value == FR_VALUE) {
                    LOG(DEBUG) << "Sending ack to controller";
                    btl_ctx.send_cmdu_to_controller(dst_mac, cmdu_tx, mid,
                                                    ieee1905_1::eMessageType::ACK_MESSAGE);

                    // Check if reboot/reset is already in progress
                    if (reboot_in_progress) {
                        LOG(WARNING) << "Factory Reset already in progress.";
                    }

                    // Set flag to indicate reboot/reset is in progress
                    reboot_in_progress = true;
                    //Trigger the handle_reboot_and_factory_reset to perform the FR and Reboot.
                    if (!airties_instance.handle_reboot_and_factory_reset(reset_type_value)) {
                        LOG(ERROR) << "\n Failed to Factory Reset the device";
                        // Clear flag if factory reset failed
                        reboot_in_progress = false;
                        break;
                    }
                }
                return true;
            }
            default: {
                LOG(DEBUG) << "No action";
            } break;
            }
        } break;
        default: {
            LOG(DEBUG) << "Not an Airties Reboot TLV ";
        } break;
        }
    }
    AirtiesUtils::airties_vs_tlv_list.clear();
    return true;
}
