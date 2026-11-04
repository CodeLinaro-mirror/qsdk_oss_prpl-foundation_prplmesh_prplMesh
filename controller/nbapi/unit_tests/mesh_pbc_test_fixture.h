/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef MESH_PBC_TEST_FIXTURE_H
#define MESH_PBC_TEST_FIXTURE_H

#include "mesh_pbc_template_actions.h"
#include "on_action.h"
#include "son_actions_stub.h"
#include "templates_actions_stub.h"

#include "db.h"
#include "tasks/pbc_onboarding_task.h"

#include <bcl/beerocks_config_file.h>
#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/beerocks_logging.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>

#include <amxo/amxo.h>
#include <amxp/amxp_signal.h>

#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <tlvf/wfa_map/tlvMultiApPushButtonStatus.h>

#include <easylogging++.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mesh_pbc_test {

using prplmesh::controller::actions::eMeshPbcBssConfigResult;
using prplmesh::controller::actions::eMeshPbcSessionTemplateResult;
using prplmesh::controller::actions::g_database;
using prplmesh::controller::actions::g_stub_templates_apply_count;
using prplmesh::controller::actions::mesh_pbc_get_bss_config;
using prplmesh::controller::actions::sMeshPbcBssConfig;
using prplmesh::controller::actions::sMeshPbcBssStatus;
using prplmesh::controller::actions::sMeshPbcSessionTemplate;

const std::string g_root           = TEMPLATES_ROOT_DM;
const std::string g_network        = g_root + ".Network";
const std::string g_ssc_table      = g_root + ".SSCTemplate";
const std::string g_mesh_pbc_table = g_root + ".X_PRPLWARE-COM_MeshPBCTemplate";
const std::string g_tr181_root     = "Device.WiFi.Templates";
const std::string g_primary        = "X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference";
const std::string g_uuid_e_param   = "X_PRPLWARE-COM_MeshPBCUUID_E";
const std::string g_template_ref   = "X_PRPLWARE-COM_MeshPBCTemplateReference";
const std::string g_uuid_e         = "12345678-9abc-def0-1234-56789abcdef0";
const std::string g_device_table   = std::string(DATAELEMENTS_ROOT_DM) + ".Network.Device";
const std::string g_bss_dm_path    = g_device_table + ".1.Radio.1.BSS.1";
// Controller AL MAC address of the database, and the UUID that pwhm makes from it
// (libswlc swl_uuid_fromMacAddress(), offset 0).
const std::string g_controller_al_mac        = "02:00:00:00:00:01";
const std::string g_controller_al_mac_uuid_e = "30323030-3030-f030-f030-303130323030";
const std::string g_default_time_stamp       = "0001-01-01T00:00:00Z";

using params_t = std::vector<std::pair<std::string, std::string>>;

class LogRecorder : public el::LogDispatchCallback {
public:
    static std::vector<std::pair<el::Level, std::string>> lines;

    static size_t count(el::Level level, const std::string &text = "")
    {
        return std::count_if(
            lines.begin(), lines.end(), [&](const std::pair<el::Level, std::string> &line) {
                return line.first == level && line.second.find(text) != std::string::npos;
            });
    }

protected:
    void handle(const el::LogDispatchData *data) noexcept override
    {
        lines.emplace_back(data->logMessage()->level(), data->logMessage()->message());
    }
};

inline void ignore_event(const char *const, const amxc_var_t *const, void *const) {}

inline void count_object_changed(const char *const, const amxc_var_t *const data, void *const priv)
{
    auto counter      = static_cast<std::pair<std::string, unsigned> *>(priv);
    const char *path  = GET_CHAR(data, "path");
    const auto &start = counter->first;
    if (path && std::string(path).compare(0, start.size(), start) == 0) {
        counter->second++;
    }
}

inline std::string mesh_pbc_path(unsigned index)
{
    return g_mesh_pbc_table + "." + std::to_string(index);
}
inline std::string ssc_path(unsigned index) { return g_ssc_table + "." + std::to_string(index); }

class MeshPbcTemplateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_amxrt                  = std::make_shared<beerocks::nbapi::Amxrt>();
        char app_name[]          = "mesh_pbc_template_actions_test";
        std::vector<char *> argv = {app_name, nullptr};
        ASSERT_EQ(0, m_amxrt->Initialize(argv.size() - 1, argv.data(), nullptr));

        m_event_loop = std::make_shared<beerocks::EventLoopImpl>();
        ASSERT_TRUE(m_event_loop->register_handlers(amxp_signal_fd(), {}));

        auto events = prplmesh::controller::actions::get_mesh_pbc_template_events_list();
        events.push_back({"event_configuration_changed", ignore_event});
        m_ambiorix = std::make_shared<beerocks::nbapi::AmbiorixImpl>(
            m_event_loop, prplmesh::controller::actions::get_mesh_pbc_template_actions_list(),
            events, m_functions);
        ASSERT_TRUE(m_ambiorix->load_datamodel(CONTROLLER_ODL_PATH));

        beerocks::config_file::sConfigMaster beerocks_master_conf;
        m_logger = std::make_shared<beerocks::logging>("logger", beerocks_master_conf.sLog);
        m_master_conf.use_dataelements_vap_configs = false;
        m_db       = std::make_shared<son::db>(m_master_conf, *m_logger,
                                         tlvf::mac_from_string(g_controller_al_mac), m_ambiorix);
        g_database = m_db.get();

        LogRecorder::lines.clear();
        el::Helpers::installLogDispatchCallback<LogRecorder>("LogRecorder");

        ASSERT_TRUE(add(g_ssc_table, 1, {{"SSCTemplateID", "ssc-1"}, {"HaulType", "Fronthaul"}}));
        ASSERT_TRUE(add(g_ssc_table, 2, {{"SSCTemplateID", "ssc-2"}, {"HaulType", "Backhaul"}}));
        ASSERT_TRUE(add(g_ssc_table, 3, {{"SSCTemplateID", "ssc-3"}, {"HaulType", "Fronthaul"}}));
        ASSERT_TRUE(
            add(g_ssc_table, 4,
                {{"SSCTemplateID", "ssc-4"}, {"HaulType", "Fronthaul"}, {"SSCEnable", "false"}}));
        ASSERT_TRUE(
            add(g_ssc_table, 5, {{"SSCTemplateID", "ssc-5"}, {"HaulType", "Fronthaul,Backhaul"}}));
        ASSERT_TRUE(set(g_network, "Enable", "true"));
        ASSERT_TRUE(add(g_mesh_pbc_table, 1, {{"PBCTemplateID", "t1"}}));
        ASSERT_TRUE(set(ssc_path(1), g_template_ref, mesh_pbc_path(1)));
        if (m_set_uuid_e) {
            ASSERT_TRUE(set(g_network, g_uuid_e_param, g_uuid_e));
        }

        deliver_events();
        g_stub_templates_apply_count = 0;
        LogRecorder::lines.clear();
    }

    void TearDown() override
    {
        el::Helpers::uninstallLogDispatchCallback<LogRecorder>("LogRecorder");
        g_database = nullptr;
        m_db.reset();
        m_ambiorix.reset();
        m_event_loop.reset();
        m_amxrt.reset();
    }

    static amxd_dm_t *dm() { return beerocks::nbapi::Amxrt::getDatamodel(); }

    /**
     * @param read_only true to add the instance as the Controller does, to a read-only table.
     * @return true if the data model accepted the instance.
     */
    static bool add(const std::string &table, unsigned index, const params_t &params,
                    bool read_only = false)
    {
        amxd_trans_t trans;
        amxd_trans_init(&trans);
        if (read_only) {
            amxd_trans_set_attr(&trans, amxd_tattr_change_ro, true);
        }
        amxd_trans_select_pathf(&trans, "%s.", table.c_str());
        amxd_trans_add_inst(&trans, index, nullptr);
        for (const auto &param : params) {
            amxd_trans_set_value(cstring_t, &trans, param.first.c_str(), param.second.c_str());
        }
        auto status = amxd_trans_apply(&trans, dm());
        amxd_trans_clean(&trans);
        return status == amxd_status_ok;
    }

    static bool remove(const std::string &table, unsigned index)
    {
        amxd_trans_t trans;
        amxd_trans_init(&trans);
        amxd_trans_select_pathf(&trans, "%s.", table.c_str());
        amxd_trans_del_inst(&trans, index, nullptr);
        auto status = amxd_trans_apply(&trans, dm());
        amxd_trans_clean(&trans);
        return status == amxd_status_ok;
    }

    // No transaction, so that no libamxd version runs the PBCTemplateID validator.
    static bool add_template_without_id(unsigned index)
    {
        amxd_object_t *table    = amxd_dm_findf(dm(), "%s.", g_mesh_pbc_table.c_str());
        amxd_object_t *instance = nullptr;
        amxc_var_t values;
        amxc_var_init(&values);
        amxc_var_set_type(&values, AMXC_VAR_ID_HTABLE);
        amxc_var_add_key(cstring_t, &values, "PBCTemplateID", "");
        auto status = amxd_object_new_instance(&instance, table, nullptr, index, &values);
        amxc_var_clean(&values);
        return status == amxd_status_ok;
    }

    static bool set(const std::string &object, const std::string &param, const std::string &value)
    {
        return set(object, {{param, value}});
    }

    static bool set(const std::string &object, const params_t &params)
    {
        amxd_trans_t trans;
        amxd_trans_init(&trans);
        amxd_trans_select_pathf(&trans, "%s.", object.c_str());
        for (const auto &param : params) {
            amxd_trans_set_value(cstring_t, &trans, param.first.c_str(), param.second.c_str());
        }
        auto status = amxd_trans_apply(&trans, dm());
        amxd_trans_clean(&trans);
        return status == amxd_status_ok;
    }

    static std::string get(const std::string &object, const std::string &param)
    {
        amxd_object_t *obj = amxd_dm_findf(dm(), "%s.", object.c_str());
        if (!obj) {
            return "<no object>";
        }
        char *value = amxd_object_get_value(cstring_t, obj, param.c_str(), nullptr);
        std::string text(value ? value : "<no value>");
        free(value);
        return text;
    }

    static uint32_t get_type(const std::string &object, const std::string &param)
    {
        amxd_object_t *obj = amxd_dm_findf(dm(), "%s.", object.c_str());
        if (!obj) {
            return AMXC_VAR_ID_INVALID;
        }
        const amxc_var_t *value = amxd_object_get_param_value(obj, param.c_str());
        return value ? amxc_var_type_of(value) : AMXC_VAR_ID_INVALID;
    }

    /**
     * @brief Checks that each value is accepted, and that each rejected value keeps the last
     * accepted value.
     */
    static void check_values(const std::string &object, const std::string &param,
                             const std::vector<std::string> &accepted,
                             const std::vector<std::string> &rejected)
    {
        for (const auto &value : accepted) {
            EXPECT_TRUE(set(object, param, value)) << param << " = \"" << value << "\"";
            EXPECT_EQ(get(object, param), value);
        }
        const std::string kept = get(object, param);
        for (const auto &value : rejected) {
            EXPECT_FALSE(set(object, param, value)) << param << " = \"" << value << "\"";
            EXPECT_EQ(get(object, param), kept) << param << " = \"" << value << "\"";
        }
    }

    static void deliver_events()
    {
        while (amxp_signal_read() == 0)
            ;
    }

    eMeshPbcBssConfigResult get_config(const std::string &template_ref)
    {
        m_config = {};
        m_reason.clear();
        return mesh_pbc_get_bss_config(template_ref, m_config, m_reason);
    }

    std::string ssc_ref(unsigned index) const { return get(ssc_path(index), g_template_ref); }

    eMeshPbcSessionTemplateResult get_session_template(const std::string &reference)
    {
        m_session_template = {};
        m_reason.clear();
        return prplmesh::controller::actions::mesh_pbc_get_session_template(
            reference, m_session_template, m_reason);
    }

    /** false: the fixture keeps Templates.Network.X_PRPLWARE-COM_MeshPBCUUID_E at "". */
    bool m_set_uuid_e = true;
    /** Methods that the data model binds when it is loaded. */
    std::vector<beerocks::nbapi::sFunctions> m_functions;

    std::shared_ptr<beerocks::nbapi::Amxrt> m_amxrt;
    std::shared_ptr<beerocks::EventLoopImpl> m_event_loop;
    std::shared_ptr<beerocks::nbapi::AmbiorixImpl> m_ambiorix;
    std::shared_ptr<beerocks::logging> m_logger;
    son::db::sDbMasterConfig m_master_conf{};
    std::shared_ptr<son::db> m_db;
    sMeshPbcBssConfig m_config;
    sMeshPbcSessionTemplate m_session_template;
    std::string m_reason;
};

