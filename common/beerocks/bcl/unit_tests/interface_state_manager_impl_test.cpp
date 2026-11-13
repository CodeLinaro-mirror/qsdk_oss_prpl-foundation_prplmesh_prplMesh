/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/network/interface_state_manager_impl.h>
#include <bcl/network/interface_state_monitor_mock.h>
#include <bcl/network/interface_state_reader_mock.h>

#include <bcl/beerocks_backport.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <vector>

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

/**
 * In this test, no state-changed event has occurred yet so interface state is obtained with an
 * explicit read through the interface state reader.
 */
TEST(InterfaceStateManagerImpl, read_state_should_succeed_before_event)
{
    auto monitor = std::make_unique<StrictMock<beerocks::net::InterfaceStateMonitorMock>>();
    auto reader  = std::make_unique<StrictMock<beerocks::net::InterfaceStateReaderMock>>();

    const char *iface_name          = "test_iface";
    const bool expected_iface_state = true;
    bool actual_iface_state         = false;

    EXPECT_CALL(*reader, read_state(iface_name, _))
        .WillOnce(Invoke([&](const std::string &iface_name, bool &iface_state) -> bool {
            iface_state = expected_iface_state;
            return true;
        }));

    beerocks::net::InterfaceStateManagerImpl interface_state_manager(std::move(monitor),
                                                                     std::move(reader));

    ASSERT_TRUE(interface_state_manager.read_state(iface_name, actual_iface_state));
    ASSERT_EQ(actual_iface_state, expected_iface_state);
}

/**
 * This test demonstrates that state is cached and only one explicit read is performed through the
 * interface state reader no matter how many times it is queried.
 */
TEST(InterfaceStateManagerImpl, read_state_should_succeed_after_read)
{
    auto monitor = std::make_unique<StrictMock<beerocks::net::InterfaceStateMonitorMock>>();
    auto reader  = std::make_unique<StrictMock<beerocks::net::InterfaceStateReaderMock>>();

    const char *iface_name          = "test_iface";
    const bool expected_iface_state = true;
    bool actual_iface_state         = false;

    EXPECT_CALL(*reader, read_state(iface_name, _))
        .WillOnce(Invoke([&](const std::string &iface_name, bool &iface_state) -> bool {
            iface_state = expected_iface_state;
            return true;
        }));

    beerocks::net::InterfaceStateManagerImpl interface_state_manager(std::move(monitor),
                                                                     std::move(reader));

    ASSERT_TRUE(interface_state_manager.read_state(iface_name, actual_iface_state));
    ASSERT_EQ(actual_iface_state, expected_iface_state);

    ASSERT_TRUE(interface_state_manager.read_state(iface_name, actual_iface_state));
    ASSERT_EQ(actual_iface_state, expected_iface_state);
}

/**
 * In this test, the interface state is obtained after a state-changed event (no explicit read
 * operation is required nor performed)
 */
TEST(InterfaceStateManagerImpl, read_state_should_succeed_after_event)
{
    auto monitor = std::make_unique<StrictMock<beerocks::net::InterfaceStateMonitorMock>>();
    auto reader  = std::make_unique<StrictMock<beerocks::net::InterfaceStateReaderMock>>();

    const char *iface_name          = "test_iface";
    const bool expected_iface_state = true;
    bool actual_iface_state         = false;

    // The monitor mock is needed to emulate that a state-changed event has occurred.
    // Since the unique_ptr to the monitor mock is moved into the interface state manager, it
    // is not available later. To overcome this problem, we use the raw pointer instead.
    auto monitor_raw_ptr = monitor.get();

    beerocks::net::InterfaceStateManagerImpl interface_state_manager(std::move(monitor),
                                                                     std::move(reader));

    monitor_raw_ptr->notify_state_changed(iface_name, expected_iface_state);
    ASSERT_TRUE(interface_state_manager.read_state(iface_name, actual_iface_state));
    ASSERT_EQ(actual_iface_state, expected_iface_state);

    monitor_raw_ptr->notify_state_changed(iface_name, !expected_iface_state);
    ASSERT_TRUE(interface_state_manager.read_state(iface_name, actual_iface_state));
    ASSERT_NE(actual_iface_state, expected_iface_state);
}

