/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "higher_layer_collection_task.h"

#include "../../agent_db.h"

#include <bcl/network/network_utils.h>
#include <tlvf/ieee_1905_1/e1905ProfileVersion.h>
#include <tlvf/ieee_1905_1/eIpv4AddressType.h>
#include <tlvf/ieee_1905_1/eIpv6AddressType.h>
#include <tlvf/ieee_1905_1/tlv1905ProfileVersion.h>
#include <tlvf/ieee_1905_1/tlvAlMacAddress.h>
#include <tlvf/ieee_1905_1/tlvControlUrl.h>
#include <tlvf/ieee_1905_1/tlvDeviceIdentification.h>
#include <tlvf/ieee_1905_1/tlvIpv4.h>
#include <tlvf/ieee_1905_1/tlvIpv6.h>
#include <tlvf/tlvftypes.h>
#include <tlvf/wfa_map/tlvHigherLayerData.h>

#include <arpa/inet.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <iostream>

namespace beerocks {

namespace {

/**
 * @brief Mock implementation of InterfaceStatusProvider for testing purposes.
 * Allows manual injection of network interface states to simulate kernel
 * responses without actual Netlink calls.
 */
class FakeInterfaceStatusProvider : public HigherLayerCollectionTask::InterfaceStatusProvider {
public:
    using sStatus = HigherLayerCollectionTask::sInterfaceNetworkStatus;

    /**
     * @brief Fills the provided map with pre-configured interface data.
     * @param[out] interfaces_network_status Map to be populated with fake data.
     * @return true always, simulating a successful data fetch.
     */
    bool refresh(std::unordered_map<std::string, sStatus> &interfaces_network_status) override
    {
        interfaces_network_status = interfaces;
        refresh_called            = true;
        return true;
    }

    std::unordered_map<std::string, sStatus> interfaces; ///< Map of fake interfaces.
    bool refresh_called = false;                         ///< Flag to verify if refresh was invoked.
};

/**
 * @brief Helper function to construct a valid 1905.1 Higher Layer Query CMDU.
 * @param[out] buffer Destination buffer for the CMDU.
 * @param size Buffer size.
 * @param[out] cmdu_rx Reference to the RX CMDU object to be populated.
 * @return true if creation and parsing were successful, false otherwise.
 */
bool make_query_cmdu(uint8_t *buffer, size_t size, ieee1905_1::CmduMessageRx &cmdu_rx)
{
    ieee1905_1::CmduMessageTx query_cmdu_tx(buffer, size);
    if (!query_cmdu_tx.create(0, ieee1905_1::eMessageType::HIGHER_LAYER_QUERY_MESSAGE)) {
        return false;
    }

    if (!query_cmdu_tx.addClass<wfa_map::tlvHigherLayerData>()) {
        return false;
    }

    return cmdu_rx.parse();
}

} // namespace

/**
 * @brief Test fixture for HigherLayerCollectionTask logic verification.
 */
class HigherLayerCollectionTaskTest : public ::testing::Test {
protected:
    using sStatus = HigherLayerCollectionTask::sInterfaceNetworkStatus;

    uint8_t m_cmdu_rx_buffer[1500] = {};               ///< Raw buffer for incoming CMDU simulation.
    uint8_t m_cmdu_tx_buffer[1500] = {};               ///< Raw buffer for outgoing CMDU simulation.
    ieee1905_1::CmduMessageTx m_cmdu_tx;               ///< TX CMDU handler.
    std::unique_ptr<HigherLayerCollectionTask> m_task; ///< Task under test.
    sMacAddr m_bridge_mac = tlvf::mac_from_string("aa:bb:cc:dd:ee:ff"); ///< Mock bridge MAC.

    FakeInterfaceStatusProvider *m_provider = nullptr; ///< Pointer to the fake data provider.
    bool m_send_called                      = false;   ///< Flag to verify response sending.

    /**
     * @brief Initializes the task and mocks required lambda dependencies.
     */
    HigherLayerCollectionTaskTest() : m_cmdu_tx(m_cmdu_tx_buffer, sizeof(m_cmdu_tx_buffer))
    {
        auto provider = std::make_unique<FakeInterfaceStatusProvider>();
        m_provider    = provider.get();

        m_task = std::make_unique<HigherLayerCollectionTask>(
            [this](const sMacAddr &, ieee1905_1::CmduMessageTx &cmdu) {
                if (!cmdu.finalize()) {
                    return false;
                }
                m_send_called = true;
                return true;
            },
            [this]() { return m_bridge_mac; }, std::move(provider), m_cmdu_tx);
    }

