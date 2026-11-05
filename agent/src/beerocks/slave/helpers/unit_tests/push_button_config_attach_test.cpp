/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */
#include "../push_button_config_attach.h"

#include <bcl/son/son_wireless_utils.h>
#include <tlvf/WSC/EncryptedSettingsPayload.h>
#include <tlvf/WSC/m2.h>

#include <gtest/gtest.h>

using beerocks::attach_push_button_configs_to_m2s;
using beerocks::PBCOnboardingTask;

namespace {

struct sTestBssConfig {
    WSC::m2::config m2_config;
    WSC::EncryptedSettingsPayload::config payload_config;
    son::wireless_utils::sWpsPushButtonConf wps_push_button;
    uint16_t pbc_listen_duration = 0;
};

constexpr uint8_t g_fronthaul = WSC::eWscVendorExtSubelementBssType::FRONTHAUL_BSS;
constexpr uint8_t g_backhaul  = WSC::eWscVendorExtSubelementBssType::BACKHAUL_BSS;
constexpr uint8_t g_teardown  = WSC::eWscVendorExtSubelementBssType::TEARDOWN;

sTestBssConfig make_m2(int8_t bss_index, uint8_t bss_type, const std::string &ssid)
{
    sTestBssConfig info;
    info.m2_config.bss_index     = bss_index;
    info.payload_config.bss_type = bss_type;
    info.payload_config.ssid     = ssid;
    return info;
}

PBCOnboardingTask::sPushButtonConfig make_config(uint8_t bss_index, uint16_t listen_duration,
                                                 const std::string &device_password = "")
{
    PBCOnboardingTask::sPushButtonConfig config;
    config.bss_index          = bss_index;
    config.uuid_e             = "12345678-9abc-def0-0123-456789abcdef";
    config.device_password_id = device_password.empty() ? 0x0004 : 0x0000;
    config.device_password    = device_password;
    config.listen_duration    = listen_duration;
    return config;
}

std::map<uint8_t, size_t>
count_per_bss_index(const std::vector<PBCOnboardingTask::sPushButtonConfig> &configs)
{
    std::map<uint8_t, size_t> counts;
    for (const auto &config : configs) {
        counts[config.bss_index]++;
    }
    return counts;
}

void expect_not_armed(const sTestBssConfig &info)
{
    EXPECT_FALSE(info.wps_push_button.armed) << "SSID " << info.payload_config.ssid;
    EXPECT_EQ(info.pbc_listen_duration, 0) << "SSID " << info.payload_config.ssid;
}

} // namespace

TEST(PushButtonConfigAttach, attached_to_the_m2_with_its_bss_index)
{
    std::vector<sTestBssConfig> m2s                           = {make_m2(1, g_fronthaul, "ssid-1"),
                                       make_m2(2, g_fronthaul, "ssid-2")};
    std::vector<PBCOnboardingTask::sPushButtonConfig> configs = {make_config(2, 30, "12345670")};

    auto listen_durations =
        attach_push_button_configs_to_m2s("wlan0", configs, count_per_bss_index(configs), m2s);

    expect_not_armed(m2s[0]);
    EXPECT_TRUE(m2s[1].wps_push_button.armed);
    EXPECT_EQ(m2s[1].wps_push_button.uuid_e, "12345678-9abc-def0-0123-456789abcdef");
    EXPECT_EQ(m2s[1].wps_push_button.device_password_id, 0x0000);
    EXPECT_EQ(m2s[1].wps_push_button.device_password, "12345670");
    EXPECT_EQ(m2s[1].pbc_listen_duration, 30);
    ASSERT_EQ(listen_durations.size(), 1U);
    EXPECT_EQ(listen_durations[0].first, "ssid-2");
    EXPECT_EQ(listen_durations[0].second, 30);
}

TEST(PushButtonConfigAttach, two_tlvs_with_one_bss_index_attach_nothing)
{
    std::vector<sTestBssConfig> m2s = {make_m2(1, g_fronthaul, "ssid-1"),
                                       make_m2(2, g_fronthaul, "ssid-2")};

    std::vector<PBCOnboardingTask::sPushButtonConfig> both_readable = {
        make_config(1, 30), make_config(1, 60), make_config(2, 90)};
    auto listen_durations = attach_push_button_configs_to_m2s(
        "wlan0", both_readable, count_per_bss_index(both_readable), m2s);
    expect_not_armed(m2s[0]);
    EXPECT_TRUE(m2s[1].wps_push_button.armed);
    ASSERT_EQ(listen_durations.size(), 1U);
    EXPECT_EQ(listen_durations[0].first, "ssid-2");

    m2s = {make_m2(1, g_fronthaul, "ssid-1")};
    std::vector<PBCOnboardingTask::sPushButtonConfig> one_readable = {make_config(1, 30)};
    listen_durations = attach_push_button_configs_to_m2s("wlan0", one_readable, {{1, 2}}, m2s);
    expect_not_armed(m2s[0]);
    EXPECT_TRUE(listen_durations.empty());
}

