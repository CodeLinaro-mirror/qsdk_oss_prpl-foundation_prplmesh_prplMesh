/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "prplmesh_cli.h"
#include "prplmesh_amx_client.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace beerocks {
namespace prplmesh_api {

prplmesh_cli::prplmesh_cli()
{
    m_amx_client = std::make_shared<beerocks::prplmesh_amx::AmxClient>();
    LOG_IF(!m_amx_client, FATAL) << "Unable to create ambiorix client instance!";

    LOG_IF(!m_amx_client->amx_initialize(AMBIORIX_BACKEND_PATH, AMBIORIX_BUS_URI), FATAL)
        << "Unable to connect to the ambiorix backend!";
}

operating_mode prplmesh_cli::get_operating_mode(bool &agt_timed_out, bool &ctl_timed_out)
{
    std::string agent_path = AGENT_ROOT_DM ".Info.";
    beerocks::prplmesh_amx::AmxResult agent_result;
    bool agent_dm_found = m_amx_client->get_object(agent_path, agent_result, agt_timed_out);

    std::string network_path = DATAELEMENTS_ROOT_DM ".Network.";
    beerocks::prplmesh_amx::AmxResult network_result;
    bool controller_dm_found =
        m_amx_client->get_object(network_path, network_result, ctl_timed_out);

    return static_cast<operating_mode>(agent_dm_found | (controller_dm_found << 1));
}

static bool print_mode_(operating_mode mode, bool agt_timed_out, bool ctl_timed_out)
{
    std::cout << "Mode: " << to_string(mode) << std::endl;
    if (agt_timed_out) {
        std::cout << "Requesting agent status timed out!!!" << std::endl;
    }
    if (ctl_timed_out > 0) {
        std::cout << "Requesting controller status timed out!!!" << std::endl;
    }

    return !agt_timed_out && !ctl_timed_out;
}

bool prplmesh_cli::print_mode()
{
    bool agt_timed_out, ctl_timed_out;
    auto mode = get_operating_mode(agt_timed_out, ctl_timed_out);

    return print_mode_(mode, agt_timed_out, ctl_timed_out);
}

bool prplmesh_cli::print_status(const std::string &format)
{
    using std::cout;
    using std::endl;
    using std::string;

    if (format != "pretty" && format != "json") {
        return false;
    }

    struct {
        operating_mode mode;
        bool agt_timed_out, ctl_timed_out;

        // Controller
        string bridge_mac;
        int num_devices;

        // Agent
        string agent_mac;
        string agent_mmode;
        string agent_currentstate;
        string agent_beststate;

        // ordered set so that ifaces are in lexicographic order
        std::set<string> agent_ifaces;

        // Fronthauls
        struct fh_state {
            string currentstate;
            string beststate;
        };
        // Iface name -> FH state
        std::map<string, fh_state> fhs;
    } state{};

    state.mode = get_operating_mode(state.agt_timed_out, state.ctl_timed_out);
    bool ret   = state.mode && !state.agt_timed_out && !state.ctl_timed_out;

    if (state.mode & PPM_OPMODE_CONTROLLER_ONLY) {
        string network_path     = DATAELEMENTS_ROOT_DM ".Network.";
        amxc_var_t *network_obj = nullptr;
        beerocks::prplmesh_amx::AmxResult network_result;
        if (m_amx_client->get_object(network_path, network_result)) {
            network_obj = network_result.object();
        }
        state.bridge_mac  = GET_CHAR(network_obj, "ControllerID");
        state.num_devices = GET_UINT32(network_obj, "DeviceNumberOfEntries");

        // For easier and more uniform usage in scripts and tests
        transform(state.bridge_mac.begin(), state.bridge_mac.end(), state.bridge_mac.begin(),
                  ::toupper);
    }

    if (state.mode & PPM_OPMODE_AGENT_ONLY) {
        string agent_path     = AGENT_ROOT_DM ".Info.";
        amxc_var_t *agent_obj = nullptr;
        beerocks::prplmesh_amx::AmxResult agent_result;
        if (m_amx_client->get_object(agent_path, agent_result)) {
            agent_obj = agent_result.object();
        }

        state.agent_mac           = GET_CHAR(agent_obj, "MACAddress");
        state.agent_mmode         = GET_CHAR(agent_obj, "ManagementMode");
        string agent_fh_ifaces    = GET_CHAR(agent_obj, "FronthaulIfaces");
        string agent_currentstate = GET_CHAR(agent_obj, "CurrentState");
        string agent_beststate    = GET_CHAR(agent_obj, "BestState");

        // For easier and more uniform usage in scripts and tests
        transform(state.agent_mac.begin(), state.agent_mac.end(), state.agent_mac.begin(),
                  ::toupper);

        // Trim state number
        state.agent_currentstate = agent_currentstate.substr(0, agent_currentstate.find(' '));
        state.agent_beststate    = agent_beststate.substr(0, agent_beststate.find(' '));

        std::stringstream ss(std::move(agent_fh_ifaces));
        for (string s; getline(ss, s, ',');) {
            state.agent_ifaces.insert(std::move(s));
        }

        auto fronthaul_iface_root             = agent_path + "Fronthaul.*.";
        const amxc_htable_t *fronthaul_ifaces = nullptr;
        beerocks::prplmesh_amx::AmxResult fronthaul_result;
        if (m_amx_client->get_htable_object(fronthaul_iface_root, fronthaul_result)) {
            fronthaul_ifaces = fronthaul_result.htable();
        }

        if (fronthaul_ifaces) {
            amxc_htable_iterate(iface_it, fronthaul_ifaces)
            {
                auto iface_obj         = amxc_var_from_htable_it(iface_it);
                auto iface_name        = GET_CHAR(iface_obj, "Iface");
                string fh_currentstate = GET_CHAR(iface_obj, "CurrentState");
                string fh_beststate    = GET_CHAR(iface_obj, "BestState");

                auto &fh_state = state.fhs[iface_name];

                // Trim state number
                fh_state.currentstate = fh_currentstate.substr(0, fh_currentstate.find(' '));
                fh_state.beststate    = fh_beststate.substr(0, fh_beststate.find(' '));
            }
        }

        ret = ret && (state.agent_ifaces.size() == state.fhs.size());
    }

    if (format == "pretty") {
        print_mode_(state.mode, state.agt_timed_out, state.ctl_timed_out);

        if (state.mode & PPM_OPMODE_CONTROLLER_ONLY) {
            cout << "Controller:\n\tbridge MAC: " << state.bridge_mac << endl;
            cout << '\t' << state.num_devices << " agent(s) connected" << endl;
        }

        if (state.mode & PPM_OPMODE_AGENT_ONLY) {
            string agent_fh_ifaces;
            for (const auto &iface : state.agent_ifaces) {
                agent_fh_ifaces += iface;
                agent_fh_ifaces += ',';
            }
            if (agent_fh_ifaces.size()) {
                agent_fh_ifaces.pop_back();
            }

            cout << "Agent:" << endl
                 << "\tMAC address: " << state.agent_mac << endl
                 << "\tmanagement mode: " << state.agent_mmode << endl
                 << "\tfronthaul ifaces: " << agent_fh_ifaces << endl
                 << "\tcurrent state: " << state.agent_currentstate << endl
                 << "\tbest state: " << state.agent_beststate << endl;

            for (const auto &fh : state.fhs) {
                cout << "\tFronthaul:" << endl
                     << "\t\tinterface: " << fh.first << endl
                     << "\t\tcurrent state: " << fh.second.currentstate << endl
                     << "\t\tbest state: " << fh.second.beststate << endl;

                state.agent_ifaces.erase(fh.first);
            }
            string missing_ifaces;
            for (const auto &iface : state.agent_ifaces) {
                missing_ifaces += iface;
                missing_ifaces += ',';
            }
            if (missing_ifaces.length()) {
                missing_ifaces.pop_back();
                cout << "\tMissing info about: " << missing_ifaces << endl;
            }
        }
    }

    if (format == "json") {
        using std::quoted;

        std::stringstream output;

        output << "{\"Mode\": " << quoted(to_string(state.mode)) << ',' << std::boolalpha
               << "\"Agent timeout\": " << state.agt_timed_out << ','
               << "\"Controller timeout\": " << state.ctl_timed_out;

        if (state.mode & PPM_OPMODE_CONTROLLER_ONLY) {
            output << ",\"Controller\": {"
                   << "\"ControllerID\": " << quoted(state.bridge_mac) << ','
                   << "\"Agents connected\": " << state.num_devices << '}';
        }

        if (state.mode & PPM_OPMODE_AGENT_ONLY) {
            string agent_fh_ifaces;
            for (const auto &iface : state.agent_ifaces) {
                agent_fh_ifaces += iface;
                agent_fh_ifaces += ',';
            }
            if (agent_fh_ifaces.size()) {
                agent_fh_ifaces.pop_back();
            }

            output << ",\"Agent\": {"
                   << "\"MACAddress\": \"" << state.agent_mac << "\","
                   << "\"ManagementMode\": " << quoted(state.agent_mmode) << ','
                   << "\"FronthaulIfaces\": " << quoted(agent_fh_ifaces) << ','
                   << "\"CurrentState\": " << quoted(state.agent_currentstate) << ','
                   << "\"BestState\": " << quoted(state.agent_beststate) << ',';

            output << "\"Fronthauls\": {";
            for (const auto &fh : state.fhs) {
                output << quoted(fh.first) << ": {"
                       << "\"CurrentState\": " << quoted(fh.second.currentstate) << ','
                       << "\"BestState\": " << quoted(fh.second.beststate) << '}';

                output << ',';
            }
            if (state.fhs.size()) {
                output.seekp(-1, std::ios_base::end);
            }
            output << '}'; // Fronthauls

            output << '}'; // Agent
        }

        output << '}';

        cout << std::move(output).str() << endl;
    }

    return ret;
}

void prplmesh_cli::print_help()
{
    std::cerr << R"help!(
Usage: prplmesh_cli OPTION or
Usage: prplmesh_cli -c <command> [command_arguments]
The following options are available:
-v	: prints the current prplMesh version
-h	: prints this help text

The following commands are available :
help            : get supported commands
version         : get current prplMesh version
mode            : get current prplMesh mode (Agent only/Controller only/Agent+Controller)
status          : print an overview of the current prplMesh status
  -o <output format>                Either "pretty" (default if stdout is a TTY) or "json" (otherwise)
show_ap         : show AccessPoints
set_ssid        : set SSID
  -o .<ap_object_number>|<ap_ssid>  Use .. if <ap_ssid> starts with .
  -n <new_ssid_name>
set_security        : set security
  -o .<ap_object_number>|<ap_ssid>  Same as for set_ssid
  -m None|WPA2-Personal
  -p <passphrase>                   For the WPA2-Personal mode
conn_map        : dump the latest network map
)help!";
}