    /**
     * @brief Prepares a fake "lo" interface with IPv4/IPv6 addresses for tests.
     */
    void SetUp() override
    {
        auto db = AgentDB::get();
        db->device_conf.device_friendly_name.clear();
        db->device_conf.device_manufacturer.clear();
        db->device_conf.device_model_name.clear();

        sStatus status{};
        status.mac_address = tlvf::mac_from_string("00:11:22:33:44:55");

        sStatus::sIpv4Entry ipv4{};
        ipv4.ipv4_address_type = ieee1905_1::eIpv4AddressType::DHCP;
        ipv4.ipv4_address      = net::network_utils::uint_ipv4_from_string("192.168.1.10");
        ipv4.ipv4_dhcp_server  = net::network_utils::uint_ipv4_from_string("192.168.1.1");
        status.ipv4_list.push_back(ipv4);

        sStatus::sIpv6Entry ipv6{};
        ipv6.ipv6_address_type = ieee1905_1::eIpv6AddressType::STATIC;
        ASSERT_EQ(inet_pton(AF_INET6, "fe80::1", status.ipv6_link_local), 1);
        ASSERT_EQ(inet_pton(AF_INET6, "2001:db8::10", ipv6.ipv6_address), 1);
        ASSERT_EQ(inet_pton(AF_INET6, "2001:db8::1", ipv6.ipv6_origin), 1);
        status.ipv6_list.push_back(ipv6);

        m_provider->interfaces["lo"] = status;
    }
};

/**
 * @test Verifies that receiving a HIGHER_LAYER_QUERY generates a valid RESPONSE with all required TLVs.
 */
TEST_F(HigherLayerCollectionTaskTest, handle_higher_layer_query_generates_response)
{
    ieee1905_1::CmduMessageRx cmdu_rx(m_cmdu_rx_buffer, sizeof(m_cmdu_rx_buffer));
    ASSERT_TRUE(make_query_cmdu(m_cmdu_rx_buffer, sizeof(m_cmdu_rx_buffer), cmdu_rx));

    ASSERT_TRUE(
        m_task->handle_higher_layer_query(tlvf::mac_from_string("11:22:33:44:55:66"), cmdu_rx));

    EXPECT_TRUE(m_send_called);
    EXPECT_TRUE(m_provider->refresh_called);

    // Parse the generated response
    ieee1905_1::CmduMessageRx response_rx(m_cmdu_tx_buffer, sizeof(m_cmdu_tx_buffer));
    ASSERT_TRUE(response_rx.parse());
    EXPECT_EQ(response_rx.getMessageType(),
              ieee1905_1::eMessageType::HIGHER_LAYER_RESPONSE_MESSAGE);

    // Verify Device Identification TLV
    auto tlv_dev_id = response_rx.getClass<ieee1905_1::tlvDeviceIdentification>();
    ASSERT_NE(tlv_dev_id, nullptr);
    EXPECT_STREQ(reinterpret_cast<char *>(tlv_dev_id->manufacturer_name()), "");

    // Verify IPv4 TLV and contents
    auto tlv_ipv4 = response_rx.getClass<ieee1905_1::tlvIpv4>();
    ASSERT_NE(tlv_ipv4, nullptr);
    ASSERT_GT(tlv_ipv4->number_of_entries(), 0);

    auto ipv4_block = tlv_ipv4->ipv4_interfaces_list(0);
    ASSERT_TRUE(std::get<0>(ipv4_block));
    ASSERT_GT(std::get<1>(ipv4_block).number_of_ipv4_addresses(), 0);

    auto ipv4_entry = std::get<1>(ipv4_block).ipv4_address_entries(0);
    ASSERT_TRUE(std::get<0>(ipv4_entry));
    EXPECT_EQ(net::network_utils::ipv4_to_string(std::get<1>(ipv4_entry).ipv4_dhcp_server),
              "192.168.1.1");

    // Verify IPv6 TLV and Link-Local address
    auto tlv_ipv6 = response_rx.getClass<ieee1905_1::tlvIpv6>();
    ASSERT_NE(tlv_ipv6, nullptr);
    ASSERT_GT(tlv_ipv6->number_of_entries(), 0);

    auto ipv6_block = tlv_ipv6->ipv6_interfaces_list(0);
    ASSERT_TRUE(std::get<0>(ipv6_block));

    uint8_t expected_ll[16] = {};
    ASSERT_EQ(inet_pton(AF_INET6, "fe80::1", expected_ll), 1);
    EXPECT_EQ(std::memcmp(std::get<1>(ipv6_block).ipv6_link_local_address(), expected_ll, 16), 0);
}

} // namespace beerocks