class MeshPbcTemplateNoUuidTest : public MeshPbcTemplateTest {
protected:
    void SetUp() override
    {
        m_set_uuid_e = false;
        MeshPbcTemplateTest::SetUp();
    }
};

using son::PBCOnboardingTask;
using eStatusResult = wfa_map::tlvMultiApPushButtonStatus::eResult;

const sMacAddr g_agent_mac      = tlvf::mac_from_string("02:00:00:00:00:02");
const sMacAddr g_other_agent    = tlvf::mac_from_string("02:00:00:00:00:03");
const sMacAddr g_unknown_mac    = tlvf::mac_from_string("02:00:00:00:00:99");
const sMacAddr g_ruid           = tlvf::mac_from_string("02:00:00:00:10:00");
const sMacAddr g_bssid_1        = tlvf::mac_from_string("02:00:00:00:10:01");
const sMacAddr g_bssid_2        = tlvf::mac_from_string("02:00:00:00:10:02");
const sMacAddr g_bssid_backhaul = tlvf::mac_from_string("02:00:00:00:10:03");
const std::string g_link_ssid   = "pbc-ssid";

class MeshPbcSessionEngineTest : public MeshPbcTemplateTest {
protected:
    void SetUp() override
    {
        MeshPbcTemplateTest::SetUp();
        m_task = std::make_unique<PBCOnboardingTask>(*m_db, m_cmdu_tx);

        m_agent = m_db->add_agent(g_agent_mac);
        ASSERT_TRUE(m_agent);
        m_agent->state                            = beerocks::STATE_CONNECTED;
        m_agent->mesh_wide_pbc_onboarding_support = true;
        ASSERT_TRUE(m_db->add_radio(g_ruid, g_agent_mac));
        auto radio = m_db->get_radio(g_agent_mac, g_ruid);
        ASSERT_TRUE(radio);
        m_bss = m_db->add_bss(*radio, g_bssid_1, g_link_ssid, 1);
        ASSERT_TRUE(m_bss);
        m_bss->fronthaul = true;
        ASSERT_TRUE(m_db->add_agent(g_other_agent));

        son::wireless_utils::sBssInfoConf link{};
        link.bss_index             = 1;
        link.ssid                  = g_link_ssid;
        link.fronthaul             = true;
        link.mesh_pbc_template_ref = mesh_pbc_path(1);
        m_db->add_configured_bss_info(g_ruid, link);
        LogRecorder::lines.clear();
    }

