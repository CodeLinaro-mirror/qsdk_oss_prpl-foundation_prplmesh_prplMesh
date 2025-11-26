/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _NET_STRUCT_
#define _NET_STRUCT_

#include <cstring>
#include <stdint.h>
#include <string>

#include <tlvf/tlvftypes.h>

namespace beerocks {
namespace net {

enum eNetworkStructsConsts {
    MAC_ADDR_LEN = 6,
    IP_ADDR_LEN  = 4,
};

typedef struct sIpv4Addr {
    uint8_t oct[IP_ADDR_LEN];
    void struct_swap() {}
    void struct_init() { std::memset(oct, 0, IP_ADDR_LEN); }
    bool operator==(sIpv4Addr const &rhs) const
    {
        return (0 == std::memcmp(this->oct, rhs.oct, IP_ADDR_LEN));
    }
    bool operator!=(sIpv4Addr const &rhs) const { return !(rhs == *this); }
} __attribute__((packed)) sIpv4Addr;

typedef struct sScanResult {
    sMacAddr mac;
    uint8_t channel;
    int8_t rssi;
    void struct_swap() {}
    void struct_init()
    {
        mac.struct_init();
        channel = 0;
        rssi    = 0;
    }
    bool operator==(sScanResult const &rhs) const
    {
        return (channel == rhs.channel && rssi == rhs.rssi && mac == rhs.mac);
    }
    bool operator!=(sScanResult const &rhs) const { return !(rhs == *this); }
} __attribute__((packed)) sScanResult;

/**
 * @brief Interface statistics.
 *
 * Information in this structure is obtained from IFLA_STATS attribute of a rtnetlink message
 * through a Rtnetlink socket.
 */
struct sInterfaceStats {
    /**
     * Total bytes transmitted.
     */
    uint32_t tx_bytes = 0;

    /**
     * Packet transmit problems.
     */
    uint32_t tx_errors = 0;

    /**
     * Total packets transmitted.
     */
    uint32_t tx_packets = 0;

    /**
     * Total bytes received.
     */
    uint32_t rx_bytes = 0;

    /**
     * Bad packets received.
     */
    uint32_t rx_errors = 0;

