/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_event_loop_impl.h>

#include <ambiorix_impl.h>
#include <ambiorix_runtime.h>
#include <optional_subobjects.h>

#include <amxd/amxd_dm_functions.h>
#include <amxd/amxd_object_hierarchy.h>
#include <amxo/amxo.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr char device_path[] = DATAELEMENTS_ROOT_DM ".Network.Device.{i}.";
constexpr char radio_path[]  = DATAELEMENTS_ROOT_DM ".Network.Device.{i}.Radio.{i}.";
constexpr char capabilities_path[] =
    DATAELEMENTS_ROOT_DM ".Network.Device.{i}.Radio.{i}.Capabilities.";
constexpr char bss_path[] = DATAELEMENTS_ROOT_DM ".Network.Device.{i}.Radio.{i}.BSS.{i}.";

// Root of a scratch object that has the same mibs as static children.
constexpr char static_root[] = "SupportedDataModelStatic.";

// The objects of the conditional mibs, as the supported data model lists them.
const std::vector<std::string> &conditional_objects()
{
    static const std::vector<std::string> objects = {
        std::string(device_path) + "bSTAMLD.",
        std::string(device_path) + "bSTAMLD.bSTAMLDConfig.",
        std::string(radio_path) + "SpatialReuse.",
        std::string(radio_path) + "X_PRPLWARE-COM_CACCompletion.",
        std::string(radio_path) + "X_PRPLWARE-COM_CACCompletion.Pairs.{i}.",
        std::string(capabilities_path) + "WiFi6APRole.",
        std::string(capabilities_path) + "WiFi6APRole.MCSNSS.{i}.",
        std::string(capabilities_path) + "WiFi6bSTARole.",
        std::string(capabilities_path) + "WiFi6bSTARole.MCSNSS.{i}.",
        std::string(capabilities_path) + "WiFi7APRole.",
        std::string(capabilities_path) + "WiFi7APRole.EMLMRFreqSeparation.{i}.",
        std::string(capabilities_path) + "WiFi7APRole.EMLSRFreqSeparation.{i}.",
        std::string(capabilities_path) + "WiFi7APRole.STRFreqSeparation.{i}.",
        std::string(capabilities_path) + "WiFi7APRole.NSTRFreqSeparation.{i}.",
        std::string(capabilities_path) + "WiFi7bSTARole.",
        std::string(capabilities_path) + "WiFi7bSTARole.EMLMRFreqSeparation.{i}.",
        std::string(capabilities_path) + "WiFi7bSTARole.EMLSRFreqSeparation.{i}.",
        std::string(capabilities_path) + "WiFi7bSTARole.STRFreqSeparation.{i}.",
        std::string(capabilities_path) + "WiFi7bSTARole.NSTRFreqSeparation.{i}.",
        std::string(bss_path) + "SetQoSManagementInput.",
    };
    return objects;
}

void ignore_event(const char *const, const amxc_var_t *const, void *const) {}

amxd_status_t ignore_action(amxd_object_t *, amxd_param_t *, amxd_action_t, const amxc_var_t *const,
                            amxc_var_t *const, void *)
{
    return amxd_status_ok;
}

// The same object as an entry of the scratch object, e.g. "SupportedDataModelStatic.WiFi6APRole.".
std::string static_twin(const std::string &path)
{
    for (const auto &optional : prplmesh::controller::actions::get_optional_subobject_list()) {
        const auto prefix = optional.path + optional.mib_name + ".";
        if (path.compare(0, prefix.size(), prefix) == 0) {
            return static_root + path.substr(optional.path.size());
        }
    }
    return "";
}

class SupportedDataModelTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_amxrt                  = std::make_shared<beerocks::nbapi::Amxrt>();
        char app_name[]          = "supported_data_model_test";
        std::vector<char *> argv = {app_name, nullptr};

        ASSERT_EQ(0, m_amxrt->Initialize(argv.size() - 1, argv.data(), nullptr));

        m_event_loop = std::make_shared<beerocks::EventLoopImpl>();
        ASSERT_TRUE(m_event_loop->register_handlers(amxp_signal_fd(), {}));

        const std::vector<beerocks::nbapi::sActionsCallback> actions = {
            {"action_read_assoc_time", ignore_action},
            {"action_read_last_change", ignore_action},
            {"action_last_steer_time", ignore_action},
        };
        const std::vector<beerocks::nbapi::sEvents> events = {
            {"event_configuration_changed", ignore_event},
            {"event_traffic_separation_override_changed", ignore_event},
            {"event_network_enable_changed", ignore_event},
            {"event_network_group_changed", ignore_event},
        };
        m_ambiorix = std::make_shared<beerocks::nbapi::AmbiorixImpl>(
            m_event_loop, actions, events, std::vector<beerocks::nbapi::sFunctions>{},
            prplmesh::controller::actions::get_optional_subobject_list());

        ASSERT_TRUE(m_ambiorix->load_datamodel(CONTROLLER_ODL_PATH));

        // The scratch object applies the conditional mibs to its definition. Its supported data
        // model is the reference.
        std::string odl = "%define { object SupportedDataModelStatic {";
        for (const auto &optional : prplmesh::controller::actions::get_optional_subobject_list()) {
            odl += " extend using mib " + optional.mib_name + ";";
        }
        odl += " } }";
        ASSERT_EQ(0, amxo_parser_parse_string(beerocks::nbapi::Amxrt::getParser(), odl.c_str(),
                                              amxd_dm_get_root(dm())));
    }

    void TearDown() override
    {
        m_ambiorix.reset();
        m_event_loop.reset();
        m_amxrt.reset();
    }

    static amxd_dm_t *dm() { return beerocks::nbapi::Amxrt::getDatamodel(); }

    static amxd_object_t *find(const std::string &path)
    {
        return amxd_dm_findf(dm(), "%s", path.c_str());
    }

    // The top-level object of the controller data model.
    static amxd_object_t *top_level_object()
    {
        amxd_object_t *object = find(DATAELEMENTS_ROOT_DM);
        while (object && amxd_object_get_parent(object) &&
               amxd_object_get_type(amxd_object_get_parent(object)) != amxd_object_root) {
            object = amxd_object_get_parent(object);
        }
        return object;
    }

    static void make_args(amxc_var_t *args, const std::string &rel_path, bool first_level_only,
                          bool parameters, amxd_dm_access_t access)
    {
        amxc_var_set_type(args, AMXC_VAR_ID_HTABLE);
        amxc_var_add_key(cstring_t, args, "rel_path", rel_path.c_str());
        amxc_var_add_key(bool, args, "first_level_only", first_level_only);
        amxc_var_add_key(bool, args, "parameters", parameters);
        amxc_var_add_key(bool, args, "functions", true);
        amxc_var_add_key(bool, args, "events", true);
        amxc_var_add_key(bool, args, "keysets", true);
        amxc_var_add_key(uint32_t, args, "access", access);
    }

    // Call _get_supported on an object.
    static amxd_status_t get_supported(amxd_object_t *object, const std::string &rel_path,
                                       amxc_var_t *ret, bool first_level_only = false,
                                       bool parameters         = true,
                                       amxd_dm_access_t access = amxd_dm_access_public)
    {
        amxc_var_t args;
        amxc_var_init(&args);
        make_args(&args, rel_path, first_level_only, parameters, access);
        const auto status = amxd_object_invoke_function(object, "_get_supported", &args, ret);
        amxc_var_clean(&args);
        return status;
    }

    // Call the libamxd implementation of _get_supported directly.
    static amxd_status_t default_get_supported(amxd_object_t *object, const std::string &rel_path,
                                               amxc_var_t *ret, bool first_level_only = false,
                                               bool parameters         = true,
                                               amxd_dm_access_t access = amxd_dm_access_public)
    {
        amxc_var_t args;
        amxc_var_init(&args);
        make_args(&args, rel_path, first_level_only, parameters, access);
        const auto status = amxd_object_func_get_supported(object, nullptr, &args, ret);
        amxc_var_clean(&args);
        return status;
    }

    // Path of an object relative to the top-level object.
    static std::string usp_rel_path(const std::string &path)
    {
        const std::string top = amxd_object_get_name(top_level_object(), AMXD_OBJECT_NAMED);
        return path.substr(top.size() + 1);
    }

    // Each conditional entry in the result is equal to the entry of the static object.
    // Returns the number of conditional entries in the result.
    static size_t expect_same_as_static(const amxc_var_t *result, const amxc_var_t *reference)
    {
        size_t count               = 0;
        const amxc_htable_t *table = amxc_var_constcast(amxc_htable_t, result);
        EXPECT_NE(nullptr, table);
        amxc_htable_for_each(it, table)
        {
            const std::string key = amxc_htable_it_get_key(it);
            const auto twin       = static_twin(key);
            if (twin.empty()) {
                continue;
            }
            ++count;
            const amxc_var_t *expected = GET_ARG(reference, twin.c_str());
            EXPECT_NE(nullptr, expected) << twin;
            int compare = -1;
            EXPECT_EQ(0, amxc_var_compare(amxc_var_from_htable_it(it), expected, &compare)) << key;
            EXPECT_EQ(0, compare) << key;
        }
        return count;
    }

    static const amxc_var_t *find_param(const amxc_var_t *entry, const std::string &name)
    {
        amxc_var_for_each(param, GET_ARG(entry, "supported_params"))
        {
            if (name == GET_CHAR(param, "param_name")) {
                return param;
            }
        }
        return nullptr;
    }

    std::shared_ptr<beerocks::nbapi::Amxrt> m_amxrt;
    std::shared_ptr<beerocks::EventLoopImpl> m_event_loop;
    std::shared_ptr<beerocks::nbapi::AmbiorixImpl> m_ambiorix;
};

