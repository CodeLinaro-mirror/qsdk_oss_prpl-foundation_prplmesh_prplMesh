/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <ambiorix_impl.h>
#include <bcl/beerocks_event_loop_mock.h>

#include <amxb/amxb.h>
#include <amxb/amxb_be.h>
#include <amxd/amxd_action.h>
#include <amxd/amxd_dm.h>
#include <amxd/amxd_object_action.h>
#include <amxd/amxd_object_function.h>
#include <amxd/amxd_object_parameter.h>

#include "amxb_mock.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <thread>
#include <unistd.h>

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
constexpr auto g_param_name_int32      = "Int32";
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
                                "            %read-only int32 Int32 = -1;\n"
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

    amxd_dm_t *datamodel() { return m_datamodel; }

    bool dispatch_pending_signals()
    {
        if (!m_signal_handlers.on_read || m_signal_fd < 0) {
            return false;
        }
        // Exercise the handler installed by AmbiorixImpl, with no test-side AMX guard.
        for (unsigned int count = 0; count < 256; ++count) {
            pollfd descriptor{m_signal_fd, POLLIN, 0};
            auto ready = poll(&descriptor, 1, 0);
            if (ready == 0) {
                return true;
            }
            if (ready < 0 || !(descriptor.revents & POLLIN) ||
                !m_signal_handlers.on_read(m_signal_fd, *m_event_loop)) {
                return false;
            }
        }
        return false;
    }

private:
    std::shared_ptr<StrictMock<beerocks::EventLoopMock>> m_event_loop;
    OdlFile m_odl_file;
    StrictMock<AmbxMock> m_amxb_mock;
    amxd_dm_t *m_datamodel = nullptr;
    beerocks::EventLoop::EventHandlers m_signal_handlers;
    int m_signal_fd = -1;

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

        EXPECT_CALL(*m_event_loop, register_handlers(_, _))
            .WillRepeatedly([this](int fd, const beerocks::EventLoop::EventHandlers &handlers) {
                if (handlers.name == "ambiorix_signal") {
                    m_signal_fd       = fd;
                    m_signal_handlers = handlers;
                }
                return true;
            });
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

// Synchronization belongs to the test harness. Holding an AMXD action here is
// intentional; production code must never wait for another thread under AMX.
class ActionGate {
public:
    void hold()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_entered = true;
        m_condition.notify_all();
        m_timed_out =
            !m_condition.wait_for(lock, std::chrono::seconds(2), [&]() { return m_released; });
    }

    bool wait_until_entered()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_condition.wait_for(lock, std::chrono::seconds(2), [&]() { return m_entered; });
    }

    void attempting()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_attempting;
        m_condition.notify_all();
    }

    void completed()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_completed;
        m_overlapped = m_overlapped || !m_released;
        m_condition.notify_all();
    }

    bool remain_excluded(unsigned int expected_attempts)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_condition.wait_for(lock, std::chrono::seconds(2),
                                  [&]() { return m_attempting == expected_attempts; })) {
            return false;
        }
        // All contenders have reached their wrapper calls; none may finish
        // while the native action is paused. No scheduling sleeps are used.
        return !m_condition.wait_for(lock, std::chrono::milliseconds(100),
                                     [&]() { return m_completed != 0 || m_timed_out; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_released = true;
        m_condition.notify_all();
    }

    bool passed()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_entered && !m_timed_out && !m_overlapped;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_entered            = false;
    bool m_released           = false;
    bool m_timed_out          = false;
    bool m_overlapped         = false;
    unsigned int m_attempting = 0;
    unsigned int m_completed  = 0;
};

class ScenarioThreads {
public:
    explicit ScenarioThreads(ActionGate &gate) : m_gate(gate) {}
    ~ScenarioThreads()
    {
        m_gate.release();
        join();
    }

    void start(std::function<void()> operation) { m_threads.emplace_back(std::move(operation)); }

    void join()
    {
        for (auto &thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

private:
    ActionGate &m_gate;
    std::vector<std::thread> m_threads;
};

// A broken recursive guard can deadlock before the gate is reached. Run each
// scenario in a child process, with a watchdog independent of AMX/SIGALRM.
void run_concurrency_child(const std::function<bool()> &scenario)
{
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::thread watchdog([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(10), [&]() { return finished; })) {
            fputs("AMX concurrency scenario timed out\n", stderr);
            _exit(124);
        }
    });
    bool passed = scenario() && !::testing::Test::HasFailure();
    {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
    }
    condition.notify_all();
    watchdog.join();
    _exit(passed ? 0 : 1);
}

