/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_session_actions.h"
#include "mesh_pbc_template_actions.h"
#include "nbapi_utils.h"
#include "on_action.h"

#include "../src/beerocks/master/tasks/pbc_onboarding_task.h"

#include <bcl/network/network_utils.h>
#include <tlvf/tlvftypes.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

using son::PBCOnboardingTask;

namespace prplmesh {
namespace controller {
namespace actions {

namespace {

constexpr char MESH_PBC_TEMPLATE_TABLE[] = "X_PRPLWARE-COM_MeshPBCTemplate";

/**
 * At most one call is open: only a call that starts a session waits, and only one session is
 * active at a time.
 */
struct sOpenCall {
    uint64_t call_id = 0;
    std::string session_id;
    std::string name;
};

/**
 * A plain pointer: the Ambiorix bus backend can call the cancel callback during the static
 * destruction at the Controller stop.
 */
sOpenCall *g_open_call = nullptr;

struct sCall {
    std::string name;
    std::string arguments;
};

std::string object_path(const amxd_object_t *object)
{
    char *path = amxd_object_get_path(object, AMXD_OBJECT_INDEXED);
    std::string text(path ? path : "");
    free(path);
    return text;
}

sCall make_call(amxd_object_t *object, amxd_function_t *func)
{
    sCall call;
    call.name = std::string(amxd_function_get_name(func)) + "() on " + object_path(object);
    return call;
}

std::string get_string_argument(const amxc_var_t *args, const char *name)
{
    auto arg = GET_ARG(args, name);
    if (!arg || amxc_var_is_null(arg)) {
        return "";
    }
    char *value = amxc_var_dyncast(cstring_t, arg);
    std::string text(value ? value : "");
    free(value);
    return text;
}

std::string template_path(const amxd_object_t *mesh_pbc_template)
{
    return std::string(TEMPLATES_ROOT_DM) + "." + MESH_PBC_TEMPLATE_TABLE + "." +
           std::to_string(amxd_object_get_index(mesh_pbc_template));
}

/**
 * @return false if the ID of the Network.Device instance is not a MAC address.
 */
bool device_al_mac(amxd_object_t *device, sMacAddr &al_mac)
{
    const std::string id = device ? get_param_string(device, "ID") : "";
    if (!beerocks::net::network_utils::is_valid_mac(id)) {
        return false;
    }
    al_mac = tlvf::mac_from_string(id);
    return true;
}

/**
 * @return The path, or "" if no instance has that PBCTemplateID.
 */
std::string find_template_by_id(const std::string &template_id)
{
    amxd_object_t *table = amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s.%s",
                                         TEMPLATES_ROOT_DM, MESH_PBC_TEMPLATE_TABLE);
    if (!table) {
        return "";
    }
    amxd_object_for_each(instance, it, table)
    {
        amxd_object_t *inst = amxc_llist_it_get_data(it, amxd_object_t, it);
        if (inst && get_param_string(inst, "PBCTemplateID") == template_id) {
            return template_path(inst);
        }
    }
    return "";
}

/**
 * @return The PBC Session ID argument in lowercase.
 */
std::string session_id_argument(const amxc_var_t *args)
{
    std::string session_id = get_string_argument(args, "PBCSessionID");
    std::transform(session_id.begin(), session_id.end(), session_id.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return session_id;
}

/**
 * @brief Parse the BSSIDList argument: a comma-separated list of MAC addresses, or a list.
 *
 * Spaces around an entry are ignored. A duplicate entry (MAC comparison ignores case) is kept
 * once. An absent or empty argument gives an empty list.
 *
 * @return false with the cause in error if an entry is not a valid MAC address.
 */
bool parse_bssid_list(const amxc_var_t *args, std::vector<sMacAddr> &bssids, std::string &text,
                      std::string &error)
{
    bssids.clear();
    text.clear();
    auto arg = GET_ARG(args, "BSSIDList");
    if (!arg || amxc_var_is_null(arg)) {
        return true;
    }

    std::vector<std::string> entries;
    if (amxc_var_type_of(arg) == AMXC_VAR_ID_LIST) {
        amxc_var_for_each(entry, arg)
        {
            char *value = amxc_var_dyncast(cstring_t, entry);
            entries.push_back(value ? value : "");
            free(value);
        }
    } else {
        char *value = amxc_var_dyncast(cstring_t, arg);
        const std::string csv(value ? value : "");
        free(value);
        if (csv.find_first_not_of(' ') != std::string::npos) {
            std::istringstream iss(csv + ",");
            std::string entry;
            while (std::getline(iss, entry, ',')) {
                entries.push_back(entry);
            }
        }
    }

    for (auto entry : entries) {
        entry.erase(0, entry.find_first_not_of(' '));
        entry.erase(entry.find_last_not_of(' ') + 1);
        text += (text.empty() ? "" : ",") + entry;
        if (!beerocks::net::network_utils::is_valid_mac(entry)) {
            error = "BSSIDList entry \"" + entry + "\" is not a MAC address";
            return false;
        }
        const sMacAddr bssid = tlvf::mac_from_string(entry);
        if (std::find(bssids.begin(), bssids.end(), bssid) == bssids.end()) {
            bssids.push_back(bssid);
        }
    }
    return true;
}

/**
 * @brief Set the out arguments of an InitiatePBC() call.
 *
 * FailureReason is set unless Status is "Success". EnrolleeMACAddress and BSSID are set only
 * when Status is "Success". PBCSessionID is set only when the call created a session
 * (session_id is not empty).
 */
void set_out_arguments(amxc_var_t *out_args, PBCOnboardingTask::eResult status,
                       uint16_t failure_reason, const sMacAddr &enrollee_mac, const sMacAddr &bssid,
                       const std::string &session_id)
{
    for (const char *name :
         {"Status", "FailureReason", "EnrolleeMACAddress", "BSSID", "PBCSessionID"}) {
        auto arg = GET_ARG(out_args, name);
        amxc_var_delete(&arg);
    }
    amxc_var_add_key(cstring_t, out_args, "Status", PBCOnboardingTask::result_to_string(status));
    if (!session_id.empty()) {
        amxc_var_add_key(cstring_t, out_args, "PBCSessionID", session_id.c_str());
    }
    if (status != PBCOnboardingTask::eResult::SUCCESS) {
        amxc_var_add_key(uint16_t, out_args, "FailureReason", failure_reason);
        return;
    }
    amxc_var_add_key(cstring_t, out_args, "EnrolleeMACAddress",
                     tlvf::mac_to_string(enrollee_mac).c_str());
    amxc_var_add_key(cstring_t, out_args, "BSSID", tlvf::mac_to_string(bssid).c_str());
}

/**
 * @brief Return a rejected InitiatePBC() call at once: amxd status OK, Status, FailureReason 0.
 */
amxd_status_t return_rejected(amxc_var_t *args, PBCOnboardingTask::eResult status)
{
    set_out_arguments(args, status, 0, beerocks::net::network_utils::ZERO_MAC,
                      beerocks::net::network_utils::ZERO_MAC, "");
    return amxd_status_ok;
}

/**
 * @brief Reject an InitiatePBC() call before the session engine sees it.
 *
 * Writes Result and FailureReason (0) of the template Status, as the session engine does for
 * a rejected request: only if the template resolves and has no active session.
 */
amxd_status_t reject_call(const sCall &call, amxc_var_t *args, const std::string &template_ref,
                          PBCOnboardingTask::eResult status, const std::string &reason)
{
    const char *status_text = PBCOnboardingTask::result_to_string(status);
    LOG(WARNING) << "push-button call " << call.name << " rejected (" << status_text
                 << "): " << reason;
    LOG(INFO) << "push-button call " << call.name << call.arguments << ": rejected, Status "
              << status_text;

    amxd_object_t *mesh_pbc_template =
        template_ref.empty()
            ? nullptr
            : mesh_pbc_resolve_templates_reference(template_ref, MESH_PBC_TEMPLATE_TABLE);
    if (mesh_pbc_template) {
        amxd_object_t *status_object = amxd_object_get_child(mesh_pbc_template, "Status");
        if (status_object && get_param_string(status_object, "State") != "InProgress") {
            mesh_pbc_set_template_status_rejected(template_path(mesh_pbc_template), status_text, 0);
        }
    }
    return return_rejected(args, status);
}

void complete_open_call(const PBCOnboardingTask::sCompletion &completion)
{
    if (!g_open_call || g_open_call->session_id != completion.session_id) {
        return;
    }
    const sOpenCall call = *g_open_call;
    delete g_open_call;
    g_open_call = nullptr;

    amxc_var_t out_args;
    amxc_var_init(&out_args);
    amxc_var_set_type(&out_args, AMXC_VAR_ID_HTABLE);
    set_out_arguments(&out_args, completion.status, completion.failure_reason,
                      completion.enrollee_mac, completion.bssid, completion.session_id);

    const bool success = (completion.status == PBCOnboardingTask::eResult::SUCCESS);
    LOG(INFO) << "push-button call " << call.name << ": reply for session " << call.session_id
              << ": Status " << PBCOnboardingTask::result_to_string(completion.status)
              << ", FailureReason "
              << (success ? std::string("-") : std::to_string(completion.failure_reason))
              << ", EnrolleeMACAddress "
              << (success ? tlvf::mac_to_string(completion.enrollee_mac) : "-") << ", BSSID "
              << (success ? tlvf::mac_to_string(completion.bssid) : "-");

    amxd_status_t ret =
        amxd_function_deferred_done(call.call_id, amxd_status_ok, &out_args, nullptr);
    if (ret != amxd_status_ok) {
        LOG(WARNING) << "push-button call " << call.name
                     << ": the reply was not sent: " << amxd_status_string(ret);
    }
    amxc_var_clean(&out_args);
}

/**
 * @brief Cancel callback of the deferred InitiatePBC() call.
 *
 * A caller that stops waiting (for example on its own timeout) does not cancel the call: it
 * stays open until the session ends, and the bus drops the reply. The bus backend calls this
 * callback only when it removes the deferred call itself, when it disconnects (Controller stop)
 * or when the data model is cleaned. The session continues, and its result goes to the template
 * Status object only.
 */
void cancel_open_call(uint64_t call_id, void *const)
{
    if (!g_open_call || g_open_call->call_id != call_id) {
        return;
    }
    LOG(INFO) << "push-button call " << g_open_call->name << ": the call of session "
              << g_open_call->session_id
              << " was removed by the bus before the session end; the session continues";
    delete g_open_call;
    g_open_call = nullptr;
}

/**
 * @brief Start a session for an InitiatePBC() call.
 *
 * A rejected request returns at once. An accepted request defers the reply until the session
 * ends.
 */
amxd_status_t start_session(const sCall &call, PBCOnboardingTask::sStartRequest &request,
                            amxd_function_t *func, amxc_var_t *args, amxc_var_t *ret)
{
    auto task = get_pbc_onboarding_task();
    if (!task) {
        return reject_call(call, args, "", PBCOnboardingTask::eResult::ERROR_NOT_READY,
                           "the push-button session engine does not run");
    }

    request.origin        = PBCOnboardingTask::eOrigin::CONTROLLER;
    request.on_completion = complete_open_call;
    auto result           = task->start(request);
    if (!result.accepted) {
        LOG(INFO) << "push-button call " << call.name << call.arguments << ": rejected, Status "
                  << PBCOnboardingTask::result_to_string(result.error);
        return return_rejected(args, result.error);
    }

    if (g_open_call) {
        LOG(ERROR) << "push-button call " << g_open_call->name << " of session "
                   << g_open_call->session_id << " is still open, removed";
        const uint64_t old_call_id = g_open_call->call_id;
        delete g_open_call;
        g_open_call = nullptr;
        amxd_function_deferred_remove(old_call_id);
    }

    uint64_t call_id = 0;
    if (amxd_function_defer(func, &call_id, ret, cancel_open_call, nullptr) != amxd_status_ok) {
        LOG(ERROR) << "push-button call " << call.name << ": session " << result.session_id
                   << " started, but the reply cannot wait for its end; terminating it";
        task->terminate(request.template_reference);
        return amxd_status_unknown_error;
    }
    g_open_call             = new sOpenCall;
    g_open_call->call_id    = call_id;
    g_open_call->session_id = result.session_id;
    g_open_call->name       = call.name;

    LOG(INFO) << "push-button call " << call.name << call.arguments << ": accepted, session "
              << result.session_id << ", the reply comes at the session end";
    return amxd_status_deferred;
}

amxd_status_t terminate_result(const sCall &call, bool terminated)
{
    LOG(INFO) << "push-button call " << call.name << call.arguments << ": "
              << (terminated ? "done" : "failed");
    return terminated ? amxd_status_ok : amxd_status_invalid_value;
}

amxd_status_t mesh_pbc_template_initiate_pbc(amxd_object_t *object, amxd_function_t *func,
                                             amxc_var_t *args, amxc_var_t *ret)
{
    sCall call                  = make_call(object, func);
    const std::string reference = template_path(object);
    std::vector<sMacAddr> bssids;
    std::string bssid_list;
    std::string error;
    const bool valid_list = parse_bssid_list(args, bssids, bssid_list, error);
    call.arguments        = " (BSSIDList \"" + bssid_list + "\")";
    if (!valid_list) {
        return reject_call(call, args, reference, PBCOnboardingTask::eResult::ERROR_INVALID_INPUT,
                           error);
    }

    PBCOnboardingTask::sStartRequest request;
    request.template_reference = reference;
    request.bssids             = std::move(bssids);
    return start_session(call, request, func, args, ret);
}

/**
 * @brief Template-level TerminatePBC().
 *
 * Ends the active session if it uses this template, from any origin.
 *
 * Example of usage:
 * ubus call X_PRPLWARE-COM_Templates.X_PRPLWARE-COM_MeshPBCTemplate.1 TerminatePBC
 */
amxd_status_t mesh_pbc_template_terminate_pbc(amxd_object_t *object, amxd_function_t *func,
                                              amxc_var_t *args, amxc_var_t *ret)
{
    const sCall call = make_call(object, func);
    auto task        = get_pbc_onboarding_task();
    if (!task) {
        LOG(WARNING) << "push-button call " << call.name
                     << " failed: the push-button session engine does not run";
        return terminate_result(call, false);
    }
    return terminate_result(call, task->terminate(template_path(object)));
}

/**
 * @brief Agent-level X_PRPLWARE-COM_InitiatePBC(PBCTemplateID, BSSIDList).
 *
 * Arms only the BSSs of this Agent. Without PBCTemplateID, the template is the one that
 * Templates.Network.X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference names.
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.1 X_PRPLWARE-COM_InitiatePBC
 * '{"PBCTemplateID": "t1"}'
 */
amxd_status_t mesh_pbc_device_initiate_pbc(amxd_object_t *object, amxd_function_t *func,
                                           amxc_var_t *args, amxc_var_t *ret)
{
    sCall call                    = make_call(object, func);
    const std::string template_id = get_string_argument(args, "PBCTemplateID");
    std::vector<sMacAddr> bssids;
    std::string bssid_list;
    std::string error;
    const bool valid_list = parse_bssid_list(args, bssids, bssid_list, error);
    call.arguments = " (PBCTemplateID \"" + template_id + "\", BSSIDList \"" + bssid_list + "\")";

    sMacAddr al_mac{};
    if (!device_al_mac(object, al_mac)) {
        return reject_call(call, args, "", PBCOnboardingTask::eResult::ERROR_INVALID_INPUT,
                           "the ID of the Device is not a MAC address");
    }

    std::string reference;
    if (!template_id.empty()) {
        reference = find_template_by_id(template_id);
        if (reference.empty()) {
            return reject_call(call, args, "", PBCOnboardingTask::eResult::ERROR_INVALID_INPUT,
                               "PBCTemplateID \"" + template_id + "\" names no " +
                                   MESH_PBC_TEMPLATE_TABLE + " instance");
        }
    } else {
        reference = mesh_pbc_get_primary_template_reference();
        if (reference.empty()) {
            return reject_call(call, args, "", PBCOnboardingTask::eResult::ERROR_INVALID_INPUT,
                               "no PBCTemplateID, and "
                               "X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference is empty");
        }
    }
    if (!valid_list) {
        return reject_call(call, args, reference, PBCOnboardingTask::eResult::ERROR_INVALID_INPUT,
                           error);
    }

    PBCOnboardingTask::sStartRequest request;
    request.template_reference = std::move(reference);
    request.agent_al_mac       = al_mac;
    request.bssids             = std::move(bssids);
    return start_session(call, request, func, args, ret);
}

/**
 * @brief Stop the armed BSSs of the active session on one Agent (Agent-level terminate).
 */
amxd_status_t terminate_on_agent(const sCall &call, amxd_object_t *device,
                                 const std::string &session_id)
{
    sMacAddr al_mac{};
    if (!device_al_mac(device, al_mac)) {
        LOG(WARNING) << "push-button call " << call.name
                     << " failed: the ID of the Device is not a MAC address";
        return terminate_result(call, false);
    }
    auto task = get_pbc_onboarding_task();
    if (!task) {
        LOG(WARNING) << "push-button call " << call.name
                     << " failed: the push-button session engine does not run";
        return terminate_result(call, false);
    }
    return terminate_result(call, task->terminate(session_id, al_mac));
}

/**
 * @brief Agent-level X_PRPLWARE-COM_TerminatePBC(PBCSessionID).
 *
 * Stops the armed BSSs of this Agent in the active session. The session continues on the other
 * Agents.
 *
 * Example of usage:
 * ubus call Device.WiFi.DataElements.Network.Device.1 X_PRPLWARE-COM_TerminatePBC
 * '{"PBCSessionID": "aa:bb:cc:dd:ee:ff-0001"}'
 */
amxd_status_t mesh_pbc_device_terminate_pbc(amxd_object_t *object, amxd_function_t *func,
                                            amxc_var_t *args, amxc_var_t *ret)
{
    sCall call                   = make_call(object, func);
    const std::string session_id = session_id_argument(args);
    call.arguments               = " (PBCSessionID \"" + session_id + "\")";
    return terminate_on_agent(call, object, session_id);
}

} // namespace

void mesh_pbc_session_actions_cleanup(void)
{
    // Clear the state first: amxd_function_deferred_remove() calls cancel_open_call().
    if (!g_open_call) {
        return;
    }
    const uint64_t call_id = g_open_call->call_id;
    delete g_open_call;
    g_open_call = nullptr;
    amxd_function_deferred_remove(call_id);
}

void mesh_pbc_templates_network_disabled(void)
{
    auto task = get_pbc_onboarding_task();
    if (!task) {
        return;
    }
    task->handle_templates_network_disabled();
}

std::vector<beerocks::nbapi::sFunctions> get_mesh_pbc_session_func_list(void)
{
    return {
        {"mesh_pbc_template_initiate_pbc",
         TEMPLATES_ROOT_DM ".X_PRPLWARE-COM_MeshPBCTemplate.InitiatePBC",
         mesh_pbc_template_initiate_pbc},
        {"mesh_pbc_template_terminate_pbc",
         TEMPLATES_ROOT_DM ".X_PRPLWARE-COM_MeshPBCTemplate.TerminatePBC",
         mesh_pbc_template_terminate_pbc},
        {"mesh_pbc_device_initiate_pbc",
         DATAELEMENTS_ROOT_DM ".Network.Device.X_PRPLWARE-COM_InitiatePBC",
         mesh_pbc_device_initiate_pbc},
        {"mesh_pbc_device_terminate_pbc",
         DATAELEMENTS_ROOT_DM ".Network.Device.X_PRPLWARE-COM_TerminatePBC",
         mesh_pbc_device_terminate_pbc},
    };
}

} // namespace actions
} // namespace controller
} // namespace prplmesh