// cppcheck-suppress syntaxError
TEST_F(SupportedDataModelTest, lists_conditional_objects)
{
    amxc_var_t result;
    amxc_var_init(&result);
    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), "", &result));

    for (const auto &path : conditional_objects()) {
        EXPECT_NE(nullptr, GET_ARG(&result, path.c_str())) << path;
    }

    const auto *wifi6_ap_role =
        GET_ARG(&result, (std::string(capabilities_path) + "WiFi6APRole.").c_str());
    ASSERT_NE(nullptr, wifi6_ap_role);
    EXPECT_FALSE(GET_BOOL(wifi6_ap_role, "is_multi_instance"));
    const auto *he160 = find_param(wifi6_ap_role, "HE160");
    ASSERT_NE(nullptr, he160);
    EXPECT_EQ(AMXC_VAR_ID_BOOL, GET_UINT32(he160, "type"));
    EXPECT_EQ(0U, GET_UINT32(he160, "access"));
    const auto *counter = find_param(wifi6_ap_role, "MCSNSSNumberOfEntries");
    ASSERT_NE(nullptr, counter);
    EXPECT_EQ(AMXC_VAR_ID_UINT32, GET_UINT32(counter, "type"));

    const auto *mcsnss =
        GET_ARG(&result, (std::string(capabilities_path) + "WiFi6APRole.MCSNSS.{i}.").c_str());
    ASSERT_NE(nullptr, mcsnss);
    EXPECT_TRUE(GET_BOOL(mcsnss, "is_multi_instance"));

    const auto *config =
        GET_ARG(&result, (std::string(device_path) + "bSTAMLD.bSTAMLDConfig.").c_str());
    ASSERT_NE(nullptr, config);
    const auto *emlmr = find_param(config, "EMLMREnabled");
    ASSERT_NE(nullptr, emlmr);
    EXPECT_EQ(1U, GET_UINT32(emlmr, "access"));

    const auto *spatial_reuse =
        GET_ARG(&result, (std::string(radio_path) + "SpatialReuse.").c_str());
    ASSERT_NE(nullptr, spatial_reuse);
    const auto *bitmap = find_param(spatial_reuse, "SRGBSSColorBitmap");
    ASSERT_NE(nullptr, bitmap);
    EXPECT_EQ(AMXC_VAR_ID_CSTRING, GET_UINT32(bitmap, "type"));

    amxc_var_clean(&result);
}

TEST_F(SupportedDataModelTest, conditional_entries_equal_static_entries)
{
    amxc_var_t result;
    amxc_var_t reference;
    amxc_var_init(&result);
    amxc_var_init(&reference);
    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), "", &result));
    ASSERT_EQ(amxd_status_ok, default_get_supported(find(static_root), "", &reference));

    // The reference has one entry for the scratch object and one per conditional object.
    EXPECT_EQ(conditional_objects().size() + 1,
              amxc_htable_size(amxc_var_constcast(amxc_htable_t, &reference)));
    EXPECT_EQ(conditional_objects().size(), expect_same_as_static(&result, &reference));

    amxc_var_clean(&reference);
    amxc_var_clean(&result);
}

