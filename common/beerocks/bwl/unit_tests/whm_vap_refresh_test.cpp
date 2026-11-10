/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

// Runs the real WHM AP HAL against a fake Ambiorix bus. This executable defines the
// AmbiorixConnection methods used by the HAL and AmbiorixConnectionManager::get_connection(),
// so they take precedence over the shared wbapi library.

#include "../whm/ap_wlan_hal_whm.h"

#include <ambiorix_connection.h>
#include <ambiorix_connection_manager.h>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

// The test target is built with AddressSanitizer to catch use-after-free; leaks are not checked.
extern "C" const char *__asan_default_options() { return "detect_leaks=0"; }

namespace beerocks {
namespace wbapi {
namespace {

constexpr char RADIO[]   = "Device.WiFi.Radio.1.";
constexpr char AP[]      = "Device.WiFi.AccessPoint.1.";
constexpr char SSID[]    = "Device.WiFi.SSID.1.";
constexpr char RADIO_Q[] = "Device.WiFi.Radio.[Name == 'wlan0'].";
constexpr char AP_Q[]    = "Device.WiFi.AccessPoint.[Alias == 'wlan0.1'].";
constexpr char AP_ALL[]  = "Device.WiFi.AccessPoint.*.";

amxc_var_t *new_table()
{
    amxc_var_t *var = nullptr;
    amxc_var_new(&var);
    amxc_var_set_type(var, AMXC_VAR_ID_HTABLE);
    return var;
}

amxc_var_t *copy_of(const amxc_var_t *src)
{
    amxc_var_t *dst = nullptr;
    amxc_var_new(&dst);
    amxc_var_copy(dst, src);
    return dst;
}

struct Subscription {
    std::string path;
    std::string filter;
    std::shared_ptr<sAmbiorixEventHandler> handler;
};

struct FakeBus {
    std::map<std::string, amxc_var_t *> objects;
    std::vector<Subscription> subscriptions;

    static FakeBus &get()
    {
        static FakeBus bus;
        return bus;
    }

    void clear()
    {
        for (auto &object : objects) {
            amxc_var_delete(&object.second);
        }
        objects.clear();
        subscriptions.clear();
    }

    void set_object(const std::string &path, amxc_var_t *object)
    {
        auto &slot = objects[path];
        amxc_var_delete(&slot);
        slot = object;
    }

    void remove_object(const std::string &path)
    {
        auto it = objects.find(path);
        if (it != objects.end()) {
            amxc_var_delete(&it->second);
            objects.erase(it);
        }
    }

    void populate()
    {
        auto radio = new_table();
        amxc_var_add_key(cstring_t, radio, "Name", "wlan0");
        amxc_var_add_key(bool, radio, "Enable", true);
        amxc_var_add_key(cstring_t, radio, "OperatingFrequencyBand", "5GHz");
        set_object(RADIO, radio);

        auto ssid = new_table();
        amxc_var_add_key(cstring_t, ssid, "MACAddress", "02:00:00:00:00:01");
        amxc_var_add_key(cstring_t, ssid, "SSID", "test-ssid");
        amxc_var_add_key(int8_t, ssid, "MLDUnit", -1);
        set_object(SSID, ssid);

        set_ap(true);
    }

