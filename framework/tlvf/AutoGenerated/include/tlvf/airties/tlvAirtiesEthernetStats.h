///////////////////////////////////////
// AUTO GENERATED FILE - DO NOT EDIT //
///////////////////////////////////////

/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _TLVF_AIRTIES_TLVAIRTIESETHERNETSTATS_H_
#define _TLVF_AIRTIES_TLVAIRTIESETHERNETSTATS_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include <string.h>
#include <memory>
#include <tlvf/BaseClass.h>
#include <tlvf/ClassList.h>
#include <tuple>
#include <vector>
#include "tlvf/ieee_1905_1/sVendorOUI.h"
#include "tlvf/airties/eAirtiesTlvTypeMap.h"

namespace airties {

class cPortList;
class tlvAirtiesEthernetStatsallcntr;
class cPortList_ext;

class tlvAirtiesEthernetStats : public BaseClass
{
    public:
        tlvAirtiesEthernetStats(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesEthernetStats(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesEthernetStats();

        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        uint16_t& tlv_id();
        uint16_t& supported_extra_stats();
        uint8_t& num_of_ports();
        std::tuple<bool, cPortList&> port_list(size_t idx);
        std::shared_ptr<cPortList> create_port_list();
        bool add_port_list(std::shared_ptr<cPortList> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
        uint16_t* m_tlv_id = nullptr;
        uint16_t* m_supported_extra_stats = nullptr;
        uint8_t* m_num_of_ports = nullptr;
        cPortList* m_port_list = nullptr;
        size_t m_port_list_idx__ = 0;
        std::vector<std::shared_ptr<cPortList>> m_port_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cPortList : public BaseClass
{
    public:
        cPortList(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cPortList(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cPortList();

        uint8_t& port_id();
        uint8_t* bytes_sent(size_t idx = 0);
        bool set_bytes_sent(const void* buffer, size_t size);
        uint8_t* bytes_recvd(size_t idx = 0);
        bool set_bytes_recvd(const void* buffer, size_t size);
        uint8_t* packets_sent(size_t idx = 0);
        bool set_packets_sent(const void* buffer, size_t size);
        uint8_t* packets_recvd(size_t idx = 0);
        bool set_packets_recvd(const void* buffer, size_t size);
        uint8_t* tx_pkt_errors(size_t idx = 0);
        bool set_tx_pkt_errors(const void* buffer, size_t size);
        uint8_t* rx_pkt_errors(size_t idx = 0);
        bool set_rx_pkt_errors(const void* buffer, size_t size);
        uint8_t* bcast_pkts_sent(size_t idx = 0);
        bool set_bcast_pkts_sent(const void* buffer, size_t size);
        uint8_t* bcast_pkts_recvd(size_t idx = 0);
        bool set_bcast_pkts_recvd(const void* buffer, size_t size);
        uint8_t* mcast_pkts_sent(size_t idx = 0);
        bool set_mcast_pkts_sent(const void* buffer, size_t size);
        uint8_t* mcast_pkts_recvd(size_t idx = 0);
        bool set_mcast_pkts_recvd(const void* buffer, size_t size);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint8_t* m_port_id = nullptr;
        uint8_t* m_bytes_sent = nullptr;
        size_t m_bytes_sent_idx__ = 0;
        int m_lock_order_counter__ = 0;
        uint8_t* m_bytes_recvd = nullptr;
        size_t m_bytes_recvd_idx__ = 0;
        uint8_t* m_packets_sent = nullptr;
        size_t m_packets_sent_idx__ = 0;
        uint8_t* m_packets_recvd = nullptr;
        size_t m_packets_recvd_idx__ = 0;
        uint8_t* m_tx_pkt_errors = nullptr;
        size_t m_tx_pkt_errors_idx__ = 0;
        uint8_t* m_rx_pkt_errors = nullptr;
        size_t m_rx_pkt_errors_idx__ = 0;
        uint8_t* m_bcast_pkts_sent = nullptr;
        size_t m_bcast_pkts_sent_idx__ = 0;
        uint8_t* m_bcast_pkts_recvd = nullptr;
        size_t m_bcast_pkts_recvd_idx__ = 0;
        uint8_t* m_mcast_pkts_sent = nullptr;
        size_t m_mcast_pkts_sent_idx__ = 0;
        uint8_t* m_mcast_pkts_recvd = nullptr;
        size_t m_mcast_pkts_recvd_idx__ = 0;
};

class tlvAirtiesEthernetStatsallcntr : public BaseClass
{
    public:
        tlvAirtiesEthernetStatsallcntr(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit tlvAirtiesEthernetStatsallcntr(std::shared_ptr<BaseClass> base, bool parse = false);
        ~tlvAirtiesEthernetStatsallcntr();

        const eAirtiesTlvTypeMap& type();
        const uint16_t& length();
        sVendorOUI& vendor_oui();
        uint16_t& tlv_id();
        uint16_t& supported_extra_stats();
        uint8_t& num_of_ports();
        std::tuple<bool, cPortList_ext&> port_list(size_t idx);
        std::shared_ptr<cPortList_ext> create_port_list();
        bool add_port_list(std::shared_ptr<cPortList_ext> ptr);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        eAirtiesTlvTypeMap* m_type = nullptr;
        uint16_t* m_length = nullptr;
        sVendorOUI* m_vendor_oui = nullptr;
        uint16_t* m_tlv_id = nullptr;
        uint16_t* m_supported_extra_stats = nullptr;
        uint8_t* m_num_of_ports = nullptr;
        cPortList_ext* m_port_list = nullptr;
        size_t m_port_list_idx__ = 0;
        std::vector<std::shared_ptr<cPortList_ext>> m_port_list_vector;
        bool m_lock_allocation__ = false;
        int m_lock_order_counter__ = 0;
};

class cPortList_ext : public BaseClass
{
    public:
        cPortList_ext(uint8_t* buff, size_t buff_len, bool parse = false);
        explicit cPortList_ext(std::shared_ptr<BaseClass> base, bool parse = false);
        ~cPortList_ext();

        uint8_t& port_id();
        uint8_t* bytes_sent(size_t idx = 0);
        bool set_bytes_sent(const void* buffer, size_t size);
        uint8_t* bytes_recvd(size_t idx = 0);
        bool set_bytes_recvd(const void* buffer, size_t size);
        uint8_t* packets_sent(size_t idx = 0);
        bool set_packets_sent(const void* buffer, size_t size);
        uint8_t* packets_recvd(size_t idx = 0);
        bool set_packets_recvd(const void* buffer, size_t size);
        uint8_t* tx_pkt_errors(size_t idx = 0);
        bool set_tx_pkt_errors(const void* buffer, size_t size);
        uint8_t* rx_pkt_errors(size_t idx = 0);
        bool set_rx_pkt_errors(const void* buffer, size_t size);
        uint8_t* bcast_bytes_sent(size_t idx = 0);
        bool set_bcast_bytes_sent(const void* buffer, size_t size);
        uint8_t* bcast_bytes_recvd(size_t idx = 0);
        bool set_bcast_bytes_recvd(const void* buffer, size_t size);
        uint8_t* bcast_pkts_sent(size_t idx = 0);
        bool set_bcast_pkts_sent(const void* buffer, size_t size);
        uint8_t* bcast_pkts_recvd(size_t idx = 0);
        bool set_bcast_pkts_recvd(const void* buffer, size_t size);
        uint8_t* mcast_bytes_sent(size_t idx = 0);
        bool set_mcast_bytes_sent(const void* buffer, size_t size);
        uint8_t* mcast_bytes_recvd(size_t idx = 0);
        bool set_mcast_bytes_recvd(const void* buffer, size_t size);
        uint8_t* mcast_pkts_sent(size_t idx = 0);
        bool set_mcast_pkts_sent(const void* buffer, size_t size);
        uint8_t* mcast_pkts_recvd(size_t idx = 0);
        bool set_mcast_pkts_recvd(const void* buffer, size_t size);
        void class_swap() override;
        bool finalize() override;
        static size_t get_initial_size();

    private:
        bool init();
        uint8_t* m_port_id = nullptr;
        uint8_t* m_bytes_sent = nullptr;
        size_t m_bytes_sent_idx__ = 0;
        int m_lock_order_counter__ = 0;
        uint8_t* m_bytes_recvd = nullptr;
        size_t m_bytes_recvd_idx__ = 0;
        uint8_t* m_packets_sent = nullptr;
        size_t m_packets_sent_idx__ = 0;
        uint8_t* m_packets_recvd = nullptr;
        size_t m_packets_recvd_idx__ = 0;
        uint8_t* m_tx_pkt_errors = nullptr;
        size_t m_tx_pkt_errors_idx__ = 0;
        uint8_t* m_rx_pkt_errors = nullptr;
        size_t m_rx_pkt_errors_idx__ = 0;
        uint8_t* m_bcast_bytes_sent = nullptr;
        size_t m_bcast_bytes_sent_idx__ = 0;
        uint8_t* m_bcast_bytes_recvd = nullptr;
        size_t m_bcast_bytes_recvd_idx__ = 0;
        uint8_t* m_bcast_pkts_sent = nullptr;
        size_t m_bcast_pkts_sent_idx__ = 0;
        uint8_t* m_bcast_pkts_recvd = nullptr;
        size_t m_bcast_pkts_recvd_idx__ = 0;
        uint8_t* m_mcast_bytes_sent = nullptr;
        size_t m_mcast_bytes_sent_idx__ = 0;
        uint8_t* m_mcast_bytes_recvd = nullptr;
        size_t m_mcast_bytes_recvd_idx__ = 0;
        uint8_t* m_mcast_pkts_sent = nullptr;
        size_t m_mcast_pkts_sent_idx__ = 0;
        uint8_t* m_mcast_pkts_recvd = nullptr;
        size_t m_mcast_pkts_recvd_idx__ = 0;
};

}; // close namespace: airties

#endif //_TLVF/AIRTIES_TLVAIRTIESETHERNETSTATS_H_
