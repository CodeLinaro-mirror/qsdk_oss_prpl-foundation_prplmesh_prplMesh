/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef AMBIORIX_RT_H
#define AMBIORIX_RT_H
#include <bcl/beerocks_event_loop.h>
#include <easylogging++.h>
#include <map>
#include <memory>
#include <set>
#include <string>

// Ambiorix
#include <amxrt/amxrt.h>

namespace beerocks {
namespace nbapi {

static int index = 0;

class Amxrt {

public:
    explicit Amxrt(std::shared_ptr<EventLoop> event_loop) : m_event_loop(event_loop)
    {
        amxrt_new();
        for (const auto *signal : {"connection-added", "listen-added"}) {
            LOG_IF(amxp_slot_connect(nullptr, signal, nullptr, connection_added, this) != 0, FATAL)
                << "Failed to connect " << signal;
        }
        for (const auto *signal : {"connection-deleted", "listen-deleted"}) {
            LOG_IF(amxp_slot_connect(nullptr, signal, nullptr, connection_deleted, this) != 0, FATAL)
                << "Failed to connect " << signal;
        }
    }

    ~Amxrt()
    {
        amxrt_stop();
        amxp_slot_disconnect_with_priv(nullptr, connection_added, this);
        amxp_slot_disconnect_with_priv(nullptr, connection_deleted, this);
        for (auto fd : m_registered_fds) {
            m_event_loop->remove_handlers(fd);
        }
        amxrt_delete();
    }

    /**
    * @brief  Initializes the default runtime configuration.
    * Sets the default values for all default configuration options.
    * Also uses the command line option parser and if needed adapts the configuration  accordingly.
    * @param argc the number of arguments available
    * @param argv the vector containing the commandline arguments
    * @param handler (optional) a pointer to a callback function to handle the option or NULL
    * @return 0 if no error occured.
    */
    int Initialize(int argc, char *argv[], amxrt_arg_fn_t handler)
    {
        return amxrt_config_init(argc, argv, &index, handler);
        // Add error handling if needed
    }

    /**
   *  @brief Load odls files mentioned on the command line or the default odl file.

   * By default odl files can be specified after the command line options.
   * This function will load these odl files.

   * If no odl files are specified at the command line, the default odl file will
   * be loaded. The application name (Argv[0]) will be used to find the default odl file.

   * The default odl file is "/etc/amx/<appname>/<appname>.odl".

   * If no odl files are defined at the commandline and the default odl file does't  exist, nothing is loaded an no error is thrown.

   * This function will also scan the default mib dir for the application. If  a mib dir is available and contains mib definitions, these are not loaded by default.
   * Mib definitions will be loaded when needed.

   * @param argc the number of arguments available
   * @param argv the vector containing the commandline arguments
   * @return  Non zero indicates an error
    */
    static int LoadOdlFiles(int argc, char *argv[])
    {
        return amxrt_load_odl_files(argc, argv, index);
        // Add error handling if needed
    }

    /**
    * @brief  Adds an entry point function.
    * 
    * The parser itself will not call entry points, but will add entry-points if
    * defined in the odl.
    * 
    * Any application or library can add extra entry points to the parser.
    * 
    * It is up to the application or library that initiates the odl parsing to
    * invoke the entry points.
    * Entry point functions must comply with the following signature:
    * 
    * typedef int (*amxo_entry_point_t) (int reason,
    *                                    amxd_dm_t *dm,
    *                                    amxo_parser_t *parser);
    * 
    * @param callback a valid function pointer (can not be NULL)
    * 
    * @return 0 when success, any other value indicates failure.
    */
    static int AddAutoSave(amxo_entry_point_t callback)
    {
        return amxo_parser_add_entry_point(amxrt_get_parser(), callback);
    }

    /**
    * @brief Connects to all bus sockets.
    * 
    * When auto-detect is enabled the ambiorix runtime will check if the well known
    * linux domain bus sockets exists and they will be added to the "uris" config.
    * 
    * Using command line option "-u" extra sockets can be added. It is also possible
    * to define the sockets that needs to be opened in the config section of an odl file.
    * 
    * The supported config options are:
    * - "uris" - used to connect on and register the data model of the application
    * - "data-uris" - used to connect on, but the data model is not registered on these sockets.
    * " "listen" - creates a listening socket, other applications can connect on these.
    * 
    * @return
    * Non zero indicates an error
    */
    static int Connect()
    {
        return amxrt_connect();
        // Add error handling if needed
    }

    /**
    * @brief
    * Scan backend directories for available backends.
    *
    * When auto-detection (configuration option "auto-detect") is enabled, all directories
    * in the configuration option "backend-dir" are scanned for valid shared object files.
    * Each found shared object file is added to the list of possible backends that can be used.
    *
    * When auto-detection is disabled this function will do nothing.
    */
    static void ConfigScanBackendDirs() { amxrt_config_scan_backend_dirs(); }

