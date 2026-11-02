/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <ambiorix_impl.h>
#include <bcl/beerocks_event_loop_mock.h>
#include <tlvf/tlvftypes.h>

#include <amxb/amxb.h>
#include <amxb/amxb_be.h>
#include <amxd/amxd_dm.h>
#include <amxd/amxd_object_parameter.h>

#include "amxb_mock.h"
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>

using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrEq;
using ::testing::StrictMock;

namespace {

constexpr auto initial_value           = 42;
constexpr auto new_value               = 0xaabbccdd;
constexpr auto g_param_path_test       = "Test";
constexpr auto g_param_path_unknown    = "Test_Unknown";
constexpr auto g_param_path            = "Test.Container";
constexpr auto g_param_strings_path    = "Test.Strings";
constexpr auto g_param_strings_search  = ".[String == '%s'].";
constexpr auto g_param_name_int8       = "Int8";
constexpr auto g_param_name_int16      = "Int16";
constexpr auto g_param_name_int32      = "Int32";
constexpr auto g_param_name_uint8      = "Uint8";
constexpr auto g_param_name_uint16     = "Uint16";
constexpr auto g_param_name_uint32     = "Uint32";
constexpr auto g_param_name_int64      = "Int64";
constexpr auto g_param_name_uint64     = "Uint64";
constexpr auto g_param_name_bool       = "Bool";
constexpr auto g_param_name_double     = "Double";
constexpr auto g_param_name_string     = "String";
constexpr auto g_param_name_unknown    = "Unknown";
constexpr auto g_object_optional       = "Optional";
constexpr auto g_param_value_foo       = "Foo";
constexpr auto g_param_value_bar       = "Bar";
constexpr auto g_param_value_baz       = "Baz";
constexpr auto g_odl_filename_template = "ambiorix_test.odl.XXXXXX";
constexpr auto g_odl_contents          = "%config {\n"
                                "    backends = ["
                                "        \"/fake/backend.so\"\n"
                                "    ];\n"
                                "    uris = ["
                                "        \"mockbe:/path\"\n"
                                "    ];\n"
                                "}\n"
                                "%define {\n"
                                "    object Test {\n"
                                "        object Container {\n"
                                "            %read-only string String = \"\";\n"
                                "            %read-only datetime Datetime;\n"
                                "            %read-only int8 Int8 = -1;\n"
                                "            %read-only int16 Int16 = -1;\n"
                                "            %read-only int32 Int32 = -1;\n"
                                "            %read-only uint8 Uint8 = 1;\n"
                                "            %read-only uint16 Uint16 = 1;\n"
                                "            %read-only uint32 Uint32 = 1;\n"
                                "            %read-only int64 Int64 = -1;\n"
                                "            %read-only uint64 Uint64 = 1;\n"
                                "            %read-only bool Bool = false;\n"
                                "            %read-only double Double = \"99.9\";\n"
                                "        }\n"
                                "        mib Optional {\n"
                                "            %read-only string String = \"\";\n"
                                "        }\n"
                                "        object Strings [] {\n"
                                "            %read-only string String = \"\";\n"
                                "            counted with NumberOfStrings;\n"
                                "        }\n"
                                "    }\n"
                                "}\n";

class OdlFile {
    char m_odl_filename[FILENAME_MAX];

public:
    OdlFile(const std::string &odl_filename_template, const std::string &odl_contents)
    {
        snprintf(m_odl_filename, sizeof(m_odl_filename), "%s%s", testing::TempDir().c_str(),
                 odl_filename_template.c_str());
        int fd = mkstemp(m_odl_filename);
        EXPECT_GE(fd, 0);
        EXPECT_EQ(write(fd, odl_contents.c_str(), odl_contents.length()), odl_contents.length());
        EXPECT_EQ(close(fd), 0);
    }

    virtual ~OdlFile() { EXPECT_EQ(unlink(m_odl_filename), 0); }

    const char *get_odl_filename() { return m_odl_filename; }
};

class AmbiorixTest : public ::testing::Test {
public:
    AmbiorixTest()
        : m_odl_file(std::string(g_odl_filename_template), std::string(g_odl_contents)){};

protected:
    std::shared_ptr<beerocks::nbapi::Amxrt> amxrt;
    std::shared_ptr<beerocks::nbapi::AmbiorixImpl> m_ambiorix;