TEST_F(SupportedDataModelTest, request_flags_pass_through)
{
    const std::string role = std::string(capabilities_path) + "WiFi6APRole.";
    amxc_var_t result;
    amxc_var_t reference;
    amxc_var_init(&result);
    amxc_var_init(&reference);

    // Without parameters.
    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), "", &result, false, false));
    ASSERT_EQ(amxd_status_ok,
              default_get_supported(find(static_root), "", &reference, false, false));
    EXPECT_EQ(conditional_objects().size(), expect_same_as_static(&result, &reference));
    const auto *entry = GET_ARG(&result, role.c_str());
    ASSERT_NE(nullptr, entry);
    EXPECT_EQ(nullptr, GET_ARG(entry, "supported_params"));

    // With protected access.
    amxc_var_clean(&result);
    amxc_var_clean(&reference);
    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), "", &result, false, true,
                                            amxd_dm_access_protected));
    ASSERT_EQ(amxd_status_ok, default_get_supported(find(static_root), "", &reference, false, true,
                                                    amxd_dm_access_protected));
    EXPECT_EQ(conditional_objects().size(), expect_same_as_static(&result, &reference));
    entry = GET_ARG(&result, role.c_str());
    ASSERT_NE(nullptr, entry);
    EXPECT_NE(nullptr, GET_ARG(entry, "supported_params"));

    amxc_var_clean(&reference);
    amxc_var_clean(&result);
}

TEST_F(SupportedDataModelTest, other_entries_unchanged)
{
    amxc_var_t result;
    amxc_var_t expected;
    amxc_var_init(&result);
    amxc_var_init(&expected);
    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), "", &result));
    ASSERT_EQ(amxd_status_ok, default_get_supported(top_level_object(), "", &expected));

    for (const auto &path : conditional_objects()) {
        EXPECT_EQ(nullptr, GET_ARG(&expected, path.c_str())) << path;
        amxc_var_t *entry = GET_ARG(&result, path.c_str());
        amxc_var_delete(&entry);
    }
    int compare = -1;
    EXPECT_EQ(0, amxc_var_compare(&result, &expected, &compare));
    EXPECT_EQ(0, compare);

    amxc_var_clean(&expected);
    amxc_var_clean(&result);
}

TEST_F(SupportedDataModelTest, path_inside_conditional_object)
{
    const std::string role = std::string(capabilities_path) + "WiFi6APRole.";
    amxc_var_t result;
    amxc_var_t reference;
    amxc_var_init(&result);
    amxc_var_init(&reference);

    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), usp_rel_path(role), &result));
    EXPECT_EQ(2U, amxc_htable_size(amxc_var_constcast(amxc_htable_t, &result)));
    EXPECT_NE(nullptr, GET_ARG(&result, role.c_str()));
    EXPECT_NE(nullptr, GET_ARG(&result, (role + "MCSNSS.{i}.").c_str()));
    ASSERT_EQ(amxd_status_ok, default_get_supported(find(static_root), "", &reference));
    EXPECT_EQ(2U, expect_same_as_static(&result, &reference));

    // An instance path and a deeper object.
    amxc_var_clean(&result);
    const std::string mcsnss = usp_rel_path(role + "MCSNSS.");
    std::string indexed      = mcsnss;
    for (auto pos = indexed.find("{i}"); pos != std::string::npos; pos = indexed.find("{i}")) {
        indexed.replace(pos, 3, "1");
    }
    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), indexed, &result));
    EXPECT_EQ(1U, amxc_htable_size(amxc_var_constcast(amxc_htable_t, &result)));
    EXPECT_NE(nullptr, GET_ARG(&result, (role + "MCSNSS.{i}.").c_str()));
    EXPECT_EQ(1U, expect_same_as_static(&result, &reference));

    // An object that the mib does not define is rejected.
    amxc_var_clean(&result);
    EXPECT_EQ(amxd_status_object_not_found,
              get_supported(top_level_object(), usp_rel_path(role + "Unknown."), &result));

    amxc_var_clean(&reference);
    amxc_var_clean(&result);
}

TEST_F(SupportedDataModelTest, first_level_only)
{
    amxc_var_t result;
    amxc_var_t reference;
    amxc_var_init(&result);
    amxc_var_init(&reference);

    // On the object that the mibs extend: the mib objects are listed without their content.
    ASSERT_EQ(amxd_status_ok,
              get_supported(top_level_object(), usp_rel_path(capabilities_path), &result, true));
    ASSERT_EQ(amxd_status_ok, default_get_supported(find(static_root), "", &reference, true));
    for (const auto &name : {"WiFi6APRole.", "WiFi6bSTARole.", "WiFi7APRole.", "WiFi7bSTARole."}) {
        const auto path   = std::string(capabilities_path) + name;
        const auto *entry = GET_ARG(&result, path.c_str());
        ASSERT_NE(nullptr, entry) << path;
        EXPECT_EQ(nullptr, GET_ARG(entry, "supported_params")) << path;
    }
    EXPECT_EQ(
        nullptr,
        GET_ARG(&result, (std::string(capabilities_path) + "WiFi6APRole.MCSNSS.{i}.").c_str()));
    EXPECT_EQ(4U, expect_same_as_static(&result, &reference));

    // On the parent of that object: the mib objects are not listed.
    amxc_var_clean(&result);
    ASSERT_EQ(amxd_status_ok,
              get_supported(top_level_object(), usp_rel_path(radio_path), &result, true));
    EXPECT_NE(nullptr, GET_ARG(&result, capabilities_path));
    EXPECT_EQ(nullptr, GET_ARG(&result, (std::string(capabilities_path) + "WiFi6APRole.").c_str()));
    EXPECT_NE(nullptr, GET_ARG(&result, (std::string(radio_path) + "SpatialReuse.").c_str()));

    amxc_var_clean(&reference);
    amxc_var_clean(&result);
}