TEST(PushButtonConfigAttach, bss_index_of_zero_or_two_m2s_attaches_nothing)
{
    std::vector<sTestBssConfig> m2s                           = {make_m2(1, g_fronthaul, "ssid-1"),
                                       make_m2(4, g_fronthaul, "ssid-4a"),
                                       make_m2(4, g_fronthaul, "ssid-4b")};
    std::vector<PBCOnboardingTask::sPushButtonConfig> configs = {make_config(3, 30),
                                                                 make_config(4, 30)};

    auto listen_durations =
        attach_push_button_configs_to_m2s("wlan0", configs, count_per_bss_index(configs), m2s);

    for (const auto &info : m2s) {
        expect_not_armed(info);
    }
    EXPECT_TRUE(listen_durations.empty());
}

TEST(PushButtonConfigAttach, teardown_m2_gets_nothing)
{
    std::vector<sTestBssConfig> m2s                           = {make_m2(1, g_teardown, ""),
                                       make_m2(2, g_teardown | g_fronthaul, "ssid-2")};
    std::vector<PBCOnboardingTask::sPushButtonConfig> configs = {make_config(1, 30),
                                                                 make_config(2, 30)};

    auto listen_durations =
        attach_push_button_configs_to_m2s("wlan0", configs, count_per_bss_index(configs), m2s);

    expect_not_armed(m2s[0]);
    expect_not_armed(m2s[1]);
    EXPECT_TRUE(listen_durations.empty());
}

TEST(PushButtonConfigAttach, m2_that_is_not_fronthaul_only_gets_nothing)
{
    std::vector<sTestBssConfig> m2s                           = {make_m2(1, g_backhaul, "backhaul"),
                                       make_m2(2, g_fronthaul | g_backhaul, "mixed"),
                                       make_m2(3, 0, "none")};
    std::vector<PBCOnboardingTask::sPushButtonConfig> configs = {
        make_config(1, 30), make_config(2, 30), make_config(3, 30)};

    auto listen_durations =
        attach_push_button_configs_to_m2s("wlan0", configs, count_per_bss_index(configs), m2s);

    for (const auto &info : m2s) {
        expect_not_armed(info);
    }
    EXPECT_TRUE(listen_durations.empty());
}

TEST(PushButtonConfigAttach, listen_durations_of_the_attached_entries_only)
{
    std::vector<sTestBssConfig> m2s = {
        make_m2(1, g_fronthaul, "ssid-1"), make_m2(2, g_backhaul, "backhaul"),
        make_m2(3, g_fronthaul, "ssid-3"), make_m2(4, g_fronthaul, "ssid-4")};
    std::vector<PBCOnboardingTask::sPushButtonConfig> configs = {
        make_config(3, 10), make_config(2, 20), make_config(1, 65535)};

    auto listen_durations =
        attach_push_button_configs_to_m2s("wlan0", configs, count_per_bss_index(configs), m2s);

    ASSERT_EQ(listen_durations.size(), 2U);
    EXPECT_EQ(listen_durations[0], std::make_pair(std::string("ssid-3"), uint16_t(10)));
    EXPECT_EQ(listen_durations[1], std::make_pair(std::string("ssid-1"), uint16_t(65535)));
    EXPECT_EQ(m2s[0].pbc_listen_duration, 65535);
    expect_not_armed(m2s[1]);
    EXPECT_EQ(m2s[2].pbc_listen_duration, 10);
    expect_not_armed(m2s[3]);
}

TEST(PushButtonConfigAttach, no_config_changes_nothing)
{
    std::vector<sTestBssConfig> m2s = {make_m2(1, g_fronthaul, "ssid-1")};
    auto listen_durations           = attach_push_button_configs_to_m2s("wlan0", {}, {}, m2s);
    expect_not_armed(m2s[0]);
    EXPECT_TRUE(listen_durations.empty());
}