    void TearDown() override
    {
        m_task.reset();
        MeshPbcTemplateTest::TearDown();
    }

    PBCOnboardingTask::sStartResult start(const std::string &reference,
                                          const sMacAddr &agent_al_mac        = {},
                                          const std::vector<sMacAddr> &bssids = {})
    {
        PBCOnboardingTask::sStartRequest request;
        request.template_reference = reference;
        request.agent_al_mac       = agent_al_mac;
        request.bssids             = bssids;
        return m_task->start(request);
    }

    void expect_rejected(const PBCOnboardingTask::sStartResult &result,
                         PBCOnboardingTask::eResult error)
    {
        EXPECT_FALSE(result.accepted);
        EXPECT_EQ(int(result.error), int(error));
        EXPECT_EQ(result.session_id, "");
    }

    /**
     * @brief Gives a message to the engine, as received from src_mac.
     *
     * @param statuses Status TLVs: BSSID, Result, Failure Reason.
     * @return The result of handle_ieee1905_1_msg().
     */
    bool receive(ieee1905_1::eMessageType type, const sMacAddr &src_mac,
                 const std::vector<std::tuple<sMacAddr, uint8_t, uint16_t>> &statuses = {})
    {
        uint8_t buffer[1024] = {};
        ieee1905_1::CmduMessageTx cmdu_tx(buffer, sizeof(buffer));
        EXPECT_TRUE(cmdu_tx.create(0x1234, type));
        for (const auto &status : statuses) {
            auto tlv = cmdu_tx.addClass<wfa_map::tlvMultiApPushButtonStatus>();
            EXPECT_TRUE(tlv);
            if (!tlv) {
                return false;
            }
            tlv->pbc_session_id().al_mac = g_agent_mac;
            tlv->pbc_session_id().mid    = 0x0101;
            tlv->bssid()                 = std::get<0>(status);
            tlv->result()                = static_cast<eStatusResult>(std::get<1>(status));
            if (std::get<1>(status) == uint8_t(eStatusResult::FAILURE_ABORTED) ||
                std::get<1>(status) == uint8_t(eStatusResult::NOT_STARTED)) {
                EXPECT_TRUE(tlv->set_failure_reason(std::get<2>(status)));
            }
        }
        EXPECT_TRUE(cmdu_tx.finalize());
        ieee1905_1::CmduMessageRx cmdu_rx(buffer, cmdu_tx.getMessageLength());
        EXPECT_TRUE(cmdu_rx.parse());
        return m_task->handle_ieee1905_1_msg(src_mac, cmdu_rx);
    }