    /**
     * Total packets received.
     */
    uint32_t rx_packets = 0;
};

typedef struct sHTCapabilities {
#if defined(__LITTLE_ENDIAN_BITFIELD)
    uint8_t reserved : 1;
    uint8_t ht_support_40mhz : 1;
    uint8_t short_gi_support_40mhz : 1;
    uint8_t short_gi_support_20mhz : 1;
    uint8_t max_num_of_supported_rx_spatial_streams : 2;
    uint8_t max_num_of_supported_tx_spatial_streams : 2;
#elif defined(__BIG_ENDIAN_BITFIELD)
    uint8_t max_num_of_supported_tx_spatial_streams : 2;
    uint8_t max_num_of_supported_rx_spatial_streams : 2;
    uint8_t short_gi_support_20mhz : 1;
    uint8_t short_gi_support_40mhz : 1;
    uint8_t ht_support_40mhz : 1;
    uint8_t reserved : 1;
#else
#error "Bitfield macros are not defined"
#endif
    void struct_swap() {}
    void struct_init() {}
} __attribute__((packed)) sHTCapabilities;

typedef struct sVHTCapabilities {
#if defined(__LITTLE_ENDIAN_BITFIELD)
    uint16_t reserved : 4;
    uint16_t mu_beamformer_capable : 1;
    uint16_t su_beamformer_capable : 1;
    uint16_t vht_support_160mhz : 1;
    uint16_t vht_support_80_80mhz : 1;
    uint16_t short_gi_support_160mhz_and_80_80mhz : 1;
    uint16_t short_gi_support_80mhz : 1;
    uint16_t max_num_of_supported_rx_spatial_streams : 3;
    uint16_t max_num_of_supported_tx_spatial_streams : 3;
#elif defined(__BIG_ENDIAN_BITFIELD)
    uint16_t max_num_of_supported_tx_spatial_streams : 3;
    uint16_t max_num_of_supported_rx_spatial_streams : 3;
    uint16_t short_gi_support_80mhz : 1;
    uint16_t short_gi_support_160mhz_and_80_80mhz : 1;
    uint16_t vht_support_80_80mhz : 1;
    uint16_t vht_support_160mhz : 1;
    uint16_t su_beamformer_capable : 1;
    uint16_t mu_beamformer_capable : 1;
    uint16_t reserved : 4;
#else
#error "Bitfield macros are not defined"
#endif
    void struct_swap() {}
    void struct_init() {}
} __attribute__((packed)) sVHTCapabilities;

typedef struct sHECapabilities {
#if defined(__LITTLE_ENDIAN_BITFIELD)
    uint16_t reserved : 1;
    uint16_t dl_ofdm_capable : 1;
    uint16_t ul_ofdm_capable : 1;
    uint16_t dl_mu_mimo_and_ofdm_capable : 1;
    uint16_t ul_mu_mimo_and_ofdm_capable : 1;
    uint16_t ul_mu_mimo_capable : 1;
    uint16_t mu_beamformer_capable : 1;
    uint16_t su_beamformer_capable : 1;
    uint16_t he_support_160mhz : 1;
    uint16_t he_support_80_80mhz : 1;
    uint16_t max_num_of_supported_rx_spatial_streams : 3;
    uint16_t max_num_of_supported_tx_spatial_streams : 3;
#elif defined(__BIG_ENDIAN_BITFIELD)
    uint16_t max_num_of_supported_tx_spatial_streams : 3;
    uint16_t max_num_of_supported_rx_spatial_streams : 3;
    uint16_t he_support_80_80mhz : 1;
    uint16_t he_support_160mhz : 1;
    uint16_t su_beamformer_capable : 1;
    uint16_t mu_beamformer_capable : 1;
    uint16_t ul_mu_mimo_capable : 1;
    uint16_t ul_mu_mimo_and_ofdm_capable : 1;
    uint16_t dl_mu_mimo_and_ofdm_capable : 1;
    uint16_t ul_ofdm_capable : 1;
    uint16_t dl_ofdm_capable : 1;
    uint16_t reserved : 1;
#else
#error "Bitfield macros are not defined"
#endif
    void struct_swap() {}
    void struct_init() {}
} __attribute__((packed)) sHECapabilities;

typedef struct sWIFI6Capabilities {
#if defined(__LITTLE_ENDIAN_BITFIELD)
    uint64_t anticipated_channel_usage : 1;
    uint64_t spatial_reuse : 1;
    uint64_t twt_responder : 1;
    uint64_t twt_requester : 1;
    uint64_t mu_edca : 1;
    uint64_t multi_bssid : 1;
    uint64_t mu_rts : 1;
    uint64_t rts : 1;
    uint64_t max_ul_ofdma_rx : 8;
    uint64_t max_dl_ofdma_tx : 8;
    uint64_t max_ul_mu_mimo_rx : 4;
    uint64_t max_dl_mu_mimo_tx : 4;
    uint64_t dl_ofdma : 1;
    uint64_t ul_ofdma : 1;
    uint64_t ul_mu_mimo : 1;
    uint64_t beamformee_sts_greater_80mhz : 1;
    uint64_t beamformee_sts_less_80mhz : 1;
    uint64_t mu_Beamformer_status : 1;
    uint64_t su_beamformee : 1;
    uint64_t su_beamformer : 1;
    uint64_t mcs_nss_length : 4;
    uint64_t he_support_80_80mhz : 1;
    uint64_t he_support_160mhz : 1;
    uint64_t agent_role : 2;
#elif defined(__BIG_ENDIAN_BITFIELD)
    uint64_t agent_role : 2;
    uint64_t he_support_160mhz : 1;
    uint64_t he_support_80_80mhz : 1;
    uint64_t mcs_nss_length : 4;
    uint64_t su_beamformer : 1;
    uint64_t su_beamformee : 1;
    uint64_t mu_Beamformer_status : 1;
    uint64_t beamformee_sts_less_80mhz : 1;
    uint64_t beamformee_sts_greater_80mhz : 1;
    uint64_t ul_mu_mimo : 1;
    uint64_t ul_ofdma : 1;
    uint64_t dl_ofdma : 1;
    uint64_t max_dl_mu_mimo_tx : 4;
    uint64_t max_ul_mu_mimo_rx : 4;
    uint64_t max_dl_ofdma_tx : 8;
    uint64_t max_ul_ofdma_rx : 8;
    uint64_t rts : 1;
    uint64_t mu_rts : 1;
    uint64_t multi_bssid : 1;
    uint64_t mu_edca : 1;
    uint64_t twt_requester : 1;
    uint64_t twt_responder : 1;
    uint64_t spatial_reuse : 1;
    uint64_t anticipated_channel_usage : 1;
#else
#error "Bitfield macros are not defined"
#endif
    void struct_swap() {}
    void struct_init() {}
} __attribute__((packed)) sWIFI6Capabilities;

typedef struct sEHTOperations {
#if defined(__LITTLE_ENDIAN_BITFIELD)
    typedef struct sEHTOperationInformations {
        uint16_t disabled_subchannel_bitmap;
        uint8_t ccfs1;
        uint8_t ccfs0;
        uint8_t control;
    } __attribute__((packed)) sEHTOperationInformations;
    sEHTOperationInformations operation_informations;

    uint32_t basic_eht_mcs_and_nss_set;

    typedef struct sEHTOperationParameters {
        uint8_t reserved : 2;
        uint8_t group_addressed_bu_indication_exponent : 2;
        uint8_t group_addressed_bu_indication_limit : 1;
        uint8_t eht_default_pe_duration : 1;
        uint8_t disabled_subchannel_valid : 1;
        uint8_t eht_operation_information_valid : 1;
    } __attribute__((packed)) sEHTOperationParameters;
    sEHTOperationParameters operation_parameters;
#elif defined(__BIG_ENDIAN_BITFIELD)
    typedef struct sEHTOperationParameters {
        uint8_t eht_operation_information_valid : 1;
        uint8_t disabled_subchannel_valid : 1;
        uint8_t eht_default_pe_duration : 1;
        uint8_t group_addressed_bu_indication_limit : 1;
        uint8_t group_addressed_bu_indication_exponent : 2;
        uint8_t reserved : 2;
    } __attribute__((packed)) sEHTOperationParameters;
    sEHTOperationParameters operation_parameters;

    uint32_t basic_eht_mcs_and_nss_set;

    typedef struct sEHTOperationInformations {
        uint8_t control;
        uint8_t ccfs0;
        uint8_t ccfs1;
        uint16_t disabled_subchannel_bitmap;
    } __attribute__((packed)) sEHTOperationInformations;
    sEHTOperationInformations operation_informations;
#else
#error "Bitfield macros are not defined"
#endif
    void struct_swap() {}
    void struct_init() {}
} __attribute__((packed)) sEHTOperations;

typedef struct sApRoleCapabilities {
    bool str_support;   // Single link STR support
    bool nstr_support;  // Non-STR support
    bool emlsr_support; // Enhanced Multi-Link Single Radio support
    bool emlmr_support; // Enhanced Multi-Link Multi Radio support
} __attribute__((packed)) sApRoleCapabilities;

typedef struct sSTARoleCapabilities {
    bool str_support;   // Single link STR support
    bool nstr_support;  // Non-STR support
    bool emlsr_support; // Enhanced Multi-Link Single Radio support
    bool emlmr_support; // Enhanced Multi-Link Multi Radio support
} __attribute__((packed)) sSTARoleCapabilities;

} // namespace net
} // namespace beerocks

#endif