void prplmesh_cli::print_version()
{
    std::cerr << "prplMesh version: " << BEEROCKS_VERSION << std::endl;
}

std::string prplmesh_cli::get_ap_path(std::string ap)
{
    std::stringstream path;
    path << DATAELEMENTS_ROOT_DM << ".Network.AccessPoint.";

    if (ap[0] == '.' and ap[1] != '.') {
        path << ap.substr(1) << '.';
        return path.str();
    }

    if (ap[0] == '.' and ap[1] == '.') {
        ap = ap.substr(1);
    }

    std::string ap_ht_path     = path.str() + "*.";
    const amxc_htable_t *ht_ap = nullptr;
    beerocks::prplmesh_amx::AmxResult ap_list_result;
    if (m_amx_client->get_htable_object(ap_ht_path, ap_list_result)) {
        ht_ap = ap_list_result.htable();
    }
    if (!ht_ap) {
        return "";
    }
    amxc_htable_iterate(ap_it, ht_ap)
    {
        std::string ap_path_i = amxc_htable_it_get_key(ap_it);
        amxc_var_t *ap_obj    = nullptr;
        beerocks::prplmesh_amx::AmxResult ap_result;
        if (m_amx_client->get_object(ap_path_i, ap_result)) {
            ap_obj = ap_result.object();
        }
        std::string ap_ssid = GET_CHAR(ap_obj, "SSID");

        if (strcasecmp(ap.c_str(), ap_ssid.c_str()) == 0) {
            return ap_path_i;
        }
    }

    return "";
}

