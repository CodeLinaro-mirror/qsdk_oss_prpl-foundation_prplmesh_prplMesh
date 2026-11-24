/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 */
#include "../../db/station.h"
#include "../../dpp/dpp_internal.h"
#include "../../son_actions.h"
#include "../dpp_protocol_task.h"

#include <gtest/gtest.h>
#include <openssl/obj_mac.h>
#include <openssl/x509.h>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/wfa_map/tlv1905EncapDpp.h>
#include <tlvf/wfa_map/tlvDppChirpValue.h>
#include <tlvf/wfa_map/tlvDppMessage.h>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
struct TransportCapture {
    bool result    = true;
    unsigned calls = 0;
    sMacAddr destination{};
    std::vector<uint8_t> wire;
};
TransportCapture transport;
} // namespace

// This definition belongs only to dpp_protocol_task_test. The protocol task,
// crypto, database and TLV serialization remain the production implementations.
namespace son {
bool son_actions::send_cmdu_to_agent(const sMacAddr &destination,
                                     ieee1905_1::CmduMessageTx &message, db &, const std::string &)
{
    ++transport.calls;
    transport.destination = destination;
    if (!message.finalize()) {
        return false;
    }
    transport.wire.assign(message.getMessageBuff(),
                          message.getMessageBuff() + message.getMessageLength());
    return transport.result;
}
} // namespace son

namespace {
namespace dpp     = son::controller_dpp;
using MessageType = ieee1905_1::eMessageType;

class DppProtocolTaskTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        transport                         = {};
        logging_config.sLog.global_levels = "error,fatal,warning";
        logging_config.sLog.syslog_levels = "off";
        logger.reset(new beerocks::logging("dpp-protocol-task-test", logging_config.sLog));
        database.reset(
            new son::db(config, *logger, tlvf::mac_from_string("02:00:00:00:00:01"), nullptr));
        protocol.reset(new son::dpp_protocol_task(*database, outgoing));
        auto agent = database->m_agents.add(agent_mac);
        ASSERT_NE(nullptr, agent);
        agent->dpp_onboarding_support = true;
    }

    void insert_bootstrap(bool onboarded = false)
    {
        dpp::DppKeyPtr key;
        ASSERT_TRUE(dpp::generate_ec_key(NID_X9_62_prime256v1, key));
        const int length = i2d_PUBKEY(key.get(), nullptr);
        ASSERT_GT(length, 0);
        std::vector<uint8_t> der(static_cast<size_t>(length));
        auto *ptr = der.data();
        ASSERT_EQ(length, i2d_PUBKEY(key.get(), &ptr));
        son::db::sDppBootstrappingInfo info;
        std::string error;
        ASSERT_TRUE(database->parse_dpp_bootstrap_info(
            "DPP:M:020000000002;K:" + dpp::base64url_encode(der.data(), der.size()) + ";;", info,
            error))
            << error;
        info.alias     = "protocol-test";
        info.onboarded = onboarded;
        ASSERT_TRUE(database->add_dpp_bootstrap_info(std::move(info), error)) << error;
    }

    void dispatch(bool expected)
    {
        ASSERT_TRUE(incoming.finalize());
        ieee1905_1::CmduMessageRx rx(incoming_buffer.data(), incoming.getMessageLength());
        ASSERT_TRUE(rx.parse());
        EXPECT_EQ(expected, protocol->handle_ieee1905_1_msg(agent_mac, rx));
    }

    void add_chirp(bool valid = true)
    {
        ASSERT_TRUE(incoming.create(1, MessageType::CHIRP_NOTIFICATION_MESSAGE));
        auto chirp = incoming.addClass<wfa_map::tlvDppChirpValue>();
        ASSERT_NE(nullptr, chirp);
        chirp->flags().hash_validity = valid;
        const auto *info             = database->get_dpp_bootstrap_info("protocol-test");
        ASSERT_NE(nullptr, info);
        ASSERT_TRUE(chirp->set_hash(info->chirp_hash.data(), info->chirp_hash.size()));
    }

    beerocks::config_file::sConfigMaster logging_config{};
    son::db::sDbMasterConfig config{};
    std::unique_ptr<beerocks::logging> logger;
    std::unique_ptr<son::db> database;
    std::array<uint8_t, 4096> outgoing_buffer{}, incoming_buffer{};
    ieee1905_1::CmduMessageTx outgoing{outgoing_buffer.data(), outgoing_buffer.size()};
    ieee1905_1::CmduMessageTx incoming{incoming_buffer.data(), incoming_buffer.size()};
    std::unique_ptr<son::dpp_protocol_task> protocol;
    const sMacAddr agent_mac = tlvf::mac_from_string("02:00:00:00:00:02");
};