    amxd_object_t *find_object(const std::string &relative_path)
    {
        return amxd_dm_findf(m_datamodel, "%s", relative_path.c_str());
    }

private:
    std::shared_ptr<StrictMock<beerocks::EventLoopMock>> m_event_loop;
    OdlFile m_odl_file;
    StrictMock<AmbxMock> m_amxb_mock;
    amxd_dm_t *m_datamodel = nullptr;

    void SetUp() override
    {
        // init amxrt struct used by AmbiorixImpl
        amxrt        = std::make_shared<beerocks::nbapi::Amxrt>();
        m_event_loop = std::make_shared<StrictMock<beerocks::EventLoopMock>>();
        m_ambiorix   = std::make_shared<beerocks::nbapi::AmbiorixImpl>(
            m_event_loop, std::vector<beerocks::nbapi::sActionsCallback>(),
            std::vector<beerocks::nbapi::sEvents>(), std::vector<beerocks::nbapi::sFunctions>());

        EXPECT_CALL(m_amxb_mock, amxb_be_load(_)).WillRepeatedly(Return(0));
        EXPECT_CALL(m_amxb_mock, amxb_connect(_, _)).WillRepeatedly(Return(0));
        // fetch datamodel pointer from amxb_register call
        EXPECT_CALL(m_amxb_mock, amxb_register(_, _))
            .WillRepeatedly(DoAll(SaveArg<1>(&m_datamodel), Return(0)));
        EXPECT_CALL(m_amxb_mock, amxb_get_fd(_)).WillRepeatedly(Return(42));

        EXPECT_CALL(*m_event_loop, register_handlers(_, _)).WillRepeatedly(Return(true));
        EXPECT_TRUE(m_ambiorix->init(std::string(m_odl_file.get_odl_filename())));
        ASSERT_TRUE(m_datamodel != nullptr);
    }