    std::string template_result() const { return get(mesh_pbc_path(1) + ".Status", "Result"); }

    uint8_t m_tx_buffer[4096] = {};
    ieee1905_1::CmduMessageTx m_cmdu_tx{m_tx_buffer, sizeof(m_tx_buffer)};
    std::unique_ptr<PBCOnboardingTask> m_task;
    std::shared_ptr<prplmesh::controller::db::Agent> m_agent;
    std::shared_ptr<prplmesh::controller::db::Agent::sRadio::sBss> m_bss;
};

using son::g_stub_refused_destination;
using son::g_stub_sent_cmdus;
using son::sStubSentCmdu;
using eSessionResult = PBCOnboardingTask::eResult;

const sMacAddr g_ruid_2     = tlvf::mac_from_string("02:00:00:00:20:00");
const sMacAddr g_ruid_3     = tlvf::mac_from_string("02:00:00:00:30:00");
const sMacAddr g_bssid_3    = tlvf::mac_from_string("02:00:00:00:2a:01");
const sMacAddr g_bssid_4    = tlvf::mac_from_string("02:00:00:00:3b:01");
const sMacAddr g_enrollee   = tlvf::mac_from_string("11:22:33:44:55:66");
const sMacAddr g_controller = tlvf::mac_from_string(g_controller_al_mac);

/**
 * Session engine with the clock of the test and two connected Agents: the first Agent has the
 * push-button BSS g_bssid_1, the second Agent has g_bssid_3 and g_bssid_4 on two radios.
 */
class MeshPbcSessionTest : public MeshPbcSessionEngineTest {
protected:
    void SetUp() override
    {
        MeshPbcSessionEngineTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        m_task = std::make_unique<PBCOnboardingTask>(*m_db, m_cmdu_tx, [this] { return m_now; });

        m_agent_2 = m_db->get_agent(g_other_agent);
        ASSERT_TRUE(m_agent_2);
        m_agent_2->state                            = beerocks::STATE_CONNECTED;
        m_agent_2->mesh_wide_pbc_onboarding_support = true;
        m_bss_3                                     = add_linked_bss(g_ruid_2, g_bssid_3);
        m_bss_4                                     = add_linked_bss(g_ruid_3, g_bssid_4);
        ASSERT_TRUE(m_bss_3);
        ASSERT_TRUE(m_bss_4);

        g_stub_sent_cmdus.clear();
        g_stub_refused_destination = beerocks::net::network_utils::ZERO_MAC;
        LogRecorder::lines.clear();
    }

