/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "db.h"
#include "on_action.h"

#include <bcl/beerocks_config_file.h>
#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/beerocks_logging.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>
#include <amxd/amxd_transaction.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {
std::vector<std::string> g_bootstrapping_triggers;
} // namespace

// controller.cpp is not linked here; record the trigger instead of running onboarding.
void son::Controller::notify_dpp_bootstrapping_trigger(const std::string &reason)
{
    g_bootstrapping_triggers.push_back(reason);
}

namespace {

const std::string g_dpp_uri =
    "DPP:C:81/1;V:3;"
    "K:MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgADesMV9KvHJ+ih374aYRoZlNHUdSjV1uZx2mHhRgotxmI=;;";

void ignore_event(const char *const, const amxc_var_t *const, void *const) {}

class ProvisionedDppEventTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_log_conf.sLog.global_levels = "error,fatal,warning";
        m_log_conf.sLog.syslog_levels = "off";
        m_logger.reset(new beerocks::logging("provisioned_dpp_event_test", m_log_conf.sLog));

        m_amxrt                  = std::make_shared<beerocks::nbapi::Amxrt>();
        char app_name[]          = "provisioned_dpp_event_test";
        std::vector<char *> argv = {app_name, nullptr};

        ASSERT_EQ(0, m_amxrt->Initialize(argv.size() - 1, argv.data(), nullptr));

        m_event_loop = std::make_shared<beerocks::EventLoopImpl>();
        ASSERT_TRUE(m_event_loop->register_handlers(amxp_signal_fd(), {}));

        // Only the ProvisionedDPP handlers run; the others need a full Controller.
        auto events = prplmesh::controller::actions::get_events_list();
        for (auto &event : events) {
            if (event.name.rfind("event_provisioned_dpp_", 0) != 0) {
                event.callback = ignore_event;
            }
        }
        m_ambiorix = std::make_shared<beerocks::nbapi::AmbiorixImpl>(
            m_event_loop, prplmesh::controller::actions::get_actions_callback_list(), events,
            std::vector<beerocks::nbapi::sFunctions>{});
        ASSERT_TRUE(m_ambiorix->load_datamodel(CONTROLLER_ODL_PATH));

        m_database = std::make_unique<son::db>(m_db_config, *m_logger,
                                               tlvf::mac_from_string("02:00:00:00:00:01"), nullptr);
        // Never constructed: the recording notify above does not touch members.
        m_database->set_controller_ctx(reinterpret_cast<son::Controller *>(m_controller_storage));
        prplmesh::controller::actions::g_database = m_database.get();
        read_all_pending_signals();
        g_bootstrapping_triggers.clear();
    }

    void TearDown() override
    {
        prplmesh::controller::actions::g_database = nullptr;
        m_database.reset();
        m_ambiorix.reset();
        m_event_loop.reset();
        m_amxrt.reset();
    }

    void read_all_pending_signals()
    {
        while (amxp_signal_read() == 0)
            ;
    }

    // Same as ProvisionedDPP.+{Alias=...,DPPURI=...}: one transaction, no later update.
    void add_provisioned_dpp(const std::string &alias, const std::string &dpp_uri)
    {
        amxd_trans_t trans;
        ASSERT_EQ(amxd_status_ok, amxd_trans_init(&trans));
        amxd_trans_select_pathf(&trans, DATAELEMENTS_ROOT_DM ".Network.ProvisionedDPP.");
        amxd_trans_add_inst(&trans, 0, nullptr);
        amxd_trans_set_value(cstring_t, &trans, "Alias", alias.c_str());
        if (!dpp_uri.empty()) {
            amxd_trans_set_value(cstring_t, &trans, "DPPURI", dpp_uri.c_str());
        }
        EXPECT_EQ(amxd_status_ok, amxd_trans_apply(&trans, beerocks::nbapi::Amxrt::getDatamodel()));
        amxd_trans_clean(&trans);
        read_all_pending_signals();
    }

    beerocks::config_file::sConfigMaster m_log_conf{};
    son::db::sDbMasterConfig m_db_config{};
    std::unique_ptr<beerocks::logging> m_logger;
    std::shared_ptr<beerocks::nbapi::Amxrt> m_amxrt;
    std::shared_ptr<beerocks::EventLoopImpl> m_event_loop;
    std::shared_ptr<beerocks::nbapi::AmbiorixImpl> m_ambiorix;
    std::unique_ptr<son::db> m_database;
    alignas(son::Controller) unsigned char m_controller_storage[sizeof(son::Controller)];
};

// cppcheck-suppress syntaxError
TEST_F(ProvisionedDppEventTest, create_with_alias_and_uri_imports_uri)
{
    add_provisioned_dpp("dpp-test", g_dpp_uri);

    EXPECT_EQ(1U, m_database->get_dpp_bootstrap_info_count());
    EXPECT_NE(nullptr, m_database->get_dpp_bootstrap_info("dpp-test"));
    EXPECT_EQ(std::vector<std::string>{"ProvisionedDPP added alias=dpp-test"},
              g_bootstrapping_triggers);
}

TEST_F(ProvisionedDppEventTest, set_uri_after_create_imports_uri)
{
    add_provisioned_dpp("dpp-test", "");
    EXPECT_EQ(0U, m_database->get_dpp_bootstrap_info_count());
    EXPECT_TRUE(g_bootstrapping_triggers.empty());

    ASSERT_TRUE(
        m_ambiorix->set(DATAELEMENTS_ROOT_DM ".Network.ProvisionedDPP.1", "DPPURI", g_dpp_uri));
    read_all_pending_signals();

    EXPECT_EQ(1U, m_database->get_dpp_bootstrap_info_count());
    EXPECT_EQ(std::vector<std::string>{"ProvisionedDPP changed alias=dpp-test"},
              g_bootstrapping_triggers);
}

} // namespace
