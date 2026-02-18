/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _TLVF_WSC_M2_H_
#define _TLVF_WSC_M2_H_

#include <tlvf/WSC/WscAttrList.h>

namespace WSC {
namespace vendor_extension {
namespace airties {

constexpr uint8_t VENDOR_HIDE_SSID = 0x80;
constexpr uint8_t VENDOR_BSS_CFG   = 0x02;
constexpr uint8_t VENDOR_VAP_TYPE  = 0x00;
constexpr uint8_t VENDOR_VAP_LABEL = 0x01;

} // namespace airties
} // namespace vendor_extension

/**
 * @class WSC::m2
 * @brief Builder/parser for the WSC **M2** attribute list carried inside an IEEE 1905.1 WSC TLV.
 *
 * Purpose:
 *  - **Create mode** (`parse==false`): serializes a complete M2 message from `m2::config`
 *    into the provided buffer (adds all mandatory WSC attributes, optional BSS index,
 *    and the WFA Vendor Extension: Version2). The class is finalized in network byte order.
 *  - **Parse mode** (`parse==true`): validates and exposes fields of an incoming M2.
 *
 * Notes
 *  - This class does **not** encrypt ConfigData. Provide `cfg.iv` and `cfg.encrypted_settings`
 *    (produced by the KeyWrap stage) before calling `create()`.
 */
class m2 : public WscAttrList {
public:
    // WSC M2 configuration (inputs to m2::init/create)
    struct config {
        /* WSC message type; must be WSC_MSG_TYPE_M2 */
        eWscMessageType msg_type{eWscMessageType::WSC_MSG_TYPE_M2};

        /* Enrollee/Registrar nonces (from M1 / freshly generated) */
        uint8_t enrollee_nonce[WSC_NONCE_LENGTH]{};
        uint8_t registrar_nonce[WSC_NONCE_LENGTH]{};

        /* Registrar public key (DH) */
        uint8_t pub_key[WSC_PUBLIC_KEY_LENGTH]{};

        /* Encryption/Auth type flags advertised in M2 */
        uint16_t encr_type_flags{0};
        eWscAuth auth_type_flags{eWscAuth::WSC_AUTH_OPEN};

        /* Device identity block shown in M2 */
        std::string manufacturer{};
        std::string model_name{};
        std::string model_number{};
        std::string serial_number{};
        uint16_t primary_dev_type_id{0};
        std::string device_name{};

        /* RF bands capability advertised in M2 */
        eWscRfBands bands{WSC_RF_BAND_2GHZ_5GHZ};

        /* WSC Encrypted Settings payload (ConfigData ciphertext) and IV */
        std::vector<uint8_t> encrypted_settings{};
        uint8_t iv[WSC_ENCRYPTED_SETTINGS_IV_LENGTH]{};

        /* Hidden SSID flag (Airties Vendor Extension subelement: hidden_ssid) */
        eWscVendorExtHiddenSsid hidden_ssid = WSC::eWscVendorExtHiddenSsid::UNSET;

        /* WSC VAP Type */
        eVapType vap_type = eVapType::OTHER;

        /* WSC VAP Label */
        std::string vap_label{};
    };

    m2(uint8_t *buff, size_t buff_len, bool parse) : WscAttrList(buff, buff_len, parse) {}
    virtual ~m2() = default;

    bool init(const config &cfg);
    bool init() { return WscAttrList::init(); };
    bool valid() const override;
    static std::shared_ptr<m2> create(ieee1905_1::tlvWsc &tlv, const config &cfg);
    static std::shared_ptr<m2> parse(ieee1905_1::tlvWsc &tlv);