class ScopedAmxdAction {
public:
    ScopedAmxdAction(amxd_dm_t *dm, const std::string &path, amxd_action_t reason,
                     amxd_action_fn_t callback, void *priv)
        : m_reason(reason), m_callback(callback)
    {
        beerocks::AmxGuard setup_guard;
        m_object    = amxd_dm_findf(dm, "%s", path.c_str());
        m_installed = m_object &&
                      amxd_object_add_action_cb(m_object, reason, callback, priv) == amxd_status_ok;
    }

    ~ScopedAmxdAction()
    {
        if (m_installed) {
            beerocks::AmxGuard cleanup_guard;
            amxd_object_remove_action_cb(m_object, m_reason, m_callback);
        }
    }

    bool installed() const { return m_installed; }

private:
    amxd_object_t *m_object = nullptr;
    amxd_action_t m_reason;
    amxd_action_fn_t m_callback;
    bool m_installed = false;
};

class ScopedInstanceSlot {
public:
    ScopedInstanceSlot(amxd_dm_t *dm, amxp_slot_fn_t callback, void *priv)
        : m_dm(dm), m_callback(callback), m_priv(priv)
    {
        beerocks::AmxGuard setup_guard;
        m_installed =
            amxp_slot_connect(&dm->sigmngr, "dm:instance-added", nullptr, callback, priv) == 0;
    }

    ~ScopedInstanceSlot()
    {
        if (m_installed) {
            beerocks::AmxGuard cleanup_guard;
            amxp_slot_disconnect_with_priv(&m_dm->sigmngr, m_callback, m_priv);
        }
    }

    bool installed() const { return m_installed; }

private:
    amxd_dm_t *m_dm;
    amxp_slot_fn_t m_callback;
    void *m_priv;
    bool m_installed = false;
};

struct RegistrationProbe {
    RegistrationProbe(amxd_dm_t *dm, beerocks::nbapi::AmbiorixImpl &model,
                      ActionGate *gate = nullptr)
        : dm(dm), model(model), gate(gate)
    {
    }

    bool matches(amxd_object_t *object) const
    {
        char *path = amxd_object_get_path(object, AMXD_OBJECT_INDEXED | AMXD_OBJECT_TERMINATE);
        bool match = path && seed_path + "." == path;
        free(path);
        return match;
    }

    amxd_dm_t *dm;
    beerocks::nbapi::AmbiorixImpl &model;
    ActionGate *gate;
    std::string seed_path;
    std::atomic<bool> list_held{false};
    std::atomic<bool> registration_active{false};
    std::atomic<bool> registration_finished{false};
    amxd_status_t list_status = amxd_status_unknown_error;
    bool recursive_read       = false;
    bool recursive_write      = false;
};

amxd_status_t hold_registration_list(amxd_object_t *object, amxd_param_t *param,
                                     amxd_action_t reason, const amxc_var_t *args,
                                     amxc_var_t *retval, void *priv)
{
    auto &probe = *static_cast<RegistrationProbe *>(priv);
    if (probe.matches(object) && !probe.list_held.exchange(true)) {
        uint32_t value = 0;
        probe.recursive_read =
            probe.model.read_param(g_param_path, g_param_name_uint32, &value) && value == 1;
        probe.gate->hold();
    }
    return amxd_action_object_list(object, param, reason, args, retval, nullptr);
}

void register_added_instance(const char *, const amxc_var_t *data, void *priv)
{
    auto &probe  = *static_cast<RegistrationProbe *>(priv);
    auto *object = amxd_dm_signal_get_object(probe.dm, data);
    // The signal path identifies the template; native ubus registration
    // resolves its newly added instance using the separate index field.
    if (object) {
        object = amxd_object_get_instance(object, nullptr, GET_UINT32(data, "index"));
    }
    if (!object || !probe.matches(object) || probe.registration_finished.load()) {
        return;
    }
    probe.registration_active = true;
    amxc_var_t functions;
    amxc_var_init(&functions);
    // Ubus registration uses this real AMXD list action to discover RPC methods.
    probe.list_status = amxd_object_list_functions(object, &functions, amxd_dm_access_protected);
    amxc_var_clean(&functions);
    // The list action has unwound; a same-thread write is now legal, while
    // the installed signal handler still owns the outer production AMX guard.
    probe.recursive_write =
        probe.model.set(g_param_path, g_param_name_int32, int32_t(initial_value));
    probe.registration_active   = false;
    probe.registration_finished = true;
}