TEST_F(DppProtocolTaskTest, RejectsUnrelatedCmdu)
{
    ASSERT_TRUE(incoming.create(1, MessageType::TOPOLOGY_QUERY_MESSAGE));
    dispatch(false);
    EXPECT_EQ(0U, transport.calls);
}

TEST_F(DppProtocolTaskTest, RejectsChirpWithoutTlv)
{
    ASSERT_TRUE(incoming.create(1, MessageType::CHIRP_NOTIFICATION_MESSAGE));
    dispatch(false);
    EXPECT_EQ(0U, transport.calls);
}

TEST_F(DppProtocolTaskTest, RejectsAuthenticationWithoutBootstrap)
{
    EXPECT_FALSE(protocol->send_dpp_authentication_request());
    EXPECT_EQ(0U, transport.calls);
}

TEST_F(DppProtocolTaskTest, RejectsZeroUnknownAndUnsupportedDirectAgents)
{
    EXPECT_FALSE(
        protocol->set_direct_encap_onboarding_agent(beerocks::net::network_utils::ZERO_MAC));
    EXPECT_FALSE(
        protocol->set_direct_encap_onboarding_agent(tlvf::mac_from_string("02:00:00:00:00:99")));
    database->m_agents.get(agent_mac)->dpp_onboarding_support = false;
    EXPECT_FALSE(protocol->set_direct_encap_onboarding_agent(agent_mac));
    EXPECT_FALSE(protocol->session_state().use_direct_encap);
    EXPECT_EQ(0U, transport.calls);
}

TEST_F(DppProtocolTaskTest, MissingBootstrapRollsBackDirectSelection)
{
    EXPECT_FALSE(protocol->start_direct_encap_onboarding(agent_mac));
    EXPECT_FALSE(protocol->session_state().use_direct_encap);
    EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, protocol->session_state().proxy_agent);
    EXPECT_EQ(0U, transport.calls);
}

TEST_F(DppProtocolTaskTest, DirectStartBuildsAuthenticationWithoutChirpTlv)
{
    insert_bootstrap();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(protocol->start_direct_encap_onboarding(agent_mac));
    EXPECT_EQ(1U, transport.calls);
    EXPECT_EQ(agent_mac, transport.destination);
    EXPECT_TRUE(protocol->session_state().use_direct_encap);
    EXPECT_EQ(agent_mac, protocol->session_state().proxy_agent);
    ieee1905_1::CmduMessageRx rx(transport.wire.data(), transport.wire.size());
    ASSERT_TRUE(rx.parse());
    EXPECT_EQ(MessageType::DIRECT_ENCAP_DPP_MESSAGE, rx.getMessageType());
    auto frame = rx.getClass<wfa_map::tlvDppMessage>();
    ASSERT_NE(nullptr, frame);
    EXPECT_GT(frame->dpp_frame_length(), 0U);
    EXPECT_EQ(nullptr, rx.getClass<wfa_map::tlvDppChirpValue>());
    EXPECT_EQ(nullptr, rx.getClass<wfa_map::tlv1905EncapDpp>());
}

TEST_F(DppProtocolTaskTest, DirectTransportFailureIsReturned)
{
    insert_bootstrap();
    ASSERT_FALSE(HasFatalFailure());
    transport.result = false;
    EXPECT_FALSE(protocol->start_direct_encap_onboarding(agent_mac));
    EXPECT_EQ(1U, transport.calls);
    EXPECT_FALSE(protocol->session_state().authentication_confirm_sent);
}