TEST(InterfaceStateManagerImpl, notify_state_changed_should_succeed)
{
    auto monitor = std::make_unique<StrictMock<beerocks::net::InterfaceStateMonitorMock>>();
    auto reader  = std::make_unique<StrictMock<beerocks::net::InterfaceStateReaderMock>>();

    const char *iface_name          = "test_iface";
    const bool expected_iface_state = true;
    bool actual_iface_state         = false;

    // The monitor mock is needed to emulate that a state-changed event has occurred.
    // Since the unique_ptr to the monitor mock is moved into the interface state manager, it
    // is not available later. To overcome this problem, we use the raw pointer instead.
    auto monitor_raw_ptr = monitor.get();

    beerocks::net::InterfaceStateManagerImpl interface_state_manager(std::move(monitor),
                                                                     std::move(reader));

    interface_state_manager.set_handler(
        [&](const std::string &iface_name, bool iface_state) { actual_iface_state = iface_state; });

    monitor_raw_ptr->notify_state_changed(iface_name, expected_iface_state);
    ASSERT_EQ(actual_iface_state, expected_iface_state);
}

/**
 * In this test, state-changed events have been lost. The state of every known interface is read
 * again and notified, even if it has not changed. An interface that is up is notified down and
 * then up, since it may have been created again. An interface that cannot be read any more is
 * notified as down.
 */
TEST(InterfaceStateManagerImpl, events_lost_should_notify_state_of_all_interfaces)
{
    auto monitor = std::make_unique<StrictMock<beerocks::net::InterfaceStateMonitorMock>>();
    auto reader  = std::make_unique<StrictMock<beerocks::net::InterfaceStateReaderMock>>();

    const char *iface_name_0 = "test_iface_0";
    const char *iface_name_1 = "test_iface_1";

    auto read_up = [](const std::string &iface_name, bool &iface_state) -> bool {
        iface_state = true;
        return true;
    };

    // Expectations for the initial read and for the read after the events have been lost.
    // Interfaces are read again in any order.
    EXPECT_CALL(*reader, read_state(iface_name_0, _))
        .WillOnce(Invoke(read_up))
        .WillOnce(Invoke(read_up));
    EXPECT_CALL(*reader, read_state(iface_name_1, _))
        .WillOnce(Invoke(read_up))
        .WillOnce(Return(false));

    // The monitor mock is needed to emulate that events have been lost.
    // Since the unique_ptr to the monitor mock is moved into the interface state manager, it
    // is not available later. To overcome this problem, we use the raw pointer instead.
    auto monitor_raw_ptr = monitor.get();

    beerocks::net::InterfaceStateManagerImpl interface_state_manager(std::move(monitor),
                                                                     std::move(reader));

    // Interfaces are notified in any order, so the sequence is recorded per interface
    std::map<std::string, std::vector<bool>> notified_iface_states;
    interface_state_manager.set_handler([&](const std::string &iface_name, bool iface_state) {
        notified_iface_states[iface_name].push_back(iface_state);
    });

    bool iface_state;
    ASSERT_TRUE(interface_state_manager.read_state(iface_name_0, iface_state));
    ASSERT_TRUE(interface_state_manager.read_state(iface_name_1, iface_state));
    ASSERT_TRUE(notified_iface_states.empty());

    // Emulate that events have been lost
    monitor_raw_ptr->notify_events_lost();

    std::map<std::string, std::vector<bool>> expected_iface_states{{iface_name_0, {false, true}},
                                                                   {iface_name_1, {false}}};
    ASSERT_EQ(notified_iface_states, expected_iface_states);

    // The cached state has been updated
    ASSERT_TRUE(interface_state_manager.read_state(iface_name_1, iface_state));
    ASSERT_FALSE(iface_state);
}
