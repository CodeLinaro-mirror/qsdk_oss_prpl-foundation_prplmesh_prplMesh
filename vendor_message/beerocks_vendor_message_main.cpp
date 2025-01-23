/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2019-2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <agent_db.h>

#include <bcl/beerocks_event_loop_impl.h>
#include <bcl/beerocks_logging.h>
#include <bcl/beerocks_os_utils.h>
#include <bcl/beerocks_version.h>

#include <easylogging++.h>
#include <mapf/common/utils.h>
#include <sys/types.h>

#include "vendor_message_slave.h"

// Do not use this macro anywhere else in ire process
// It should only be there in one place in each executable module
BEEROCKS_INIT_BEEROCKS_VERSION

using namespace multi_vendor;
using namespace beerocks;

static bool g_running = true;
static int s_signal   = 0;

// Pointer to logger instance
static std::vector<std::shared_ptr<beerocks::logging>> g_loggers;

static void handle_signal()
{
    if (!s_signal)
        return;

    switch (s_signal) {

    // Terminate
    case SIGTERM:
    case SIGINT:
        LOG(INFO) << "Caught signal '" << strsignal(s_signal) << "' Exiting...";
        g_running = false;
        break;

    // Roll log file
    case SIGUSR1: {

        for (auto &logger : g_loggers) {
            CLOG(INFO, logger->get_logger_id()) << "LOG Roll Signal!";
            logger->apply_settings();
            CLOG(INFO, logger->get_logger_id()) << "--- Start of file after roll ---";
        }
        break;
    }

    default:
        LOG(WARNING) << "Unhandled Signal: '" << strsignal(s_signal) << "' Ignoring...";
        break;
    }

    s_signal = 0;
}

static void init_signals()
{
    // Signal handler function
    auto signal_handler = [](int signum) { s_signal = signum; };

    struct sigaction sigterm_action;
    sigterm_action.sa_handler = signal_handler;
    sigemptyset(&sigterm_action.sa_mask);
    sigterm_action.sa_flags = 0;
    sigaction(SIGTERM, &sigterm_action, NULL);

    struct sigaction sigint_action;
    sigint_action.sa_handler = signal_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);

    struct sigaction sigusr1_action;
    sigusr1_action.sa_handler = signal_handler;
    sigemptyset(&sigusr1_action.sa_mask);
    sigusr1_action.sa_flags = 0;
    sigaction(SIGUSR1, &sigusr1_action, NULL);
}

static void
fill_son_slave_config(const beerocks::config_file::sConfigSlave &beerocks_vendor_message_slave_conf,
                      multi_vendor::VendorMessageSlave::sVendorMessageConfig &vendor_message_conf)
{
    vendor_message_conf.temp_path    = beerocks_vendor_message_slave_conf.temp_path;
    vendor_message_conf.bridge_iface = beerocks_vendor_message_slave_conf.bridge_iface;
}

static std::shared_ptr<beerocks::logging>
init_logger(const std::string &file_name, const beerocks::config_file::SConfigLog &log_config,
            int argc, char **argv, const std::string &logger_id = std::string())
{
    auto logger = std::make_shared<beerocks::logging>(file_name, log_config, logger_id);
    if (!logger) {
        std::cout << "Failed to allocated logger to " << file_name;
        return std::shared_ptr<beerocks::logging>();
    }
    logger->apply_settings();
    CLOG(INFO, logger->get_logger_id())
        << std::endl
        << "Running " << file_name << " Version " << BEEROCKS_VERSION << " Build date "
        << BEEROCKS_BUILD_DATE << std::endl
        << std::endl;
    beerocks::version::log_version(argc, argv, logger->get_logger_id());

    // Redirect stdout / stderr to file
    if (logger->get_log_files_enabled()) {
        beerocks::os_utils::redirect_console_std(log_config.files_path + file_name + "_std.log");
    }

    return logger;
}