struct WriteProbe {
    WriteProbe(beerocks::nbapi::AmbiorixImpl &model, ActionGate &gate) : model(model), gate(gate) {}
    beerocks::nbapi::AmbiorixImpl &model;
    ActionGate &gate;
    std::atomic<bool> held{false};
    bool recursive_read = false;
};

amxd_status_t hold_object_write(amxd_object_t *object, amxd_param_t *param, amxd_action_t reason,
                                const amxc_var_t *args, amxc_var_t *retval, void *priv)
{
    auto &probe = *static_cast<WriteProbe *>(priv);
    if (!probe.held.exchange(true)) {
        uint32_t value = 0;
        probe.recursive_read =
            probe.model.read_param(g_param_path, g_param_name_uint32, &value) && value == 1;
        probe.gate.hold();
    }
    return amxd_action_object_write(object, param, reason, args, retval, nullptr);
}

class AmbiorixConcurrencyDeathTest : public AmbiorixTest {
protected:
    bool signal_registration_excludes_operations()
    {
        if (!dispatch_pending_signals()) {
            return false;
        }
        // A distinct pre-existing instance can be removed without invalidating
        // the instance whose registration/list action is paused.
        auto removable = m_ambiorix->add_instance(g_param_strings_path);
        if (removable.empty() ||
            !m_ambiorix->set(removable, g_param_name_string, std::string(g_param_value_bar)) ||
            !dispatch_pending_signals()) {
            return false;
        }
        ActionGate gate;
        RegistrationProbe registration(datamodel(), *m_ambiorix, &gate);
        ScopedAmxdAction list_action(datamodel(), g_param_strings_path, action_object_list,
                                     hold_registration_list, &registration);
        ScopedInstanceSlot slot(datamodel(), register_added_instance, &registration);
        if (!list_action.installed() || !slot.installed()) {
            return false;
        }
        registration.seed_path = m_ambiorix->add_instance(g_param_strings_path);
        if (registration.seed_path.empty()) {
            return false;
        }

        bool dispatched = false;
        bool set_ok     = false;
        bool read_ok    = false;
        bool add_ok     = false;
        bool remove_ok  = false;
        std::atomic<bool> callback_overlap{false};
        ScenarioThreads threads(gate);
        threads.start([&]() { dispatched = dispatch_pending_signals(); });
        bool entered = gate.wait_until_entered();
        if (entered) {
            auto complete = [&]() {
                if (registration.registration_active.load()) {
                    callback_overlap = true;
                }
                gate.completed();
            };
            threads.start([&, complete]() {
                gate.attempting();
                set_ok = m_ambiorix->set(g_param_path, g_param_name_string,
                                         std::string(g_param_value_foo));
                complete();
            });
            threads.start([&, complete]() {
                gate.attempting();
                uint32_t value = 0;
                read_ok =
                    m_ambiorix->read_param(g_param_path, g_param_name_uint32, &value) && value == 1;
                complete();
            });
            threads.start([&, complete]() {
                gate.attempting();
                auto added = m_ambiorix->add_instance(g_param_strings_path);
                // Observe add_instance itself before its independently guarded
                // follow-up write can hide a missing add operation guard.
                complete();
                add_ok = !added.empty() && m_ambiorix->set(added, g_param_name_string,
                                                           std::string(g_param_value_baz));
            });
            threads.start([&, complete]() {
                gate.attempting();
                remove_ok = m_ambiorix->remove_instance(g_param_strings_path, 1);
                complete();
            });
        }
        bool excluded = entered && gate.remain_excluded(4);
        gate.release();
        threads.join();

        std::string value;
        int32_t recursive_value = 0;
        auto search             = std::string(g_param_strings_path) + g_param_strings_search;
        bool final_state =
            m_ambiorix->read_param(g_param_path, g_param_name_string, &value) &&
            value == g_param_value_foo &&
            m_ambiorix->read_param(g_param_path, g_param_name_int32, &recursive_value) &&
            recursive_value == initial_value &&
            m_ambiorix->get_instance_index(search, g_param_value_bar) == 0 &&
            m_ambiorix->get_instance_index(search, g_param_value_baz) != 0;
        bool passed = excluded && gate.passed() && !callback_overlap && dispatched && set_ok &&
                      read_ok && add_ok && remove_ok && registration.recursive_read &&
                      registration.recursive_write && registration.registration_finished &&
                      registration.list_status == amxd_status_ok && final_state;
        if (!passed) {
            fprintf(stderr,
                    "signal/list overlap: excluded=%d set=%d read=%d add=%d remove=%d "
                    "list_status=%d recursion=%d/%d final_state=%d\n",
                    excluded, set_ok, read_ok, add_ok, remove_ok, int(registration.list_status),
                    registration.recursive_read, registration.recursive_write, final_state);
        }
        return passed;
    }