TEST_F(DppProtocolTaskTest, MatchedChirpBuildsProxiedAuthentication)
{
    insert_bootstrap();
    ASSERT_FALSE(HasFatalFailure());
    add_chirp();
    ASSERT_FALSE(HasFatalFailure());
    dispatch(true);
    ASSERT_EQ(1U, transport.calls);
    EXPECT_EQ(agent_mac, transport.destination);
    EXPECT_FALSE(protocol->session_state().use_direct_encap);
    EXPECT_EQ(agent_mac, protocol->session_state().proxy_agent);
    EXPECT_TRUE(protocol->session_state().last_chirp_enrollee_valid);
    EXPECT_EQ(agent_mac, protocol->session_state().last_chirp_enrollee);
    ieee1905_1::CmduMessageRx rx(transport.wire.data(), transport.wire.size());
    ASSERT_TRUE(rx.parse());
    EXPECT_EQ(MessageType::PROXIED_ENCAP_DPP_MESSAGE, rx.getMessageType());
    auto encap = rx.getClass<wfa_map::tlv1905EncapDpp>();
    ASSERT_NE(nullptr, encap);
    EXPECT_EQ(wfa_map::tlv1905EncapDpp::DPP_AUTHENTICATION_REQUEST, encap->frame_type());
    EXPECT_FALSE(encap->frame_flags().dpp_frame_indicator);
    ASSERT_NE(nullptr, encap->dest_sta_mac());
    EXPECT_EQ(agent_mac, *encap->dest_sta_mac());
    EXPECT_GT(encap->encapsulated_frame_length(), 0U);
    auto chirp = rx.getClass<wfa_map::tlvDppChirpValue>();
    ASSERT_NE(nullptr, chirp);
    EXPECT_TRUE(chirp->flags().hash_validity);
}

TEST_F(DppProtocolTaskTest, InvalidChirpDoesNotStartAuthentication)
{
    insert_bootstrap();
    ASSERT_FALSE(HasFatalFailure());
    add_chirp(false);
    ASSERT_FALSE(HasFatalFailure());
    dispatch(true);
    EXPECT_EQ(0U, transport.calls);
    EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, protocol->session_state().proxy_agent);
}

TEST_F(DppProtocolTaskTest, CompletedBootstrapChirpDoesNotRestartAuthentication)
{
    insert_bootstrap(true);
    ASSERT_FALSE(HasFatalFailure());
    // The URI M: address must still be connected. An absent/disconnected enrollee
    // is intentionally allowed to onboard again even when this URI completed before.
    auto station = database->add_station(agent_mac, agent_mac);
    ASSERT_NE(nullptr, station);
    station->state = beerocks::STATE_CONNECTED;
    add_chirp();
    ASSERT_FALSE(HasFatalFailure());
    dispatch(true);
    EXPECT_EQ(0U, transport.calls);
    EXPECT_TRUE(database->get_dpp_bootstrap_info("protocol-test")->onboarded);
    EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, protocol->session_state().proxy_agent);
}

TEST_F(DppProtocolTaskTest, CompletedBootstrapChirpRestartsAuthenticationForAbsentEnrollee)
{
    insert_bootstrap(true);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_FALSE(database->has_station(agent_mac));
    add_chirp();
    ASSERT_FALSE(HasFatalFailure());
    dispatch(true);
    ASSERT_EQ(1U, transport.calls);
    EXPECT_FALSE(database->get_dpp_bootstrap_info("protocol-test")->onboarded);
    EXPECT_EQ(agent_mac, protocol->session_state().proxy_agent);
    ieee1905_1::CmduMessageRx rx(transport.wire.data(), transport.wire.size());
    ASSERT_TRUE(rx.parse());
    EXPECT_EQ(MessageType::PROXIED_ENCAP_DPP_MESSAGE, rx.getMessageType());
    auto encap = rx.getClass<wfa_map::tlv1905EncapDpp>();
    ASSERT_NE(nullptr, encap);
    EXPECT_EQ(wfa_map::tlv1905EncapDpp::DPP_AUTHENTICATION_REQUEST, encap->frame_type());
}