void prplmesh_cli::show_ap()
{
    std::cout << "Show AccessPoints:" << std::endl;
    std::string ap_ht_path     = DATAELEMENTS_ROOT_DM ".Network.AccessPoint.*.";
    const amxc_htable_t *ht_ap = nullptr;
    beerocks::prplmesh_amx::AmxResult ap_list_result;
    if (m_amx_client->get_htable_object(ap_ht_path, ap_list_result)) {
        ht_ap = ap_list_result.htable();
    }
    if (!ht_ap) {
        // No access points defined?
        // Or error retrieving object?
        std::cerr << "Unable to access object at path " << ap_ht_path << std::endl;
        return;
    }
    auto flags = std::cout.flags();
    boolalpha(std::cout);
    int ap_index = 0;
    amxc_htable_iterate(ap_it, ht_ap)
    {
        ap_index++;
        std::string ap_path_i = amxc_htable_it_get_key(ap_it);
        amxc_var_t *ap_obj    = nullptr;
        beerocks::prplmesh_amx::AmxResult ap_result;
        if (m_amx_client->get_object(ap_path_i, ap_result)) {
            ap_obj = ap_result.object();
        }
        // AP[1]: ssid: PrplCli, MultiApMode: Fronthaul
        //     Band 2.4G: true, Band 5G-L: true, Band 5G-H: true, Band 6G: false
        std::cout << "AP[" << ap_index << "]:";
        std::string ap_ssid = GET_CHAR(ap_obj, "SSID");
        std::cout << " ssid: " << ap_ssid;
        std::string ap_multi_ap_mode = GET_CHAR(ap_obj, "MultiApMode");
        std::cout << ", MultiAPMode: " << ap_multi_ap_mode << std::endl;
        std::cout << "    Band 2.4G: " << GET_BOOL(ap_obj, "Band2_4G");
        std::cout << ", Band 5G-L: " << GET_BOOL(ap_obj, "Band5GL");
        std::cout << ", Band 5G-H: " << GET_BOOL(ap_obj, "Band5GH");
        std::cout << ", Band 6G: " << GET_BOOL(ap_obj, "Band6G") << std::endl;
    }
    std::cout.flags(flags);
    if (ap_index == 0) {
        std::cout << "(None defined)" << std::endl;
    }
}