    void TearDown() override
    {
        g_stub_sent_cmdus.clear();
        g_stub_refused_destination = beerocks::net::network_utils::ZERO_MAC;
        MeshPbcSessionEngineTest::TearDown();
    }

    /** Adds a radio of the second Agent with one BSS that has a push-button link. */
    std::shared_ptr<prplmesh::controller::db::Agent::sRadio::sBss>
    add_linked_bss(const sMacAddr &ruid, const sMacAddr &bssid)
    {
        if (!m_db->add_radio(ruid, g_other_agent)) {
            return nullptr;
        }
        auto radio = m_db->get_radio(g_other_agent, ruid);
        if (!radio) {
            return nullptr;
        }
        auto bss = m_db->add_bss(*radio, bssid, g_link_ssid, 1);
        if (!bss) {
            return nullptr;
        }
        bss->fronthaul = true;
        son::wireless_utils::sBssInfoConf link{};
        link.bss_index             = 1;
        link.ssid                  = g_link_ssid;
        link.fronthaul             = true;
        link.mesh_pbc_template_ref = mesh_pbc_path(1);
        m_db->add_configured_bss_info(ruid, link);
        return bss;
    }

    /**
     * @brief Starts a Controller session that records each completion.
     *
     * @return The result of start().
     */
    PBCOnboardingTask::sStartResult start_session(const sMacAddr &agent_al_mac        = {},
                                                  const std::vector<sMacAddr> &bssids = {})
    {
        PBCOnboardingTask::sStartRequest request;
        request.template_reference = mesh_pbc_path(1);
        request.agent_al_mac       = agent_al_mac;
        request.bssids             = bssids;
        request.on_completion      = [this](const PBCOnboardingTask::sCompletion &completion) {
            m_completions.push_back(completion);
        };
        auto result = m_task->start(request);
        if (result.accepted) {
            for (const auto &sent : g_stub_sent_cmdus) {
                if (sent.trigger && sent.start) {
                    m_session_id = sent.session_id;
                }
            }
        }
        return result;
    }

    /**
     * @brief Gives the engine a Multi-AP Push-Button Status message with one Status TLV.
     *
     * The Failure Reason field is present for FAILURE_ABORTED and NOT_STARTED.
     */
    void report(const sMacAddr &src_mac, const wfa_map::sPbcSessionId &session_id,
                const sMacAddr &bssid, eStatusResult result, uint16_t failure_reason = 0,
                const sMacAddr &enrollee = beerocks::net::network_utils::ZERO_MAC)
    {
        uint8_t buffer[256] = {};
        ieee1905_1::CmduMessageTx cmdu_tx(buffer, sizeof(buffer));
        ASSERT_TRUE(
            cmdu_tx.create(0x2345, ieee1905_1::eMessageType::MULTI_AP_PUSH_BUTTON_STATUS_MESSAGE));
        auto tlv = cmdu_tx.addClass<wfa_map::tlvMultiApPushButtonStatus>();
        ASSERT_TRUE(tlv);
        tlv->pbc_session_id() = session_id;
        tlv->bssid()          = bssid;
        tlv->result()         = result;
        if (result == eStatusResult::FAILURE_ABORTED || result == eStatusResult::NOT_STARTED) {
            ASSERT_TRUE(tlv->set_failure_reason(failure_reason));
        }
        tlv->enrollee_mac() = enrollee;
        ASSERT_TRUE(cmdu_tx.finalize());
        ieee1905_1::CmduMessageRx cmdu_rx(buffer, cmdu_tx.getMessageLength());
        ASSERT_TRUE(cmdu_rx.parse());
        EXPECT_TRUE(m_task->handle_ieee1905_1_msg(src_mac, cmdu_rx));
    }