    // getters
    eWscMessageType msg_type() const { return getAttr<cWscAttrMessageType>()->msg_type(); };
    std::string manufacturer() const
    {
        return getAttr<cWscAttrManufacturer>()->manufacturer_str();
    };
    std::string model_name() const { return getAttr<cWscAttrModelName>()->model_str(); };
    std::string device_name() const { return getAttr<cWscAttrDeviceName>()->device_name_str(); };
    std::string serial_number() const
    {
        return getAttr<cWscAttrSerialNumber>()->serial_number_str();
    };
    uint16_t encr_type_flags() const
    {
        return getAttr<cWscAttrEncryptionTypeFlags>()->encr_type_flags();
    };
    uint16_t auth_type_flags() const
    {
        return getAttr<cWscAttrAuthenticationTypeFlags>()->auth_type_flags();
    };
    uint8_t *enrollee_nonce() { return getAttr<cWscAttrEnrolleeNonce>()->nonce(); };
    uint8_t *public_key() { return getAttr<cWscAttrPublicKey>()->public_key(); };
    uint16_t rf_bands() const { return getAttr<cWscAttrRfBands>()->bands(); };

    uint8_t *authenticator() { return getAttr<cWscAttrAuthenticator>()->data(); };
    uint8_t *registrar_nonce() { return getAttr<cWscAttrRegistrarNonce>()->nonce(); };
    cWscAttrEncryptedSettings &encrypted_settings()
    {
        return *getAttr<cWscAttrEncryptedSettings>();
    };
    eWscVendorExtHiddenSsid hidden_ssid() const
    {
        for (auto &vendor_ext_attr : getAttrList<WSC::cWscAttrVendorExtension>()) {
            if ((WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_1 != vendor_ext_attr->vendor_id_0()) ||
                (WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_2 != vendor_ext_attr->vendor_id_1()) ||
                (WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_3 != vendor_ext_attr->vendor_id_2())) {
                continue;
            }

            auto vendor_data = vendor_ext_attr->vendor_data();
            if (vendor_data[0] == WSC::vendor_extension::airties::VENDOR_BSS_CFG) {

                // Hidden BSS attribute is set
                return (vendor_data[1] == WSC::vendor_extension::airties::VENDOR_HIDE_SSID)
                           ? eWscVendorExtHiddenSsid::ENABLED
                           : eWscVendorExtHiddenSsid::DISABLED;
            }
        }
        return eWscVendorExtHiddenSsid::UNSET;
    };
    eVapType vap_type() const
    {
        for (auto &vendor_ext_attr : getAttrList<WSC::cWscAttrVendorExtension>()) {
            if ((WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_1 != vendor_ext_attr->vendor_id_0()) ||
                (WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_2 != vendor_ext_attr->vendor_id_1()) ||
                (WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_3 != vendor_ext_attr->vendor_id_2())) {
                continue;
            }

            const auto *data = vendor_ext_attr->vendor_data();
            const size_t len = vendor_ext_attr->vendor_data_length();

            // Need at least 2 bytes: [subelem_id][value]
            if (len >= 2 && data[0] == WSC::vendor_extension::airties::VENDOR_VAP_TYPE) {

                const uint8_t raw = data[1];

                if (!eVapTypeValidate::check(raw)) {
                    LOG(WARNING) << "Invalid VAP type received in vendor extension: " << int(raw)
                                 << " -> using OTHER";
                    return eVapType::OTHER;
                }

                return static_cast<eVapType>(raw);
            }
        }

        return eVapType::OTHER;
    }

    std::string vap_label() const
    {
        for (auto &vendor_ext_attr : getAttrList<WSC::cWscAttrVendorExtension>()) {
            if ((WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_1 != vendor_ext_attr->vendor_id_0()) ||
                (WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_2 != vendor_ext_attr->vendor_id_1()) ||
                (WSC::eWscVendorId::WSC_VENDOR_ID_AIRTIES_3 != vendor_ext_attr->vendor_id_2())) {
                continue;
            }

            const auto *data = vendor_ext_attr->vendor_data();
            const size_t len = vendor_ext_attr->vendor_data_length();

            // Need at least 2 bytes: [subelem_id][value]
            if (len >= 2 && data[0] == WSC::vendor_extension::airties::VENDOR_VAP_LABEL) {
                return std::string(reinterpret_cast<const char *>(data + 1), len - 1);
            }
        }

        return {};
    }
};

} // namespace WSC

#endif // _TLVF_WSC_M2_H_