    /**
    * @brief Enables system signals that should be monitored by the eventloop.
    * 
    * The default implementation of the event loop will only monitor
    * - SIGINT
    * - SIGTERM
    * - SIGALRM
    * 
    * If other signals needs to be monitored, a list variant must be created, containing
    * the identifiers of the signals and passed to this function.
    * 
    * syssigs is a list variant containing the signal identifiers.
    */
    static void EnableSyssigs()
    {
        amxc_var_t *config  = amxrt_get_config();
        amxc_var_t *syssigs = GET_ARG(config, "system-signals");
        if (syssigs != NULL) {
            amxrt_enable_syssigs(syssigs);
        }
    }

    /**
    * @brief Register the data model or wait for required data model objects.
    * 
    * When the application has a data model available, it can be registered using this function.
    * As soon as the data model is registered, the application becomes a data model provider.
    * 
    * If required objects are defined (using odl files with "requires" or using command line option -R),
    * then this function will wait until these objects become available on the used bus systems before
    * registering the data model.
    * 
    * The wait can only be fulfilled when an eventloop is running, as it needs event handling.
    * 
    * When registering of the data model succeeded, the entry-points of the loaded modules/plugins
    * are called with reason AMXO_START (0).
    * 
    * @return Non zero indicates an error
    */
    static int RegisterOrWait()
    {
        return amxrt_register_or_wait();
        // Add error handling if needed
    }

    /** 
    * @brief Starts the event loop.
    * 
    * This function will start the event loop. The event loop will be "waiting" for
    * events and if one is received, it will be dispatched (correct callback functions
    * are called).
    * 
    * The event loop will keep running until @ref amxrt_el_stop is called, that is:
    * this function will not return until the event loop is stopped.
    * 
    * IF the event loop fails to start the function returns immediately.
    * 
    * @return Non 0 will indicate that starting the event loop failed.
    * 0 will indicate that the event loop was stopped.
    */
    int RunEventLoop() { return amxrt_el_start(); }

    /**
    * @brief Gets the runtime data model storage.
    * 
    * @return The amxd_dm_t pointer where the data model is stored.
    */
    static amxd_dm_t *getDatamodel() { return amxrt_get_dm(); }

    /**
    * @brief Gets runtime odl parser.
    * 
    * To manually read the odl files using the parser use any of the libamxo functions.
    * 
    * Example:
    * @code
    * amxo_parser_t* odl_parser = amxrt_get_parser();
    * amxd_dm_t* dm = amxrt_get_dm();
    * amxd_object_t* root = amxd_dm_get_root(dm);
    * 
    * amxo_parser_parse_file(parser, "/tmp/my_definition.odl", root);
    * @endcode
    * 
    * @return The runtime odl parser.
    */
    static amxo_parser_t *getParser() { return amxrt_get_parser(); }

    /**
    * @brief Gets the htable variant containing the configuration options
    * 
    * @return The htable variant containing the configuration options.
    */
    static amxc_var_t *getConfig() { return amxrt_get_config(); }

private:
    static void connection_added(const char *, const amxc_var_t *data, void *priv)
    {
        auto *connection = amxp_connection_get(amxc_var_constcast(fd_t, data));
        if (connection) {
            static_cast<Amxrt *>(priv)->add_connection(connection->fd);
        }
    }

    static void connection_deleted(const char *, const amxc_var_t *data, void *priv)
    {
        auto *runtime = static_cast<Amxrt *>(priv);
        const auto fd = amxc_var_constcast(fd_t, data);
        if (runtime->m_registered_fds.erase(fd)) {
            runtime->m_event_loop->remove_handlers(fd);
        }
    }

    void add_connection(int fd)
    {
        EventLoop::EventHandlers handlers = {
            .name = "amxrt_connection",
            .on_read = [this](int fd, EventLoop &) { return read_connection(fd, false); },
            .on_write = nullptr,
            .on_disconnect = [this](int fd, EventLoop &) { return read_connection(fd, true); },
            .on_error = [this](int fd, EventLoop &) { return read_connection(fd, true); },
        };
        LOG_IF(!m_event_loop->register_handlers(fd, handlers), FATAL)
            << "Failed to register Ambiorix connection " << fd;
        m_registered_fds.insert(fd);
    }

    bool read_connection(int fd, bool disconnected)
    {
        // EventLoopImpl removes HUP/ERR handlers before calling us.
        if (disconnected) {
            m_registered_fds.erase(fd);
        }
        if (auto *connection = amxp_connection_get(fd)) {
            // The runtime reader owns disconnect handling and configured retries.
            connection->reader(fd, connection->priv);
        }
        amxp_timers_calculate();
        amxp_timers_check();
        if (disconnected && amxp_connection_get(fd) && !m_registered_fds.count(fd)) {
            add_connection(fd);
        }
        return true;
    }

    std::shared_ptr<EventLoop> m_event_loop;
    std::set<int> m_registered_fds;
};
} // namespace nbapi
} // namespace beerocks
#endif