    /** Gives the engine a report for the active session. */
    void report(const sMacAddr &src_mac, const sMacAddr &bssid, eStatusResult result,
                uint16_t failure_reason  = 0,
                const sMacAddr &enrollee = beerocks::net::network_utils::ZERO_MAC)
    {
        report(src_mac, m_session_id, bssid, result, failure_reason, enrollee);
    }

    /** Moves the clock of the engine and runs the engine once. */
    void advance(std::chrono::milliseconds time)
    {
        m_now += time;
        m_task->execute();
    }

    /** @return The Triggers sent to al_mac with that State. */
    static std::vector<sStubSentCmdu> triggers(const sMacAddr &al_mac, bool start)
    {
        std::vector<sStubSentCmdu> result;
        for (const auto &sent : g_stub_sent_cmdus) {
            if (sent.trigger && sent.destination == al_mac && sent.start == start) {
                result.push_back(sent);
            }
        }
        return result;
    }

    /** @return The number of Triggers with this State that the stub recorded. */
    static size_t trigger_count(bool start)
    {
        return std::count_if(
            g_stub_sent_cmdus.begin(), g_stub_sent_cmdus.end(),
            [&](const sStubSentCmdu &sent) { return sent.trigger && sent.start == start; });
    }

    /** @return The BSSIDs as a set of text MAC addresses. */
    static std::set<std::string> bssid_set(const std::vector<sMacAddr> &bssids)
    {
        std::set<std::string> result;
        for (const auto &bssid : bssids) {
            result.insert(tlvf::mac_to_string(bssid));
        }
        return result;
    }

    /** Checks that exactly one STOP went to al_mac, with these BSSIDs. */
    static void expect_one_stop(const sMacAddr &al_mac, const std::vector<sMacAddr> &bssids)
    {
        auto stops = triggers(al_mac, false);
        ASSERT_EQ(stops.size(), 1U) << al_mac;
        EXPECT_EQ(bssid_set(stops[0].bssids), bssid_set(bssids)) << al_mac;
        EXPECT_EQ(stops[0].bssids.size(), bssids.size()) << al_mac;
    }

    /** @return A parameter of the X_PRPLWARE-COM_MeshPBC object of a BSS. */
    static std::string bss_param(const prplmesh::controller::db::Agent::sRadio::sBss &bss,
                                 const std::string &param)
    {
        return get(bss.dm_path + ".X_PRPLWARE-COM_MeshPBC", param);
    }

    /** @return A parameter of the Status object of the template of the fixture. */
    std::string status(const std::string &param) const
    {
        return get(mesh_pbc_path(1) + ".Status", param);
    }

    /** Checks the one completion of the session and the template Status.Result. */
    void expect_completion(eSessionResult status, uint16_t failure_reason,
                           const sMacAddr &enrollee = beerocks::net::network_utils::ZERO_MAC,
                           const sMacAddr &bssid    = beerocks::net::network_utils::ZERO_MAC)
    {
        ASSERT_EQ(m_completions.size(), 1U);
        const auto &completion = m_completions[0];
        EXPECT_EQ(completion.session_id, PBCOnboardingTask::session_id_to_string(m_session_id));
        EXPECT_EQ(int(completion.status), int(status));
        EXPECT_EQ(completion.failure_reason, failure_reason);
        EXPECT_EQ(completion.enrollee_mac, enrollee);
        EXPECT_EQ(completion.bssid, bssid);
        EXPECT_EQ(template_result(), PBCOnboardingTask::result_to_string(status));
    }

    std::chrono::steady_clock::time_point m_now = std::chrono::steady_clock::now();
    std::shared_ptr<prplmesh::controller::db::Agent> m_agent_2;
    std::shared_ptr<prplmesh::controller::db::Agent::sRadio::sBss> m_bss_3;
    std::shared_ptr<prplmesh::controller::db::Agent::sRadio::sBss> m_bss_4;
    wfa_map::sPbcSessionId m_session_id{};
    std::vector<PBCOnboardingTask::sCompletion> m_completions;
};

} // namespace mesh_pbc_test

#endif // MESH_PBC_TEST_FIXTURE_H
