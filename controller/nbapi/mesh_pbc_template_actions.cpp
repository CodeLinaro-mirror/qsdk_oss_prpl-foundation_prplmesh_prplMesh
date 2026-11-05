/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_template_actions.h"
#include "nbapi_utils.h"
#include "templates_actions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <utility>

namespace prplmesh {
namespace controller {
namespace actions {

namespace {
constexpr char TR181_TEMPLATES_PREFIX[]      = "Device.WiFi.Templates.";
constexpr char MESH_PBC_TEMPLATE_TABLE[]     = "X_PRPLWARE-COM_MeshPBCTemplate";
constexpr char MESH_PBC_UUID_E_PARAM[]       = "X_PRPLWARE-COM_MeshPBCUUID_E";
constexpr char MESH_PBC_TEMPLATE_REF_PARAM[] = "X_PRPLWARE-COM_MeshPBCTemplateReference";

/**
 * @brief Values of one session for a Status.History entry.
 */
struct sMeshPbcSessionValues {
    std::string session_id;
    std::string result;
    uint16_t failure_reason = 0;
    std::string enrollee_mac;
    std::string bssid;
    std::string time_stamp;
};

/**
 * The values that the latest session of each template wrote to Status at its end, by template
 * path. A rejected request writes Status.Result and Status.FailureReason after that end; the
 * History entry gets the values of the session from here. An entry is deleted when the next
 * session of the template is created, or when the template instance is removed.
 */
std::map<std::string, sMeshPbcSessionValues> g_ended_sessions;
} // namespace

static std::string mesh_pbc_uuid_e_path(void)
{
    return std::string(TEMPLATES_ROOT_DM) + ".Network." + MESH_PBC_UUID_E_PARAM;
}

/**
 * @return false if the list has an empty entry (for example "WPS,,DPP" or "WPS,") or an
 * entry with leading or trailing whitespace.
 */
static bool mesh_pbc_split_csv(const std::string &csv, std::vector<std::string> &out)
{
    out.clear();
    if (csv.empty()) {
        return true;
    }

    std::istringstream iss(csv + ",");
    std::string entry;
    while (std::getline(iss, entry, ',')) {
        if (entry.empty() || std::isspace(static_cast<unsigned char>(entry.front())) ||
            std::isspace(static_cast<unsigned char>(entry.back()))) {
            return false;
        }
        out.push_back(entry);
    }
    return true;
}

/**
 * @brief Runs the default type validation, then gets the new value from the arguments.
 */
static amxd_status_t mesh_pbc_validate_get_string(amxd_object_t *object, amxd_param_t *param,
                                                  amxd_action_t reason,
                                                  const amxc_var_t *const args,
                                                  amxc_var_t *const retval, void *priv,
                                                  std::string &value)
{
    amxd_status_t status = amxd_action_param_validate(object, param, reason, args, retval, priv);
    if (status != amxd_status_ok) {
        return status;
    }

    char *new_value = amxc_var_dyncast(cstring_t, args);
    value.assign(new_value ? new_value : "");
    free(new_value);
    return amxd_status_ok;
}

amxd_object_t *mesh_pbc_resolve_templates_reference(const std::string &reference,
                                                    const std::string &table_name)
{
    const std::string native_prefix = std::string(TEMPLATES_ROOT_DM) + ".";
    const std::string tr181_prefix  = TR181_TEMPLATES_PREFIX;
    std::string relative_path;
    if (reference.compare(0, native_prefix.size(), native_prefix) == 0) {
        relative_path = reference.substr(native_prefix.size());
    } else if (reference.compare(0, tr181_prefix.size(), tr181_prefix) == 0) {
        relative_path = reference.substr(tr181_prefix.size());
    } else {
        return nullptr;
    }

    const std::string table_prefix = table_name + ".";
    if (relative_path.compare(0, table_prefix.size(), table_prefix) != 0) {
        return nullptr;
    }
    const std::string index = relative_path.substr(table_prefix.size());
    if (index.empty() || index[0] == '0' || index.size() > 10 ||
        !std::all_of(index.begin(), index.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return nullptr;
    }
    const std::string path = native_prefix + relative_path;

    auto dm                 = beerocks::nbapi::Amxrt::getDatamodel();
    amxd_object_t *table    = amxd_dm_findf(dm, "%s.%s", TEMPLATES_ROOT_DM, table_name.c_str());
    amxd_object_t *instance = amxd_dm_findf(dm, "%s", path.c_str());
    if (!table || !instance || amxd_object_get_type(instance) != amxd_object_instance ||
        amxd_object_get_parent(instance) != table) {
        return nullptr;
    }
    return instance;
}

/**
 * @return true if the value is a UUID in the canonical 8-4-4-4-12 hexadecimal form; else
 * false, with the cause in error.
 */
static bool mesh_pbc_check_uuid_e(const std::string &value, std::string &error)
{
    constexpr size_t uuid_length = 36;
    bool valid                   = (value.size() == uuid_length);
    for (size_t i = 0; valid && i < uuid_length; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            valid = (value[i] == '-');
        } else {
            valid = (std::isxdigit(static_cast<unsigned char>(value[i])) != 0);
        }
    }
    if (!valid) {
        error = "\"" + value + "\" is not a canonical UUID";
    }
    return valid;
}

/**
 * @brief Validate Templates.Network.X_PRPLWARE-COM_MeshPBCUUID_E.
 *
 * Same rule as the pwhm validator of WiFi.AccessPoint.{i}.WPS.UUID: accept the current value,
 * otherwise only a UUID in the canonical 8-4-4-4-12 hexadecimal form. So a write of "" fails
 * once a value is set. The current value is accepted so that the default "" loads: the parser
 * validates each value at the end of its definition. The value is also checked while the
 * saved data model loads: a saved value that is not a UUID stops the load.
 */
static amxd_status_t validate_mesh_pbc_uuid_e(amxd_object_t *object, amxd_param_t *param,
                                              amxd_action_t reason, const amxc_var_t *const args,
                                              amxc_var_t *const retval, void *priv)
{
    std::string value;
    amxd_status_t status =
        mesh_pbc_validate_get_string(object, param, reason, args, retval, priv, value);
    if (status != amxd_status_ok) {
        return status;
    }

    char *current        = amxc_var_dyncast(cstring_t, &param->value);
    const bool unchanged = (value == (current ? current : ""));
    free(current);
    std::string error;
    if (unchanged || mesh_pbc_check_uuid_e(value, error)) {
        return amxd_status_ok;
    }
    LOG(WARNING) << MESH_PBC_UUID_E_PARAM << " rejected: " << error;
    return amxd_status_invalid_value;
}

static std::string mesh_pbc_instance_path(const char *table_name, amxd_object_t *instance)
{
    return std::string(TEMPLATES_ROOT_DM) + "." + table_name + "." +
           std::to_string(amxd_object_get_index(instance));
}

/**
 * @return The value 0 to 15, or -1 if the character is not a hexadecimal digit.
 */
static int mesh_pbc_hex_digit_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief The octets are in the order of the hexadecimal digits. The dashes are ignored.
 *
 * @return false if the UUID does not have exactly 32 hexadecimal digits, otherwise true.
 */
static bool mesh_pbc_uuid_to_octets(const std::string &uuid, std::array<uint8_t, 16> &octets)
{
    std::string digits;
    for (const char c : uuid) {
        if (c != '-') {
            digits.push_back(c);
        }
    }
    if (digits.size() != 2 * octets.size()) {
        return false;
    }
    for (size_t i = 0; i < octets.size(); i++) {
        const int high = mesh_pbc_hex_digit_value(digits[2 * i]);
        const int low  = mesh_pbc_hex_digit_value(digits[2 * i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        octets[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

/**
 * @return true if the value is a canonical UUID; false if it is "" or not valid, with the cause
 * in reason. The reason names the parameter.
 */
static bool mesh_pbc_get_mesh_uuid_e(amxd_object_t *network, std::string &uuid_e,
                                     std::string &reason)
{
    const std::string path = mesh_pbc_uuid_e_path();
    uuid_e                 = network ? get_param_string(network, MESH_PBC_UUID_E_PARAM) : "";
    std::string error;
    if (uuid_e.empty()) {
        reason = path + " is empty";
        return false;
    }
    if (!mesh_pbc_check_uuid_e(uuid_e, error)) {
        reason = path + " is not valid: " + error;
        return false;
    }
    return true;
}

eMeshPbcBssConfigResult mesh_pbc_get_bss_config(const std::string &template_ref,
                                                sMeshPbcBssConfig &config, std::string &reason)
{
    if (template_ref.empty()) {
        reason = std::string("the SSCTemplate of the BSS has no ") + MESH_PBC_TEMPLATE_REF_PARAM;
        return eMeshPbcBssConfigResult::SKIP;
    }

    auto dm                = beerocks::nbapi::Amxrt::getDatamodel();
    amxd_object_t *network = amxd_dm_findf(dm, "%s.Network", TEMPLATES_ROOT_DM);
    if (!network || !get_param_bool(network, "Enable")) {
        reason = "Templates.Network.Enable is false";
        return eMeshPbcBssConfigResult::SKIP;
    }

    amxd_object_t *mesh_pbc_template =
        mesh_pbc_resolve_templates_reference(template_ref, MESH_PBC_TEMPLATE_TABLE);
    if (!mesh_pbc_template) {
        reason = std::string(MESH_PBC_TEMPLATE_REF_PARAM) + " \"" + template_ref + "\" is not an " +
                 MESH_PBC_TEMPLATE_TABLE + " instance";
        return eMeshPbcBssConfigResult::INVALID;
    }
    const std::string path = mesh_pbc_instance_path(MESH_PBC_TEMPLATE_TABLE, mesh_pbc_template);
    if (!get_param_bool(mesh_pbc_template, "Enable")) {
        reason = path + " is disabled";
        return eMeshPbcBssConfigResult::SKIP;
    }

    // libamxd does not run the validator of an instance add with PBCTemplateID "", because the
    // value equals the default.
    if (get_param_string(mesh_pbc_template, "PBCTemplateID").empty()) {
        reason = path + ".PBCTemplateID is empty";
        return eMeshPbcBssConfigResult::INVALID;
    }
    const std::string method = get_param_string(mesh_pbc_template, "PushButtonMethod");
    std::vector<std::string> methods;
    if (!mesh_pbc_split_csv(method, methods) ||
        std::find(methods.begin(), methods.end(), "WPS") == methods.end()) {
        reason = path + ".PushButtonMethod \"" + method + "\" does not contain \"WPS\"";
        return eMeshPbcBssConfigResult::INVALID;
    }

    const uint32_t listen_duration = get_param_uint32(mesh_pbc_template, "ListenDuration");

    amxd_object_t *wps = amxd_object_get_child(mesh_pbc_template, "WPS");
    if (!wps) {
        reason = path + ".WPS is not found";
        return eMeshPbcBssConfigResult::INVALID;
    }
    std::string uuid_e;
    if (!mesh_pbc_get_mesh_uuid_e(network, uuid_e, reason)) {
        return eMeshPbcBssConfigResult::INVALID;
    }

    const uint32_t device_password_id = get_param_uint32(wps, "DevicePasswordID");

    constexpr uint16_t device_password_id_push_button = 0x0004;
    config                                            = {};
    config.template_path                              = path;
    config.listen_duration                            = static_cast<uint16_t>(listen_duration);
    config.uuid_e_text                                = uuid_e;
    config.device_password_id                         = static_cast<uint16_t>(device_password_id);
    if (!mesh_pbc_uuid_to_octets(uuid_e, config.uuid_e)) {
        reason =
            mesh_pbc_uuid_e_path() + " is not valid: \"" + uuid_e + "\" is not a canonical UUID";
        return eMeshPbcBssConfigResult::INVALID;
    }
    if (config.device_password_id != device_password_id_push_button) {
        constexpr size_t device_password_max_length = 32;
        const std::string device_password           = get_param_string(wps, "DevicePassword");
        if (device_password.size() > device_password_max_length) {
            reason = path + ".WPS.DevicePassword has " + std::to_string(device_password.size()) +
                     " octets, more than " + std::to_string(device_password_max_length);
            return eMeshPbcBssConfigResult::INVALID;
        }
        config.device_password         = device_password;
        config.device_password_missing = config.device_password.empty();
    }
    return eMeshPbcBssConfigResult::FOUND;
}

eMeshPbcSessionTemplateResult
mesh_pbc_get_session_template(const std::string &reference,
                              sMeshPbcSessionTemplate &session_template, std::string &reason)
{
    session_template = {};
    amxd_object_t *mesh_pbc_template =
        mesh_pbc_resolve_templates_reference(reference, MESH_PBC_TEMPLATE_TABLE);
    if (!mesh_pbc_template) {
        reason = "\"" + reference + "\" is not an " + MESH_PBC_TEMPLATE_TABLE + " instance";
        return eMeshPbcSessionTemplateResult::NOT_FOUND;
    }
    const std::string path = mesh_pbc_instance_path(MESH_PBC_TEMPLATE_TABLE, mesh_pbc_template);
    session_template.template_path = path;

    amxd_object_t *network =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s.Network", TEMPLATES_ROOT_DM);
    if (!network || !get_param_bool(network, "Enable")) {
        reason = "Templates.Network.Enable is false";
        return eMeshPbcSessionTemplateResult::NOT_READY;
    }

    if (!get_param_bool(mesh_pbc_template, "Enable")) {
        reason = path + ".Enable is false";
        return eMeshPbcSessionTemplateResult::INVALID;
    }
    // libamxd does not run the validator of an instance add with PBCTemplateID "", because the
    // value equals the default.
    if (get_param_string(mesh_pbc_template, "PBCTemplateID").empty()) {
        reason = path + ".PBCTemplateID is empty";
        return eMeshPbcSessionTemplateResult::INVALID;
    }

    const std::string method = get_param_string(mesh_pbc_template, "PushButtonMethod");
    std::vector<std::string> methods;
    const bool split = mesh_pbc_split_csv(method, methods);
    const bool wps   = split && std::find(methods.begin(), methods.end(), "WPS") != methods.end();
    const bool dpp   = split && std::find(methods.begin(), methods.end(), "DPP") != methods.end();
    if (!wps && !dpp) {
        reason = path + ".PushButtonMethod \"" + method + "\" contains neither \"WPS\" nor \"DPP\"";
        return eMeshPbcSessionTemplateResult::INVALID;
    }

    const uint32_t listen_duration = get_param_uint32(mesh_pbc_template, "ListenDuration");

    if (wps) {
        if (!amxd_object_get_child(mesh_pbc_template, "WPS")) {
            reason = path + ".WPS is not found";
            return eMeshPbcSessionTemplateResult::INVALID;
        }
        std::string uuid_e;
        if (!mesh_pbc_get_mesh_uuid_e(network, uuid_e, reason)) {
            return eMeshPbcSessionTemplateResult::INVALID;
        }
    }

    const std::string scope = get_param_string(mesh_pbc_template, "ButtonPressScope");

    session_template.wps             = wps;
    session_template.dpp             = dpp;
    session_template.listen_duration = static_cast<uint16_t>(listen_duration);
    if (scope == "MeshWide") {
        session_template.button_press_scope = eMeshPbcButtonPressScope::MESH_WIDE;
    } else if (scope == "RequestingDevice") {
        session_template.button_press_scope = eMeshPbcButtonPressScope::REQUESTING_DEVICE;
    }
    return eMeshPbcSessionTemplateResult::VALID;
}

std::string mesh_pbc_get_primary_template_reference(void)
{
    amxd_object_t *network =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s.Network", TEMPLATES_ROOT_DM);
    if (!network) {
        return "";
    }
    return get_param_string(network, "X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference");
}

/**
 * @return The current time in RFC 3339 format, or "" on a failure.
 */
static std::string mesh_pbc_current_time(void)
{
    amxc_ts_t now;
    char buf[64] = {};
    if (amxc_ts_now(&now) != 0 || amxc_ts_format(&now, buf, sizeof(buf)) == 0) {
        LOG(ERROR) << "Failed to get the current time";
        return "";
    }
    return buf;
}

/**
 * @param[in] fill Adds the values to the transaction; the object is selected.
 * @return false if the object is not found or the write failed.
 */
template <typename F> static bool mesh_pbc_write_object(const std::string &object_path, F fill)
{
    auto dm               = beerocks::nbapi::Amxrt::getDatamodel();
    amxd_object_t *object = amxd_dm_findf(dm, "%s", object_path.c_str());
    if (!object) {
        LOG(WARNING) << object_path << " is not found, not written";
        return false;
    }

    amxd_trans_t transaction;
    if (amxd_trans_init(&transaction) != amxd_status_ok) {
        LOG(ERROR) << "Failed to initialize the transaction for " << object_path;
        return false;
    }
    amxd_trans_set_attr(&transaction, amxd_tattr_change_ro, true);
    amxd_trans_select_object(&transaction, object);
    fill(transaction);
    amxd_status_t ret = amxd_trans_apply(&transaction, dm);
    amxd_trans_clean(&transaction);
    if (ret != amxd_status_ok) {
        LOG(ERROR) << "Failed to write " << object_path << ": " << amxd_status_string(ret);
        return false;
    }
    return true;
}

/**
 * @return The name of the Stats counter of a Status.Result value, or nullptr if the value has
 * no counter.
 */
static const char *mesh_pbc_result_counter(const std::string &result)
{
    static const std::array<std::pair<const char *, const char *>, 6> counters = {{
        {"Success", "Successes"},
        {"Error_Timeout", "Timeouts"},
        {"Error_Not_Ready", "NotReady"},
        {"Error_Invalid_Input", "InvalidInput"},
        {"Error_Other", "OtherFailures"},
        {"Canceled", "Canceled"},
    }};
    for (const auto &counter : counters) {
        if (result == counter.first) {
            return counter.second;
        }
    }
    if (!result.empty()) {
        LOG(WARNING) << "push-button result " << result << " has no Stats counter";
    }
    return nullptr;
}

/**
 * @brief Writes the template Status, and increases one counter of the template Stats in the
 * same transaction.
 *
 * A template deleted during a session has no Status object: the write is skipped with a
 * warning, and no counter changes.
 *
 * @param[in] counter Name of the Stats counter, or nullptr to change no counter.
 */
template <typename F>
static bool mesh_pbc_write_template_status(const std::string &template_path, const char *counter,
                                           F fill)
{
    amxd_object_t *stats =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s.Stats", template_path.c_str());
    return mesh_pbc_write_object(template_path + ".Status", [&](amxd_trans_t &transaction) {
        fill(transaction);
        if (!counter) {
            return;
        }
        if (!stats) {
            LOG(ERROR) << template_path << ".Stats is not found, " << counter << " not increased";
            return;
        }
        const uint64_t value = amxd_object_get_value(uint64_t, stats, counter, nullptr);
        amxd_trans_select_object(&transaction, stats);
        amxd_trans_set_value(uint64_t, &transaction, counter, value + 1);
    });
}

/**
 * @brief Adds to the transaction the deletion of the History entries that were added first,
 * until at most max_entries entries are left.
 *
 * @param[in] history The History table, selected in the transaction.
 * @param[in] added Number of entries that the transaction added before this call.
 */
static void mesh_pbc_trim_history(amxd_trans_t &transaction, amxd_object_t *history,
                                  uint32_t max_entries, uint32_t added = 0)
{
    uint64_t count = uint64_t(amxd_object_get_instance_count(history)) + added;
    amxd_object_for_each(instance, it, history)
    {
        if (count <= max_entries) {
            break;
        }
        amxd_trans_del_inst(
            &transaction, amxd_object_get_index(amxc_container_of(it, amxd_object_t, it)), nullptr);
        count--;
    }
}

/**
 * @brief Gets the session that the template Status holds.
 *
 * Result, FailureReason, EnrolleeMACAddress and BSSID come from the values that the session
 * wrote at its end, if they are known; otherwise from Status.
 *
 * @return false if Status holds no session (PBCSessionID is "").
 */
static bool mesh_pbc_get_status_session(amxd_object_t *status, const sMeshPbcSessionValues *ended,
                                        sMeshPbcSessionValues &session)
{
    session.session_id = get_param_string(status, "PBCSessionID");
    if (session.session_id.empty()) {
        return false;
    }
    session.time_stamp = get_param_string(status, "TimeStamp");
    if (ended && ended->session_id == session.session_id) {
        session.result         = ended->result;
        session.failure_reason = ended->failure_reason;
        session.enrollee_mac   = ended->enrollee_mac;
        session.bssid          = ended->bssid;
        return true;
    }
    session.result         = get_param_string(status, "Result");
    session.failure_reason = amxd_object_get_value(uint16_t, status, "FailureReason", nullptr);
    session.enrollee_mac   = get_param_string(status, "EnrolleeMACAddress");
    session.bssid          = get_param_string(status, "BSSID");
    return true;
}

/**
 * @brief Deletes the History entry with this PBC Session ID, if one exists.
 *
 * PBCSessionID is the unique key of the table, and a PBC Session ID can be used again (a
 * button Request after 60 s). A transaction deletes an instance only at its end, so the
 * deletion cannot be in the transaction that adds the new entry.
 *
 * @return false if the entry exists and the deletion failed.
 */
static bool mesh_pbc_delete_history_entry(amxd_object_t *history, const std::string &session_id)
{
    amxd_object_t *entry = nullptr;
    amxd_object_for_each(instance, it, history)
    {
        amxd_object_t *instance = amxc_container_of(it, amxd_object_t, it);
        if (get_param_string(instance, "PBCSessionID") == session_id) {
            entry = instance;
            break;
        }
    }
    if (!entry) {
        return true;
    }
    LOG(WARNING) << "push-button session ID " << session_id
                 << " is used again: its older History entry is deleted";
    char *path = amxd_object_get_path(history, AMXD_OBJECT_INDEXED);
    const std::string history_path(path ? path : "");
    free(path);
    const uint32_t index = amxd_object_get_index(entry);
    return mesh_pbc_write_object(history_path, [&](amxd_trans_t &transaction) {
        amxd_trans_del_inst(&transaction, index, nullptr);
    });
}

/**
 * @brief Adds to the transaction a History entry with the values of a session.
 *
 * @param[in] history The History table.
 */
static void mesh_pbc_add_history_entry(amxd_trans_t &transaction, amxd_object_t *history,
                                       const sMeshPbcSessionValues &session)
{
    amxd_trans_select_object(&transaction, history);
    amxd_trans_add_inst(&transaction, 0, nullptr);
    amxd_trans_set_value(cstring_t, &transaction, "PBCSessionID", session.session_id.c_str());
    amxd_trans_set_value(cstring_t, &transaction, "Result", session.result.c_str());
    amxd_trans_set_value(uint16_t, &transaction, "FailureReason", session.failure_reason);
    amxd_trans_set_value(cstring_t, &transaction, "EnrolleeMACAddress",
                         session.enrollee_mac.c_str());
    amxd_trans_set_value(cstring_t, &transaction, "BSSID", session.bssid.c_str());
    if (!session.time_stamp.empty()) {
        amxd_trans_set_value(cstring_t, &transaction, "TimeStamp", session.time_stamp.c_str());
    }
}

bool mesh_pbc_set_template_status_started(const std::string &template_path,
                                          const std::string &session_id)
{
    const std::string time_stamp = mesh_pbc_current_time();
    const char *counter          = "Sessions";

    sMeshPbcSessionValues ended;
    auto ended_it          = g_ended_sessions.find(template_path);
    const bool ended_found = (ended_it != g_ended_sessions.end());
    if (ended_found) {
        ended = ended_it->second;
        g_ended_sessions.erase(ended_it);
    }

    amxd_object_t *status =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s.Status", template_path.c_str());
    amxd_object_t *history     = status ? amxd_object_get_child(status, "History") : nullptr;
    const uint32_t max_entries = status ? get_param_uint32(status, "MaxHistoryEntries") : 0;
    sMeshPbcSessionValues previous;
    const bool move =
        history && max_entries > 0 &&
        mesh_pbc_get_status_session(status, ended_found ? &ended : nullptr, previous) &&
        mesh_pbc_delete_history_entry(history, previous.session_id);

    return mesh_pbc_write_template_status(template_path, counter, [&](amxd_trans_t &transaction) {
        if (move) {
            mesh_pbc_add_history_entry(transaction, history, previous);
            amxd_trans_select_object(&transaction, status);
        }
        amxd_trans_set_value(cstring_t, &transaction, "State", "InProgress");
        amxd_trans_set_value(cstring_t, &transaction, "PBCSessionID", session_id.c_str());
        if (!time_stamp.empty()) {
            amxd_trans_set_value(cstring_t, &transaction, "TimeStamp", time_stamp.c_str());
        }
        amxd_trans_set_value(cstring_t, &transaction, "Result", "");
        amxd_trans_set_value(uint16_t, &transaction, "FailureReason", 0);
        amxd_trans_set_value(cstring_t, &transaction, "EnrolleeMACAddress", "");
        amxd_trans_set_value(cstring_t, &transaction, "BSSID", "");
        if (history) {
            amxd_trans_select_object(&transaction, history);
            mesh_pbc_trim_history(transaction, history, max_entries, move ? 1 : 0);
        }
    });
}

bool mesh_pbc_set_template_status_ended(const std::string &template_path, const std::string &result,
                                        uint16_t failure_reason, const std::string &enrollee_mac,
                                        const std::string &bssid)
{
    const bool success  = (result == "Success");
    const char *counter = mesh_pbc_result_counter(result);
    sMeshPbcSessionValues ended;
    ended.result         = result;
    ended.failure_reason = failure_reason;
    ended.enrollee_mac   = success ? enrollee_mac : "";
    ended.bssid          = success ? bssid : "";
    if (!mesh_pbc_write_template_status(template_path, counter, [&](amxd_trans_t &transaction) {
            amxd_trans_set_value(cstring_t, &transaction, "State", "Idle");
            amxd_trans_set_value(cstring_t, &transaction, "Result", result.c_str());
            amxd_trans_set_value(uint16_t, &transaction, "FailureReason", failure_reason);
            amxd_trans_set_value(cstring_t, &transaction, "EnrolleeMACAddress",
                                 ended.enrollee_mac.c_str());
            amxd_trans_set_value(cstring_t, &transaction, "BSSID", ended.bssid.c_str());
        })) {
        return false;
    }
    amxd_object_t *status =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s.Status", template_path.c_str());
    ended.session_id                = status ? get_param_string(status, "PBCSessionID") : "";
    g_ended_sessions[template_path] = ended;
    return true;
}

bool mesh_pbc_set_template_status_rejected(const std::string &template_path,
                                           const std::string &result, uint16_t failure_reason)
{
    const char *counter = mesh_pbc_result_counter(result);
    return mesh_pbc_write_template_status(template_path, counter, [&](amxd_trans_t &transaction) {
        amxd_trans_set_value(cstring_t, &transaction, "Result", result.c_str());
        amxd_trans_set_value(uint16_t, &transaction, "FailureReason", failure_reason);
    });
}

bool mesh_pbc_set_bss_status(const std::string &bss_dm_path, const sMeshPbcBssStatus &status)
{
    const std::string time_stamp = mesh_pbc_current_time();
    return mesh_pbc_write_object(
        bss_dm_path + ".X_PRPLWARE-COM_MeshPBC", [&](amxd_trans_t &transaction) {
            amxd_trans_set_value(cstring_t, &transaction, "PBCSessionID",
                                 status.session_id.c_str());
            amxd_trans_set_value(cstring_t, &transaction, "Result", status.result.c_str());
            amxd_trans_set_value(uint16_t, &transaction, "FailureReason", status.failure_reason);
            amxd_trans_set_value(cstring_t, &transaction, "EnrolleeMACAddress",
                                 status.enrollee_mac.c_str());
            if (!time_stamp.empty()) {
                amxd_trans_set_value(cstring_t, &transaction, "TimeStamp", time_stamp.c_str());
            }
            amxd_trans_set_value(cstring_t, &transaction, "State", status.state.c_str());
        });
}

static void event_mesh_pbc_template_configuration_changed(const char *const sig_name,
                                                          const amxc_var_t *const data,
                                                          void *const priv)
{
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get " << TEMPLATES_ROOT_DM << "." << MESH_PBC_TEMPLATE_TABLE
                     << " instance from signal";
        return;
    }
    LOG(DEBUG) << "event_mesh_pbc_template_configuration_changed";
    templates_request_apply();
}

/**
 * @brief Deletes the g_ended_sessions entry of a template instance that was removed.
 *
 * A new instance can get the index, and so the path, of the removed instance.
 */
static void mesh_pbc_erase_ended_session(const char *const sig_name, const amxc_var_t *const data)
{
    if (!sig_name || std::string(sig_name) != "dm:instance-removed") {
        return;
    }
    amxd_object_t *table = amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    const char *name     = table ? amxd_object_get_name(table, AMXD_OBJECT_NAMED) : nullptr;
    if (!name || std::string(name) != MESH_PBC_TEMPLATE_TABLE) {
        return;
    }
    g_ended_sessions.erase(std::string(TEMPLATES_ROOT_DM) + "." + MESH_PBC_TEMPLATE_TABLE + "." +
                           std::to_string(GET_UINT32(data, "index")));
}

static void event_mesh_pbc_template_instance_changed(const char *const sig_name,
                                                     const amxc_var_t *const data, void *const priv)
{
    mesh_pbc_erase_ended_session(sig_name, data);
    if (!templates_events_enabled()) {
        return;
    }
    if (!amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data)) {
        LOG(WARNING) << "Failed to get " << TEMPLATES_ROOT_DM << "." << MESH_PBC_TEMPLATE_TABLE
                     << " from instance signal";
        return;
    }
    LOG(DEBUG) << "event_mesh_pbc_template_instance_changed";
    templates_request_apply();
}

/**
 * @brief Deletes the History entries of a template that were added first, until at most
 * Status.MaxHistoryEntries entries are left.
 */
static void event_mesh_pbc_max_history_entries_changed(const char *const sig_name,
                                                       const amxc_var_t *const data,
                                                       void *const priv)
{
    amxd_object_t *status = amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);
    amxd_object_t *history = status ? amxd_object_get_child(status, "History") : nullptr;
    if (!history) {
        LOG(WARNING) << "Failed to get the push-button Status.History from signal";
        return;
    }
    const uint32_t max_entries = get_param_uint32(status, "MaxHistoryEntries");
    if (amxd_object_get_instance_count(history) <= max_entries) {
        return;
    }
    char *path = amxd_object_get_path(history, AMXD_OBJECT_INDEXED);
    const std::string history_path(path ? path : "");
    free(path);
    mesh_pbc_write_object(history_path, [&](amxd_trans_t &transaction) {
        mesh_pbc_trim_history(transaction, history, max_entries);
    });
}

std::vector<beerocks::nbapi::sEvents> get_mesh_pbc_template_events_list(void)
{
    return {
        {"event_mesh_pbc_template_configuration_changed",
         event_mesh_pbc_template_configuration_changed},
        {"event_mesh_pbc_template_instance_changed", event_mesh_pbc_template_instance_changed},
        {"event_mesh_pbc_max_history_entries_changed", event_mesh_pbc_max_history_entries_changed},
    };
}

std::vector<beerocks::nbapi::sActionsCallback> get_mesh_pbc_template_actions_list(void)
{
    return {
        {"validate_mesh_pbc_uuid_e", validate_mesh_pbc_uuid_e},
    };
}

} // namespace actions
} // namespace controller
} // namespace prplmesh
