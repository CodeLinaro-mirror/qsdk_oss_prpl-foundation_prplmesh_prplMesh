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
#include <bcl/beerocks_version.h>
#include <bcl/network/file_descriptor.h>
#include <bpl/bpl.h>

#include <easylogging++.h>
#include <mapf/common/utils.h>
#include <sys/types.h>

#include "ambiorix_impl.h"
#include "vendor_message_slave.h"

// Do not use this macro anywhere else in ire process
// It should only be there in one place in each executable module
BEEROCKS_INIT_BEEROCKS_VERSION

using namespace vendor_message;
using namespace beerocks;

static bool g_running = true;

// Pointer to logger instance
static std::vector<std::shared_ptr<beerocks::logging>> g_loggers;

static void handle_signal(int signal_num)
{
    if (!signal_num)
        return;

    switch (signal_num) {

    // Terminate
    case SIGTERM:
    case SIGINT:
        LOG(INFO) << "Caught signal '" << strsignal(signal_num) << "' Exiting...";
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
        LOG(WARNING) << "Unhandled Signal: '" << strsignal(signal_num) << "' Ignoring...";
        break;
    }
}

/**
 * @brief Set up process signal handling using signalfd.
 *
 * This function configures synchronous handling of process signals by:
 *  - Initializing a signal mask for the signals of interest (SIGTERM,
 *    SIGUSR1, SIGINT)
 *  - Blocking these signals from default asynchronous delivery
 *  - Creating a non-blocking signalfd file descriptor for the blocked signals
 *
 * The returned signal file descriptor can be monitored using epoll
 * and allows signals to be handled in the main event loop like regular I/O
 * events, avoiding traditional signal handlers.
 *
 * @param[out] sig_mask Signal set containing the blocked signals.
 * @param[out] signal_fd File descriptor used to receive signal events.
 * @returns true on success
 *          false otherwise
 */

static bool setup_signals(sigset_t &sig_mask, int &signal_fd)
{
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGTERM);
    sigaddset(&sig_mask, SIGINT);
    sigaddset(&sig_mask, SIGUSR1);

    if (sigprocmask(SIG_BLOCK, &sig_mask, nullptr) == -1) {
        LOG(ERROR) << "sigprocmask blocking async delivery of"
                      " signals failed";
        return false;
    }

    signal_fd = signalfd(-1, &sig_mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd == beerocks::net::FileDescriptor::invalid_descriptor) {
        LOG(ERROR) << "signalfd failed";
        return false;
    }
    return true;
}

/**
 * @brief Clean up signalfd-based signal handling.
 *
 * This function reverses the setup performed by setup_signals() by:
 *  - Unblocking the previously blocked signals
 *  - Closing the associated signalfd file descriptor
 *  - Resetting the file descriptor to an invalid state
 *
 * It should be called during graceful shutdown or error handling
 * to ensure proper signal delivery and resource cleanup.
 *
 * @param[in] sig_mask Signal set containing the signals to unblock.
 * @param[in,out] signal_fd Signalfd file descriptor to close and invalidate.
 *
 * @note The caller must ensure no other threads are using signal_fd.
 */
static void cleanup_signals(const sigset_t &sig_mask, int &signal_fd)
{
    if (signal_fd != beerocks::net::FileDescriptor::invalid_descriptor) {
        if (sigprocmask(SIG_UNBLOCK, &sig_mask, nullptr) == -1) {
            LOG(ERROR) << "sigprocmask signals unblock failed";
        }
        close(signal_fd);
        signal_fd = beerocks::net::FileDescriptor::invalid_descriptor;
    }
}

static void
fill_son_slave_config(const beerocks::config_file::sConfigSlave &beerocks_vendor_message_slave_conf,
                      vendor_message::VendorMessageSlave::sVendorMessageConfig &vendor_message_conf)
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

static std::shared_ptr<vendor_message::VendorMessageSlave> start_vendor_message_thread(
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

    vendor_message::VendorMessageSlave::sVendorMessageConfig vendor_message_conf;

    fill_son_slave_config(beerocks_vendor_message_slave_conf, vendor_message_conf);

    auto vendor_message_slave = std::make_shared<vendor_message::VendorMessageSlave>(
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

    int signal_fd = beerocks::net::FileDescriptor::invalid_descriptor;
    sigset_t sig_mask{};

    if (!setup_signals(sig_mask, signal_fd)) {
        return 1;
    }

    // Define the event handlers for SIGTERM and SIGUSR1 signals
    beerocks::EventLoop::EventHandlers handlers;
    handlers.name = "vendor_message_signal_handler";

    handlers.on_read = [&](int fd, EventLoop &loop) {
        signalfd_siginfo si{};
        ssize_t bytes = read(fd, &si, sizeof(si));

        if (bytes != sizeof(si)) {
            LOG(ERROR) << "failed to read from signalfd";
        } else {
            handle_signal(si.ssi_signo);
        }
        return true;
    };

    handlers.on_error = [&](int fd, EventLoop &loop) {
        LOG(ERROR) << "vendor_message_signal events error! on fd " << fd;
        event_loop->remove_handlers(fd);
        cleanup_signals(sig_mask, signal_fd);
        return false;
    };

    // Register the event handlers with epoll
    if (!event_loop->register_handlers(signal_fd, handlers)) {
        cleanup_signals(sig_mask, signal_fd);
        LOG(ERROR) << "Unable to register signal handlers for Vendor_message!";
        return 1;
    }

    // Write pid file
    beerocks::os_utils::write_pid_file(beerocks_vendor_message_slave_conf.temp_path,
                                       BEEROCKS_V_MESSAGE);
    std::string pid_file_path = beerocks_vendor_message_slave_conf.temp_path + "pid/" +
                                BEEROCKS_V_MESSAGE; // for file touching

    auto vendor_message =
        start_vendor_message_thread(beerocks_vendor_message_slave_conf, argc, argv);
    if (!vendor_message) {
        event_loop->remove_handlers(signal_fd);
        cleanup_signals(sig_mask, signal_fd);
        LOG(ERROR) << "Failed to start vendor message thread";
        return 1;
    }

    auto touch_time_stamp_timeout = std::chrono::steady_clock::now();
    while (g_running) {

        if (std::chrono::steady_clock::now() > touch_time_stamp_timeout) {
            beerocks::os_utils::touch_pid_file(pid_file_path);
            touch_time_stamp_timeout = std::chrono::steady_clock::now() +
                                       std::chrono::seconds(beerocks::TOUCH_PID_TIMEOUT_SECONDS);
        }

        // Check if all vendor_message_slave are still running and break on error.
        if (!vendor_message->is_running()) {
            break;
        }

        // Run application event loop and break on error.
        if (event_loop->run() < 0) {
            LOG(ERROR) << "Event loop failure!";
            break;
        }
    }

    event_loop->remove_handlers(signal_fd);
    cleanup_signals(sig_mask, signal_fd);
    vendor_message->stop();

    LOG(DEBUG) << "Bye Bye!";
    return 0;
}

int main(int argc, char *argv[])
{

    std::cout << "Beerocks Vendor Message Process Start" << std::endl;

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

    // Initialize the BPL (Beerocks Platform Library)
    if (beerocks::bpl::bpl_init() < 0) {
        LOG(ERROR) << "Failed to initialize BPL!";
        return false;
    }

    // killall running slave
    beerocks::os_utils::kill_pid(beerocks_vendor_message_slave_conf.temp_path + "pid/",
                                 std::string(BEEROCKS_V_MESSAGE));
    //Vendor Message Slave
    return createDaemon(beerocks_vendor_message_slave_conf, argc, argv);
}