TEST_F(SupportedDataModelTest, call_on_inner_object)
{
    // A call on Network.Device.
    auto *device = find(DATAELEMENTS_ROOT_DM ".Network.Device.");
    ASSERT_NE(nullptr, device);
    amxc_var_t result;
    amxc_var_init(&result);
    ASSERT_EQ(amxd_status_ok, get_supported(device, "{i}.Radio.", &result));
    EXPECT_NE(nullptr, GET_ARG(&result, (std::string(radio_path) + "SpatialReuse.").c_str()));
    EXPECT_NE(nullptr,
              GET_ARG(&result, (std::string(bss_path) + "SetQoSManagementInput.").c_str()));
    EXPECT_EQ(nullptr, GET_ARG(&result, (std::string(device_path) + "bSTAMLD.").c_str()));

    amxc_var_clean(&result);
}

TEST_F(SupportedDataModelTest, attached_mib_does_not_change_result)
{
    amxc_var_t without_instances;
    amxc_var_t with_instances;
    amxc_var_init(&without_instances);
    amxc_var_init(&with_instances);
    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), "", &without_instances));

    ASSERT_EQ(DATAELEMENTS_ROOT_DM ".Network.Device.1",
              m_ambiorix->add_instance(DATAELEMENTS_ROOT_DM ".Network.Device"));
    ASSERT_EQ(DATAELEMENTS_ROOT_DM ".Network.Device.1.Radio.1",
              m_ambiorix->add_instance(DATAELEMENTS_ROOT_DM ".Network.Device.1.Radio"));
    ASSERT_TRUE(m_ambiorix->add_optional_subobject(
        DATAELEMENTS_ROOT_DM ".Network.Device.1.Radio.1.Capabilities.", "WiFi6APRole"));
    ASSERT_NE(nullptr,
              find(DATAELEMENTS_ROOT_DM ".Network.Device.1.Radio.1.Capabilities.WiFi6APRole."));

    // Neither the instances nor the attached mib change the supported data model.
    ASSERT_EQ(amxd_status_ok, get_supported(top_level_object(), "", &with_instances));
    int compare = -1;
    EXPECT_EQ(0, amxc_var_compare(&with_instances, &without_instances, &compare));
    EXPECT_EQ(0, compare);

    amxc_var_clean(&with_instances);
    amxc_var_clean(&without_instances);
}

TEST_F(SupportedDataModelTest, every_conditional_mib_is_listed)
{
    // Each mib is either applied to a defined object or in the list.
    std::set<std::string> applied;
    amxd_object_hierarchy_walk(
        amxd_dm_get_root(dm()), amxd_direction_down,
        [](amxd_object_t *object, int32_t, void *) { return object != find(static_root); },
        [](amxd_object_t *object, int32_t, void *priv) {
            char *mibs = amxd_object_get_mibs(object);
            if (!mibs) {
                return;
            }
            std::string names(mibs);
            free(mibs);
            size_t start = 0;
            while (start < names.size()) {
                const auto end = names.find(' ', start);
                static_cast<std::set<std::string> *>(priv)->insert(
                    names.substr(start, end - start));
                start = end == std::string::npos ? names.size() : end + 1;
            }
        },
        INT32_MAX, &applied);

    std::set<std::string> listed;
    for (const auto &optional : prplmesh::controller::actions::get_optional_subobject_list()) {
        listed.insert(optional.mib_name);
    }

    amxc_llist_for_each(it, &dm()->mibs)
    {
        const auto *mib        = amxc_llist_it_get_data(it, amxd_object_t, it);
        const std::string name = amxd_object_get_name(mib, AMXD_OBJECT_NAMED);
        if (applied.count(name)) {
            EXPECT_EQ(0U, listed.count(name)) << name << " is static and listed";
        } else {
            EXPECT_EQ(1U, listed.count(name)) << name << " is not listed";
        }
    }
}

} // namespace