    // Without Enable, the AccessPoint is still listed but its refresh fails.
    void set_ap(bool with_enable)
    {
        auto ap = new_table();
        amxc_var_add_key(cstring_t, ap, "Alias", "wlan0.1");
        amxc_var_add_key(cstring_t, ap, "SSIDReference", "Device.WiFi.SSID.1");
        amxc_var_add_key(cstring_t, ap, "RadioReference", "Device.WiFi.Radio.1");
        if (with_enable) {
            amxc_var_add_key(bool, ap, "Enable", true);
        }
        amxc_var_add_key(cstring_t, ap, "MultiAPType", "FronthaulBSS");
        amxc_var_add_key(cstring_t, ap, "CustomAlias", "");
        amxc_var_add_key(cstring_t, ap, "Status", "Enabled");
        set_object(AP, ap);
    }
};

} // namespace

AmbiorixConnectionSmartPtr AmbiorixConnectionManager::get_connection(const std::string &,
                                                                     const std::string &)
{
    static auto connection = std::make_shared<AmbiorixConnection>("fake", "usp:fake");
    return connection;
}

AmbiorixConnection::AmbiorixConnection(const std::string &backend, const std::string &uri)
    : m_amxb_backend(backend), m_bus_uri(uri)
{
}
AmbiorixConnection::~AmbiorixConnection() {}
const std::string &AmbiorixConnection::uri() const { return m_bus_uri; }
int AmbiorixConnection::get_fd() { return 100; }
int AmbiorixConnection::get_signal_fd() { return 101; }

AmbiorixVariantSmartPtr AmbiorixConnection::get_object(const std::string &path, const int32_t, bool)
{
    auto &bus = FakeBus::get();
    if (path == AP_ALL) {
        auto all = new_table();
        auto ap  = bus.objects.find(AP);
        if (ap != bus.objects.end()) {
            amxc_var_set_key(all, AP, copy_of(ap->second), AMXC_VAR_FLAG_DEFAULT);
        }
        return AmbiorixVariant::bind(all);
    }
    auto it = bus.objects.find(path);
    if (it == bus.objects.end()) {
        return AmbiorixVariantSmartPtr{};
    }
    return AmbiorixVariant::bind(copy_of(it->second));
}

AmbiorixVariantSmartPtr AmbiorixConnection::get_param(const std::string &, const std::string &)
{
    return AmbiorixVariantSmartPtr{};
}

bool AmbiorixConnection::resolve_path(const std::string &search, std::vector<std::string> &out,
                                      int &status)
{
    status = 0;
    out.clear();
    if (search == RADIO_Q) {
        out = {RADIO};
    } else if (search == AP_Q) {
        out = {AP};
    } else if (FakeBus::get().objects.count(search)) {
        out = {search};
    }
    return !out.empty();
}

bool AmbiorixConnection::call(const std::string &, const char *, AmbiorixVariant &,
                              AmbiorixVariant &)
{
    return false;
}

bool AmbiorixConnection::subscribe(const std::string &path, const std::string &filter,
                                   sAmbiorixSubscriptionInfo &info)
{
    FakeBus::get().subscriptions.push_back({path, filter, info.handler});
    return true;
}

bool AmbiorixConnection::unsubscribe(sAmbiorixSubscriptionInfo &) { return true; }

namespace {

class TestApHal : public bwl::whm::ap_wlan_hal_whm {
public:
    TestApHal()
        : bwl::base_wlan_hal(bwl::HALType::AccessPoint, "wlan0", bwl::IfaceType::Intel, cb(), {}),
          bwl::whm::ap_wlan_hal_whm("wlan0", cb(), {})
    {
    }
    static bwl::base_wlan_hal::hal_event_cb_t cb()
    {
        return [](bwl::base_wlan_hal::hal_event_ptr_t) { return true; };
    }
    using bwl::whm::base_wlan_hal_whm::m_vapsExtInfo;
    bwl::RadioInfo &radio_info() { return m_radio_info; }
};

struct BusCleanup {
    ~BusCleanup() { FakeBus::get().clear(); }
};

// One object-changed event that carries Status=Disabled and WDSEnable=true for the AP.
amxc_var_t *status_and_wds_event()
{
    auto event = new_table();
    amxc_var_add_key(cstring_t, event, "path", AP);
    auto params = amxc_var_add_key(amxc_htable_t, event, "parameters", nullptr);
    auto status = amxc_var_add_key(amxc_htable_t, params, "Status", nullptr);
    amxc_var_add_key(cstring_t, status, "to", "Disabled");
    auto wds = amxc_var_add_key(amxc_htable_t, params, "WDSEnable", nullptr);
    amxc_var_add_key(bool, wds, "to", true);
    return event;
}

std::shared_ptr<sAmbiorixEventHandler> find_ap_status_handler()
{
    for (auto &s : FakeBus::get().subscriptions) {
        if (s.path == "Device.WiFi.AccessPoint." && s.handler &&
            s.handler->event_type == AMX_CL_OBJECT_CHANGED_EVT &&
            s.filter.find("parameters.Status") != std::string::npos) {
            return s.handler;
        }
    }
    return nullptr;
}

// cppcheck-suppress syntaxError
TEST(whm_vap_refresh, removed_access_point_frees_its_slot)
{
    BusCleanup cleanup;
    FakeBus::get().populate();
    TestApHal hal;
    ASSERT_TRUE(hal.refresh_vaps_info(beerocks::IFACE_RADIO_ID));
    ASSERT_EQ(1u, hal.radio_info().available_vaps.size());

    FakeBus::get().remove_object(AP);

    EXPECT_TRUE(hal.refresh_vaps_info(beerocks::IFACE_RADIO_ID));
    EXPECT_TRUE(hal.radio_info().available_vaps.empty());
    EXPECT_TRUE(hal.m_vapsExtInfo.empty());
}

// The AP event handler processes all changed parameters of one event. A Status change refreshes
// the VAP, and a failed refresh restores m_vapsExtInfo; the later WDSEnable parameter must not
// use an iterator into the old map (AddressSanitizer reports a use-after-free otherwise).
// cppcheck-suppress syntaxError
TEST(whm_vap_refresh, failed_refresh_in_ap_event_keeps_vap_info)
{
    BusCleanup cleanup;
    FakeBus::get().populate();
    TestApHal hal;
    ASSERT_TRUE(hal.refresh_vaps_info(beerocks::IFACE_RADIO_ID));
    ASSERT_EQ(1u, hal.m_vapsExtInfo.size());
    auto handler = find_ap_status_handler();
    ASSERT_TRUE(handler);

    FakeBus::get().set_ap(false);

    AmbiorixVariant event(status_and_wds_event(), true);
    handler->callback_fn(event);

    ASSERT_EQ(1u, hal.m_vapsExtInfo.size());
    EXPECT_EQ(1u, hal.m_vapsExtInfo.count("wlan0.1"));
    ASSERT_EQ(1u, hal.radio_info().available_vaps.size());
    EXPECT_EQ("wlan0.1", hal.radio_info().available_vaps.begin()->second.bss);
}

} // namespace
} // namespace wbapi
} // namespace beerocks