    void TearDown() override
    {
        EXPECT_CALL(m_amxb_mock, amxb_free(_)).WillRepeatedly(Return());
        EXPECT_CALL(m_amxb_mock, amxb_be_remove_all()).WillRepeatedly(Return());

        EXPECT_CALL(*m_event_loop, remove_handlers(_)).Times(2).WillRepeatedly(Return(true));

        // we need new instance of nbapi for every single test
        // thus manually release pointer managed by shared_ptr
        // Calling reset() in turn calls the object's destructor so previous expectations are satisfied.
        m_ambiorix.reset();
        amxrt.reset();
    }
};

TEST_F(AmbiorixTest, test_instance)
{
    const auto search_path = std::string(g_param_strings_path) + g_param_strings_search;
    // no instance exists
    EXPECT_EQ(0, m_ambiorix->get_instance_index(search_path, g_param_value_bar));
    EXPECT_EQ(0, m_ambiorix->get_instance_index(search_path, g_param_value_foo));
    EXPECT_EQ(0, m_ambiorix->get_instance_index(search_path, g_param_value_baz));

    // let us add some instances
    EXPECT_EQ(std::string(g_param_strings_path) + ".1",
              m_ambiorix->add_instance(std::string(g_param_strings_path)));
    EXPECT_EQ(std::string(g_param_strings_path) + ".2",
              m_ambiorix->add_instance(std::string(g_param_strings_path)));
    EXPECT_EQ(std::string(g_param_strings_path) + ".3",
              m_ambiorix->add_instance(std::string(g_param_strings_path)));

    // and set keys
    EXPECT_TRUE(m_ambiorix->set(std::string(g_param_strings_path) + ".1", g_param_name_string,
                                std::string(g_param_value_foo)));
    EXPECT_TRUE(m_ambiorix->set(std::string(g_param_strings_path) + ".3", g_param_name_string,
                                std::string(g_param_value_baz)));
    EXPECT_TRUE(m_ambiorix->set(std::string(g_param_strings_path) + ".2", g_param_name_string,
                                std::string(g_param_value_bar)));

    // check if instances were added correctly
    EXPECT_EQ(2, m_ambiorix->get_instance_index(search_path, g_param_value_bar));
    EXPECT_EQ(1, m_ambiorix->get_instance_index(search_path, g_param_value_foo));
    EXPECT_EQ(3, m_ambiorix->get_instance_index(search_path, g_param_value_baz));

    // remove instance 2
    EXPECT_TRUE(m_ambiorix->remove_instance(std::string(g_param_strings_path), 2));
    EXPECT_EQ(0, m_ambiorix->get_instance_index(search_path, g_param_value_bar));
    EXPECT_EQ(1, m_ambiorix->get_instance_index(search_path, g_param_value_foo));
    EXPECT_EQ(3, m_ambiorix->get_instance_index(search_path, g_param_value_baz));

    // remove all instances
    EXPECT_TRUE(m_ambiorix->remove_all_instances(std::string(g_param_strings_path)));

    // check if instances were removed
    EXPECT_EQ(0, m_ambiorix->get_instance_index(search_path, g_param_value_bar));
    EXPECT_EQ(0, m_ambiorix->get_instance_index(search_path, g_param_value_foo));
    EXPECT_EQ(0, m_ambiorix->get_instance_index(search_path, g_param_value_bar));
}

TEST_F(AmbiorixTest, test_optional_subobject)
{
    //must fail because path does not exists
    EXPECT_FALSE(m_ambiorix->add_optional_subobject(g_param_path_unknown, g_object_optional));

    //success
    EXPECT_TRUE(m_ambiorix->add_optional_subobject(g_param_path_test, g_object_optional));

    // must fail, because of duplicate
    EXPECT_FALSE(m_ambiorix->add_optional_subobject(g_param_path_test, g_object_optional));

    //must fail because path does not exists
    EXPECT_FALSE(m_ambiorix->remove_optional_subobject(g_param_path_unknown, g_object_optional));

    //success
    EXPECT_TRUE(m_ambiorix->remove_optional_subobject(g_param_path_test, g_object_optional));

    // should fail, because already removed
    // ToDo: does not fail!!!
    //EXPECT_FALSE(m_ambiorix->remove_optional_subobject(g_ParamPathTest, g_ObjectOptional));
}

TEST_F(AmbiorixTest, set_string_should_succeed)
{
    amxd_object_t *obj = find_object(g_param_path);
    ASSERT_TRUE(obj);
    EXPECT_EQ(amxd_object_set_cstring_t(obj, g_param_name_string, g_param_value_bar),
              amxd_status_ok);
    EXPECT_TRUE(m_ambiorix->set(g_param_path, g_param_name_string, std::string(g_param_value_foo)));
    amxd_status_t status;
    char *value = amxd_object_get_cstring_t(obj, g_param_name_string, &status);
    EXPECT_EQ(status, amxd_status_ok);
    EXPECT_STREQ(value, g_param_value_foo);
    free(value);
}

TEST_F(AmbiorixTest, set_string_should_fail)
{
    EXPECT_FALSE(
        m_ambiorix->set(g_param_path, g_param_name_unknown, std::string(g_param_value_foo)));
}

/*
 * Add a test for each instance of the set() function.
 * Ideally, we'd use a parameterized test, but that is not possible when
 * the type itself varies. So instead use a template class that defines the
 * test itself, and instantiate it for the different types.
 */
template <class T> class AmbiorixTestSetter : public AmbiorixTest {
protected:
    using setter_t = std::function<amxd_status_t(amxd_object_t *, const char *name, const T)>;
    using getter_t = std::function<T(amxd_object_t *, const char *name, amxd_status_t *)>;
    void set_should_succeed(const std::string &parameter_name, T initial_value, T new_value,
                            setter_t setter, getter_t getter)
    {
        amxd_object_t *obj = find_object(g_param_path);
        ASSERT_TRUE(obj);
        EXPECT_EQ(setter(obj, parameter_name.c_str(), initial_value), amxd_status_ok);
        EXPECT_TRUE(m_ambiorix->set(g_param_path, parameter_name, new_value));
        amxd_status_t status;
        T value = getter(obj, parameter_name.c_str(), &status);
        EXPECT_EQ(status, amxd_status_ok);
        EXPECT_EQ(value, new_value);
    }
    void set_should_fail(const std::string &parameter_name, T new_value)
    {
        EXPECT_FALSE(m_ambiorix->set(g_param_path, parameter_name, new_value));
    }
};

class AmbiorixTestSetterInt32 : public AmbiorixTestSetter<int32_t> {
};
TEST_F(AmbiorixTestSetterInt32, set_int32_should_succeed)
{
    set_should_succeed(g_param_name_int32, initial_value, new_value, amxd_object_set_int32_t,
                       amxd_object_get_int32_t);
}
TEST_F(AmbiorixTestSetterInt32, set_int32_should_fail)
{
    set_should_fail(g_param_name_unknown, new_value);
}

class AmbiorixTestSetterUint32 : public AmbiorixTestSetter<uint32_t> {
};
TEST_F(AmbiorixTestSetterUint32, set_uint32_should_succeed)
{
    set_should_succeed(g_param_name_uint32, initial_value, new_value, amxd_object_set_uint32_t,
                       amxd_object_get_uint32_t);
}
TEST_F(AmbiorixTestSetterUint32, set_uint32_should_fail)
{
    set_should_fail(g_param_name_unknown, new_value);
}

class AmbiorixTestSetterInt64 : public AmbiorixTestSetter<int64_t> {
};
TEST_F(AmbiorixTestSetterInt64, set_int64_should_succeed)
{
    set_should_succeed(g_param_name_int64, initial_value, new_value, amxd_object_set_int64_t,
                       amxd_object_get_int64_t);
}
TEST_F(AmbiorixTestSetterInt64, set_int64_should_fail)
{
    set_should_fail(g_param_name_unknown, new_value);
}

class AmbiorixTestSetterUint64 : public AmbiorixTestSetter<uint64_t> {
};
TEST_F(AmbiorixTestSetterUint64, set_uint64_should_succeed)
{
    set_should_succeed(g_param_name_uint64, initial_value, new_value, amxd_object_set_uint64_t,
                       amxd_object_get_uint64_t);
}
TEST_F(AmbiorixTestSetterUint64, set_uint64_should_fail)
{
    set_should_fail(g_param_name_unknown, new_value);
}

class AmbiorixTestSetterBool : public AmbiorixTestSetter<bool> {
};
TEST_F(AmbiorixTestSetterBool, set_int32_should_succeed)
{
    set_should_succeed(g_param_name_bool, initial_value, new_value, amxd_object_set_bool,
                       amxd_object_get_bool);
}
TEST_F(AmbiorixTestSetterBool, set_int32_should_fail)
{
    set_should_fail(g_param_name_unknown, new_value);
}

class AmbiorixTestSetterDouble : public AmbiorixTestSetter<double> {
};
TEST_F(AmbiorixTestSetterDouble, set_uint32_should_succeed)
{
    set_should_succeed(g_param_name_double, initial_value, new_value, amxd_object_set_double,
                       amxd_object_get_double);
}
TEST_F(AmbiorixTestSetterDouble, set_uint32_should_fail)
{
    set_should_fail(g_param_name_unknown, new_value);
}

/*
 * AmbiorixTransImpl tests
 * The constructor is private; transactions are obtained via AmbiorixImpl::create_transaction().
 */
TEST_F(AmbiorixTest, create_transaction_should_succeed)
{
    auto trans = m_ambiorix->create_transaction(g_param_path);
    ASSERT_NE(trans, nullptr);
}

TEST_F(AmbiorixTest, create_transaction_should_fail)
{
    auto trans = m_ambiorix->create_transaction(g_param_path_unknown);
    EXPECT_EQ(trans, nullptr);
}

TEST_F(AmbiorixTest, transaction_apply_without_changes_should_succeed)
{
    auto trans = m_ambiorix->create_transaction(g_param_path);
    ASSERT_NE(trans, nullptr);
    EXPECT_TRUE(trans->apply());
}

TEST_F(AmbiorixTest, transaction_apply_twice_should_fail)
{
    auto trans = m_ambiorix->create_transaction(g_param_path);
    ASSERT_NE(trans, nullptr);
    EXPECT_TRUE(trans->apply());
    // apply() cleans the transaction; a second apply must fail
    EXPECT_FALSE(trans->apply());
}

TEST_F(AmbiorixTest, transaction_set_string_should_succeed)
{
    amxd_object_t *obj = find_object(g_param_path);
    ASSERT_TRUE(obj);
    EXPECT_EQ(amxd_object_set_cstring_t(obj, g_param_name_string, g_param_value_bar),
              amxd_status_ok);

    auto trans = m_ambiorix->create_transaction(g_param_path);
    ASSERT_NE(trans, nullptr);
    EXPECT_TRUE(trans->set(g_param_name_string, std::string(g_param_value_foo)));
    EXPECT_TRUE(trans->apply());

    amxd_status_t status;
    char *value = amxd_object_get_cstring_t(obj, g_param_name_string, &status);
    EXPECT_EQ(status, amxd_status_ok);
    EXPECT_STREQ(value, g_param_value_foo);
    free(value);
}

TEST_F(AmbiorixTest, transaction_set_mac_should_succeed)
{
    constexpr auto mac_str = "aa:bb:cc:dd:ee:ff";
    const sMacAddr mac     = tlvf::mac_from_string(mac_str);

    amxd_object_t *obj = find_object(g_param_path);
    ASSERT_TRUE(obj);

    auto trans = m_ambiorix->create_transaction(g_param_path);
    ASSERT_NE(trans, nullptr);
    EXPECT_TRUE(trans->set(g_param_name_string, mac));
    EXPECT_TRUE(trans->apply());

    amxd_status_t status;
    char *value = amxd_object_get_cstring_t(obj, g_param_name_string, &status);
    EXPECT_EQ(status, amxd_status_ok);
    EXPECT_STREQ(value, mac_str);
    free(value);
}

TEST_F(AmbiorixTest, transaction_set_multiple_params_should_succeed)
{
    amxd_object_t *obj = find_object(g_param_path);
    ASSERT_TRUE(obj);

    auto trans = m_ambiorix->create_transaction(g_param_path);
    ASSERT_NE(trans, nullptr);
    EXPECT_TRUE(trans->set(g_param_name_string, std::string(g_param_value_baz)));
    EXPECT_TRUE(trans->set(g_param_name_int32, static_cast<int32_t>(new_value)));
    EXPECT_TRUE(trans->set(g_param_name_uint32, static_cast<uint32_t>(new_value)));
    EXPECT_TRUE(trans->set(g_param_name_bool, true));
    EXPECT_TRUE(trans->set(g_param_name_double, 12.5));
    EXPECT_TRUE(trans->apply());

    amxd_status_t status;
    char *str_value = amxd_object_get_cstring_t(obj, g_param_name_string, &status);
    EXPECT_EQ(status, amxd_status_ok);
    EXPECT_STREQ(str_value, g_param_value_baz);
    free(str_value);

    EXPECT_EQ(amxd_object_get_int32_t(obj, g_param_name_int32, &status),
              static_cast<int32_t>(new_value));
    EXPECT_EQ(status, amxd_status_ok);
    EXPECT_EQ(amxd_object_get_uint32_t(obj, g_param_name_uint32, &status),
              static_cast<uint32_t>(new_value));
    EXPECT_EQ(status, amxd_status_ok);
    EXPECT_TRUE(amxd_object_get_bool(obj, g_param_name_bool, &status));
    EXPECT_EQ(status, amxd_status_ok);
    EXPECT_DOUBLE_EQ(amxd_object_get_double(obj, g_param_name_double, &status), 12.5);
    EXPECT_EQ(status, amxd_status_ok);
}

TEST_F(AmbiorixTest, transaction_set_unknown_param_apply_should_fail)
{
    auto trans = m_ambiorix->create_transaction(g_param_path);
    ASSERT_NE(trans, nullptr);
    // Queuing an unknown parameter typically succeeds; applying the transaction must fail.
    EXPECT_TRUE(trans->set(g_param_name_unknown, std::string(g_param_value_foo)));
    EXPECT_FALSE(trans->apply());
}

template <class T> class AmbiorixTransTestSetter : public AmbiorixTest {
protected:
    using getter_t = std::function<T(amxd_object_t *, const char *name, amxd_status_t *)>;
    void set_should_succeed(const std::string &parameter_name, T new_value, getter_t getter)
    {
        amxd_object_t *obj = find_object(g_param_path);
        ASSERT_TRUE(obj);

        auto trans = m_ambiorix->create_transaction(g_param_path);
        ASSERT_NE(trans, nullptr);
        EXPECT_TRUE(trans->set(parameter_name, new_value));
        EXPECT_TRUE(trans->apply());

        amxd_status_t status;
        T value = getter(obj, parameter_name.c_str(), &status);
        EXPECT_EQ(status, amxd_status_ok);
        EXPECT_EQ(value, new_value);
    }
};

class AmbiorixTransTestSetterInt32 : public AmbiorixTransTestSetter<int32_t> {
};
TEST_F(AmbiorixTransTestSetterInt32, set_int32_should_succeed)
{
    set_should_succeed(g_param_name_int32, static_cast<int32_t>(new_value),
                       amxd_object_get_int32_t);
}

class AmbiorixTransTestSetterUint32 : public AmbiorixTransTestSetter<uint32_t> {
};
TEST_F(AmbiorixTransTestSetterUint32, set_uint32_should_succeed)
{
    set_should_succeed(g_param_name_uint32, static_cast<uint32_t>(new_value),
                       amxd_object_get_uint32_t);
}

class AmbiorixTransTestSetterInt64 : public AmbiorixTransTestSetter<int64_t> {
};
TEST_F(AmbiorixTransTestSetterInt64, set_int64_should_succeed)
{
    set_should_succeed(g_param_name_int64, static_cast<int64_t>(new_value),
                       amxd_object_get_int64_t);
}

class AmbiorixTransTestSetterUint64 : public AmbiorixTransTestSetter<uint64_t> {
};
TEST_F(AmbiorixTransTestSetterUint64, set_uint64_should_succeed)
{
    set_should_succeed(g_param_name_uint64, static_cast<uint64_t>(new_value),
                       amxd_object_get_uint64_t);
}

class AmbiorixTransTestSetterBool : public AmbiorixTransTestSetter<bool> {
};
TEST_F(AmbiorixTransTestSetterBool, set_bool_should_succeed)
{
    set_should_succeed(g_param_name_bool, true, amxd_object_get_bool);
}

class AmbiorixTransTestSetterDouble : public AmbiorixTransTestSetter<double> {
};
TEST_F(AmbiorixTransTestSetterDouble, set_double_should_succeed)
{
    set_should_succeed(g_param_name_double, 42.5, amxd_object_get_double);
}

TEST_F(AmbiorixTest, transaction_set_float_should_succeed)
{
    amxd_object_t *obj = find_object(g_param_path);
    ASSERT_TRUE(obj);

    auto trans = m_ambiorix->create_transaction(g_param_path);
    ASSERT_NE(trans, nullptr);
    EXPECT_TRUE(trans->set(g_param_name_double, 3.5f));
    EXPECT_TRUE(trans->apply());

    amxd_status_t status;
    EXPECT_FLOAT_EQ(static_cast<float>(amxd_object_get_double(obj, g_param_name_double, &status)),
                    3.5f);
    EXPECT_EQ(status, amxd_status_ok);
}

class AmbiorixTransTestSetterInt8 : public AmbiorixTransTestSetter<int8_t> {
};
TEST_F(AmbiorixTransTestSetterInt8, set_int8_should_succeed)
{
    set_should_succeed(g_param_name_int8, static_cast<int8_t>(-7), amxd_object_get_int8_t);
}

class AmbiorixTransTestSetterInt16 : public AmbiorixTransTestSetter<int16_t> {
};
TEST_F(AmbiorixTransTestSetterInt16, set_int16_should_succeed)
{
    set_should_succeed(g_param_name_int16, static_cast<int16_t>(-1234), amxd_object_get_int16_t);
}

class AmbiorixTransTestSetterUint8 : public AmbiorixTransTestSetter<uint8_t> {
};
TEST_F(AmbiorixTransTestSetterUint8, set_uint8_should_succeed)
{
    set_should_succeed(g_param_name_uint8, static_cast<uint8_t>(200), amxd_object_get_uint8_t);
}

class AmbiorixTransTestSetterUint16 : public AmbiorixTransTestSetter<uint16_t> {
};
TEST_F(AmbiorixTransTestSetterUint16, set_uint16_should_succeed)
{
    set_should_succeed(g_param_name_uint16, static_cast<uint16_t>(40000), amxd_object_get_uint16_t);
}

} // namespace