static std::shared_ptr<multi_vendor::VendorMessageSlave> start_vendor_message_thread(
    const beerocks::config_file::sConfigSlave &beerocks_vendor_message_slave_conf, int argc,
    char *argv[])
{
    std::string base_vendor_message_name(BEEROCKS_V_MESSAGE);

    // Init logger
    auto vendor_message_logger =
        init_logger(base_vendor_message_name, beerocks_vendor_message_slave_conf.sLog, argc, argv,
                    base_vendor_message_name);
    if (!vendor_message_logger) {
        return nullptr;
    }
    g_loggers.push_back(vendor_message_logger);

    multi_vendor::VendorMessageSlave::sVendorMessageConfig vendor_message_conf;

    fill_son_slave_config(beerocks_vendor_message_slave_conf, vendor_message_conf);

    auto vendor_message_slave = std::make_shared<multi_vendor::VendorMessageSlave>(
        vendor_message_conf, *vendor_message_logger);
    if (!vendor_message_slave) {
        CLOG(ERROR, vendor_message_logger->get_logger_id())
            << "beerocks::slave_thread allocating has failed!";
        return nullptr;
    }

    if (!vendor_message_slave->start()) {
        CLOG(ERROR, vendor_message_logger->get_logger_id())
            << "vendor_message_slave.start() has failed";
        return nullptr;
    }
    return vendor_message_slave;
}

bool createDaemon(beerocks::config_file::sConfigSlave &beerocks_vendor_message_slave_conf, int argc,
                  char *argv[])
{
    // Init logger vendor_message
    auto vendor_message_logger =
        init_logger(BEEROCKS_V_MESSAGE, beerocks_vendor_message_slave_conf.sLog, argc, argv);
    if (!vendor_message_logger) {
        return 1;
    }
    g_loggers.push_back(vendor_message_logger);

    // Create application event loop to wait for blocking I/O operations.
    auto event_loop = std::make_shared<beerocks::EventLoopImpl>();
    LOG_IF(!event_loop, FATAL) << "Unable to create event loop!";

    // Write pid file
    beerocks::os_utils::write_pid_file(beerocks_vendor_message_slave_conf.temp_path,
                                       BEEROCKS_V_MESSAGE);
    std::string pid_file_path = beerocks_vendor_message_slave_conf.temp_path + "pid/" +
                                BEEROCKS_V_MESSAGE; // for file touching

    auto vendor_message =
        start_vendor_message_thread(beerocks_vendor_message_slave_conf, argc, argv);
    if (!vendor_message) {
        LOG(ERROR) << "Failed to start vendor message thread";
        return 1;
    }

    while (g_running) {

        if (s_signal) {
            handle_signal();
            continue;
        }

        // Check if all vendor_message_slave are still running and break on error.
        if (!vendor_message->is_running()) {
            break;
        }
    }
    vendor_message->stop();
    LOG(DEBUG) << "Bye Bye!";
    return 0;
}

void os_utils::kill_pid(const std::string &path, const std::string &file_name)
{
    int pid_out;
    if (is_pid_running(path, file_name, &pid_out)) {
        LOG(DEBUG) << __FUNCTION__ << " SIGTERM pid=" << pid_out << std::endl;
        kill(pid_out, SIGTERM);
        auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        // wait until the process is down or timeout expires
        while (getpgid(pid_out) >= 0 && std::chrono::steady_clock::now() < timeout)
            ;

        if (getpgid(pid_out) >= 0) {
            LOG(DEBUG) << __FUNCTION__ << " SIGKILL pid=" << pid_out << std::endl;
            kill(pid_out, SIGKILL);
        }
    }
}

int main(int argc, char *argv[])
{

    std::cout << "Beerocks Vendor Message Process Start" << std::endl;

    init_signals();

    // read slave config file
    std::string vendor_message_slave_config_file_path =
        CONF_FILES_WRITABLE_PATH + std::string(BEEROCKS_AGENT) +
        ".conf"; //search first in platform-specific default directory
    beerocks::config_file::sConfigSlave beerocks_vendor_message_slave_conf;
    if (!beerocks::config_file::read_slave_config_file(vendor_message_slave_config_file_path,
                                                       beerocks_vendor_message_slave_conf)) {
        vendor_message_slave_config_file_path = mapf::utils::get_install_path() + "config/" +
                                                std::string(BEEROCKS_AGENT) +
                                                ".conf"; // if not found, search in beerocks path
        if (!beerocks::config_file::read_slave_config_file(vendor_message_slave_config_file_path,
                                                           beerocks_vendor_message_slave_conf)) {
            std::cout << "config file '" << vendor_message_slave_config_file_path << "' args error."
                      << std::endl;
            return 1;
        }
    }

    // killall running slave
    beerocks::os_utils::kill_pid(beerocks_vendor_message_slave_conf.temp_path + "pid/",
                                 std::string(BEEROCKS_V_MESSAGE));
    //Vendor Message Slave
    return createDaemon(beerocks_vendor_message_slave_conf, argc, argv);
}
