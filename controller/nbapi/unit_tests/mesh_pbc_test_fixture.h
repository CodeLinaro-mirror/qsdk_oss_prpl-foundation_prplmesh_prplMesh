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
#include "templates_actions_stub.h"

#include "db.h"

#include <bcl/beerocks_config_file.h>
#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/beerocks_logging.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>

#include <amxo/amxo.h>
#include <amxp/amxp_signal.h>

#include <easylogging++.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
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

} // namespace mesh_pbc_test

#endif // MESH_PBC_TEST_FIXTURE_H