TEST_F(DppProtocolTaskTest, CompletedBootstrapChirpRestartsAuthenticationForDisconnectedEnrollee)
{
    insert_bootstrap(true);
    ASSERT_FALSE(HasFatalFailure());
    auto station = database->add_station(agent_mac, agent_mac);
    ASSERT_NE(nullptr, station);
    station->state = beerocks::STATE_DISCONNECTED;
    add_chirp();
    ASSERT_FALSE(HasFatalFailure());
    dispatch(true);
    ASSERT_EQ(1U, transport.calls);
    EXPECT_FALSE(database->get_dpp_bootstrap_info("protocol-test")->onboarded);
    EXPECT_EQ(agent_mac, protocol->session_state().proxy_agent);
    ieee1905_1::CmduMessageRx rx(transport.wire.data(), transport.wire.size());
    ASSERT_TRUE(rx.parse());
    EXPECT_EQ(MessageType::PROXIED_ENCAP_DPP_MESSAGE, rx.getMessageType());
    auto encap = rx.getClass<wfa_map::tlv1905EncapDpp>();
    ASSERT_NE(nullptr, encap);
    EXPECT_EQ(wfa_map::tlv1905EncapDpp::DPP_AUTHENTICATION_REQUEST, encap->frame_type());
}

TEST_F(DppProtocolTaskTest, DirectSessionRejectsMissingDppTlv)
{
    ASSERT_TRUE(protocol->set_direct_encap_onboarding_agent(agent_mac));
    ASSERT_TRUE(incoming.create(1, MessageType::DIRECT_ENCAP_DPP_MESSAGE));
    dispatch(false);
    EXPECT_EQ(0U, transport.calls);
}

TEST_F(DppProtocolTaskTest, DirectSessionRejectsProxiedTraffic)
{
    ASSERT_TRUE(protocol->set_direct_encap_onboarding_agent(agent_mac));
    ASSERT_TRUE(incoming.create(1, MessageType::PROXIED_ENCAP_DPP_MESSAGE));
    dispatch(false);
    EXPECT_TRUE(protocol->session_state().use_direct_encap);
    EXPECT_EQ(agent_mac, protocol->session_state().proxy_agent);
    EXPECT_FALSE(protocol->session_state().authentication_confirm_sent);
    EXPECT_FALSE(protocol->session_state().configuration_request_received);
    // Rejection prevents DPP processing, not the required 1905 acknowledgement.
    ASSERT_EQ(1U, transport.calls);
    EXPECT_EQ(agent_mac, transport.destination);
    ieee1905_1::CmduMessageRx rx(transport.wire.data(), transport.wire.size());
    ASSERT_TRUE(rx.parse());
    EXPECT_EQ(MessageType::ACK_MESSAGE, rx.getMessageType());
    EXPECT_EQ(1U, rx.getMessageId());
}

TEST_F(DppProtocolTaskTest, ConfigurationRequiresAgentAndObjects)
{
    EXPECT_FALSE(protocol->send_dpp_configuration_response({"{}"}));
    ASSERT_TRUE(protocol->set_direct_encap_onboarding_agent(agent_mac));
    EXPECT_FALSE(protocol->send_dpp_configuration_response({}));
    EXPECT_FALSE(protocol->send_dpp_configuration_response({"{}"}));
    EXPECT_FALSE(protocol->session_state().configuration_response_sent);
    EXPECT_EQ(0U, transport.calls);
}

TEST_F(DppProtocolTaskTest, ResetClearsSelectedAgentAndStagedConfiguration)
{
    ASSERT_TRUE(protocol->set_direct_encap_onboarding_agent(agent_mac));
    protocol->set_pending_configuration_objects({"{\"test\":1}"}, true);
    ASSERT_EQ(1U, protocol->session_state().pending_configuration_objects.size());
    ASSERT_TRUE(protocol->session_state().pending_send_conn_status);
    protocol->reset_session();
    const auto &state = protocol->session_state();
    EXPECT_FALSE(state.use_direct_encap);
    EXPECT_EQ(beerocks::net::network_utils::ZERO_MAC, state.proxy_agent);
    EXPECT_FALSE(state.last_chirp_enrollee_valid);
    EXPECT_FALSE(state.authentication_confirm_sent);
    EXPECT_FALSE(state.configuration_request_received);
    EXPECT_FALSE(state.configuration_response_sent);
    EXPECT_FALSE(state.configuration_result_received);
    EXPECT_FALSE(state.connection_status_result_received);
    EXPECT_TRUE(state.pending_configuration_objects.empty());
    EXPECT_FALSE(state.pending_send_conn_status);
    EXPECT_TRUE(state.configuration_request_json.empty());
    EXPECT_TRUE(state.requested_net_role.empty());
    EXPECT_FALSE(protocol->send_dpp_authentication_request());
    EXPECT_EQ(0U, transport.calls);
}

} // namespace