    bool transaction_excludes_signal_registration()
    {
        if (!dispatch_pending_signals()) {
            return false;
        }
        ActionGate gate;
        WriteProbe writer(*m_ambiorix, gate);
        RegistrationProbe registration(datamodel(), *m_ambiorix);
        ScopedAmxdAction write_action(datamodel(), g_param_path, action_object_write,
                                      hold_object_write, &writer);
        ScopedInstanceSlot slot(datamodel(), register_added_instance, &registration);
        if (!write_action.installed() || !slot.installed()) {
            return false;
        }
        registration.seed_path = m_ambiorix->add_instance(g_param_strings_path);
        if (registration.seed_path.empty()) {
            return false;
        }
        bool write_ok   = false;
        bool dispatched = false;
        ScenarioThreads threads(gate);
        threads.start([&]() {
            write_ok =
                m_ambiorix->set(g_param_path, g_param_name_string, std::string(g_param_value_foo));
        });
        bool entered = gate.wait_until_entered();
        if (entered) {
            threads.start([&]() {
                gate.attempting();
                dispatched = dispatch_pending_signals();
                gate.completed();
            });
        }
        // Check entry as well as completion: an unguarded signal handler might
        // enter the list callback and only block at its later recursive write.
        bool excluded = entered && gate.remain_excluded(1) &&
                        !registration.registration_active.load() &&
                        !registration.registration_finished.load();
        gate.release();
        threads.join();

        std::string value;
        bool final_state = m_ambiorix->read_param(g_param_path, g_param_name_string, &value) &&
                           value == g_param_value_foo;
        bool passed = excluded && gate.passed() && write_ok && dispatched &&
                      writer.recursive_read && registration.registration_finished &&
                      registration.recursive_write && registration.list_status == amxd_status_ok &&
                      final_state;
        if (!passed) {
            fprintf(stderr,
                    "transaction/signal overlap: excluded=%d write=%d dispatch=%d "
                    "list_status=%d recursion=%d/%d final_state=%d\n",
                    excluded, write_ok, dispatched, int(registration.list_status),
                    writer.recursive_read, registration.recursive_write, final_state);
        }
        return passed;
    }
};

TEST_F(AmbiorixConcurrencyDeathTest, signal_registration_excludes_concurrent_model_operations)
{
    ASSERT_EXIT(
        run_concurrency_child([this]() { return signal_registration_excludes_operations(); }),
        ::testing::ExitedWithCode(0), "");
}

TEST_F(AmbiorixConcurrencyDeathTest, transaction_excludes_concurrent_signal_registration)
{
    ASSERT_EXIT(
        run_concurrency_child([this]() { return transaction_excludes_signal_registration(); }),
        ::testing::ExitedWithCode(0), "");
}

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

TEST_F(AmbiorixTest, set_strings_rejects_invalid_parameter_without_partial_update)
{
    ASSERT_TRUE(m_ambiorix->set(g_param_path, g_param_name_string, std::string(g_param_value_bar)));
    // String sorts before Unknown: a sequence of individual setters would
    // publish Foo before discovering the invalid parameter.
    EXPECT_FALSE(
        m_ambiorix->set_strings(g_param_path, {{g_param_name_string, g_param_value_foo},
                                               {g_param_name_unknown, g_param_value_baz}}));
    std::string value;
    ASSERT_TRUE(m_ambiorix->read_param(g_param_path, g_param_name_string, &value));
    EXPECT_EQ(value, g_param_value_bar);

    // Rejecting the transaction must also leave the next operation usable.
    EXPECT_TRUE(m_ambiorix->set_strings(g_param_path, {{g_param_name_string, g_param_value_foo}}));
    ASSERT_TRUE(m_ambiorix->read_param(g_param_path, g_param_name_string, &value));
    EXPECT_EQ(value, g_param_value_foo);
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

} // namespace