bool prplmesh_cli::set_ssid(const std::string &ap, const std::string &ssid)
{
    std::string ap_path = get_ap_path(ap);
    if (ap_path.empty()) {
        std::cerr << "No AP found with id " << ap << std::endl;
        return false;
    }

    amxc_var_t *ap_obj = nullptr;
    beerocks::prplmesh_amx::AmxResult ap_result;
    if (m_amx_client->get_object(ap_path, ap_result)) {
        ap_obj = ap_result.object();
    }
    if (!ap_obj) {
        std::cerr << "Unable to access object at path " << ap_path << std::endl;
        return false;
    }

    amxc_var_set(cstring_t, GET_ARG(ap_obj, "SSID"), ssid.c_str());
    auto status = m_amx_client->set_object(ap_path, ap_obj);

    if (status != AMXB_STATUS_OK) {
        std::cerr << "Setting new SSID failed with: " << amxb_get_error(status) << std::endl;
    } else {
        std::cerr << "Successfully set " << ap_path << " SSID to " << ssid << std::endl;
    }

    return status == AMXB_STATUS_OK;
}

bool prplmesh_cli::set_security(const std::string &ap, const std::string &mode,
                                const std::string &passphrase)
{
    std::string ap_path = get_ap_path(ap);
    if (ap_path.empty()) {
        std::cerr << "No AP found with id " << ap << std::endl;
        return false;
    }

    ap_path += "Security.";
    amxc_var_t *ap_obj = nullptr;
    beerocks::prplmesh_amx::AmxResult ap_result;
    if (m_amx_client->get_object(ap_path, ap_result)) {
        ap_obj = ap_result.object();
    }
    if (!ap_obj) {
        std::cerr << "Unable to access object at path " << ap_path << std::endl;
        return false;
    }

    amxc_var_set(cstring_t, GET_ARG(ap_obj, "ModeEnabled"), mode.c_str());
    auto status = m_amx_client->set_object(ap_path, ap_obj, ap_obj);
    if (status != AMXB_STATUS_OK) {
        std::cerr << "Changing security params failed with: " << amxb_get_error(status) << '\n';
        return false;
    }

    if (mode == "WPA2-Personal") {
        ap_obj = amxc_var_get_first(amxc_var_get_first(ap_obj));
        if (GET_ARG(ap_obj, "KeyPassphrase")) {
            amxc_var_set(cstring_t, GET_ARG(ap_obj, "KeyPassphrase"), passphrase.c_str());
        } else {
            amxc_var_add_key(cstring_t, ap_obj, "KeyPassphrase", passphrase.c_str());
        }
        status = m_amx_client->set_object(ap_path, ap_obj, ap_obj);
    }

    if (status != AMXB_STATUS_OK) {
        std::cerr << "Changing security params failed with: " << amxb_get_error(status) << '\n';
    } else {
        std::cerr << "Successfully set " << ap_path << " params" << std::endl;
    }

    return status == AMXB_STATUS_OK;
}

} // namespace prplmesh_api
} // namespace beerocks
