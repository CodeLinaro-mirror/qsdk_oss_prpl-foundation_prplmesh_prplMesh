/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "mesh_pbc_template_actions.h"
#include "nbapi_utils.h"
#include "on_action.h"
#include "templates_actions.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <sstream>

namespace prplmesh {
namespace controller {
namespace actions {

namespace {
constexpr char TR181_TEMPLATES_PREFIX[]      = "Device.WiFi.Templates.";
constexpr char MESH_PBC_TEMPLATE_TABLE[]     = "X_PRPLWARE-COM_MeshPBCTemplate";
constexpr char MESH_PBC_UUID_E_PARAM[]       = "X_PRPLWARE-COM_MeshPBCUUID_E";
constexpr char MESH_PBC_TEMPLATE_REF_PARAM[] = "X_PRPLWARE-COM_MeshPBCTemplateReference";
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

    session_template.wps             = wps;
    session_template.dpp             = dpp;
    session_template.listen_duration = static_cast<uint16_t>(listen_duration);
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
 * @brief A template deleted during a session has no Status object: the write is skipped with a
 * warning.
 */
template <typename F>
static bool mesh_pbc_write_template_status(const std::string &template_path, F fill)
{
    return mesh_pbc_write_object(template_path + ".Status", fill);
}

bool mesh_pbc_set_template_status_started(const std::string &template_path,
                                          const std::string &session_id)
{
    const std::string time_stamp = mesh_pbc_current_time();
    return mesh_pbc_write_template_status(template_path, [&](amxd_trans_t &transaction) {
        amxd_trans_set_value(cstring_t, &transaction, "State", "InProgress");
        amxd_trans_set_value(cstring_t, &transaction, "PBCSessionID", session_id.c_str());
        if (!time_stamp.empty()) {
            amxd_trans_set_value(cstring_t, &transaction, "TimeStamp", time_stamp.c_str());
        }
        amxd_trans_set_value(cstring_t, &transaction, "Result", "");
        amxd_trans_set_value(uint16_t, &transaction, "FailureReason", 0);
        amxd_trans_set_value(cstring_t, &transaction, "EnrolleeMACAddress", "");
        amxd_trans_set_value(cstring_t, &transaction, "BSSID", "");
    });
}

bool mesh_pbc_set_template_status_ended(const std::string &template_path, const std::string &result,
                                        uint16_t failure_reason, const std::string &enrollee_mac,
                                        const std::string &bssid)
{
    const bool success = (result == "Success");
    return mesh_pbc_write_template_status(template_path, [&](amxd_trans_t &transaction) {
        amxd_trans_set_value(cstring_t, &transaction, "State", "Idle");
        amxd_trans_set_value(cstring_t, &transaction, "Result", result.c_str());
        amxd_trans_set_value(uint16_t, &transaction, "FailureReason", failure_reason);
        amxd_trans_set_value(cstring_t, &transaction, "EnrolleeMACAddress",
                             success ? enrollee_mac.c_str() : "");
        amxd_trans_set_value(cstring_t, &transaction, "BSSID", success ? bssid.c_str() : "");
    });
}

bool mesh_pbc_set_template_status_rejected(const std::string &template_path,
                                           const std::string &result, uint16_t failure_reason)
{
    return mesh_pbc_write_template_status(template_path, [&](amxd_trans_t &transaction) {
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

std::string mesh_pbc_make_uuid_e(const sMacAddr &al_mac)
{
    if (al_mac == beerocks::net::network_utils::ZERO_MAC) {
        return "";
    }

    // Algorithm of the WPS UUID of pwhm (wld_radio.c, offset 0), which calls
    // swl_uuid_fromMacAddress() of libswlc (swl_uuid.c): the octets of the UUID are the ASCII
    // codes of the 12 hexadecimal digits of the MAC address text, repeated, with the version
    // nibble and the variant bits set to 0xf. libswlc uses the characters of its input text as
    // given; this code uses the lowercase text, so the value equals the pwhm value for a MAC
    // address given in lowercase.
    static const char hex_digits[] = "0123456789abcdef";
    char mac_digits[2 * sizeof(al_mac.oct)];
    for (size_t i = 0; i < sizeof(al_mac.oct); i++) {
        mac_digits[2 * i]     = hex_digits[al_mac.oct[i] >> 4];
        mac_digits[2 * i + 1] = hex_digits[al_mac.oct[i] & 0x0f];
    }
    std::array<uint8_t, 16> octets;
    for (size_t i = 0; i < octets.size(); i++) {
        octets[i] = static_cast<uint8_t>(mac_digits[i % sizeof(mac_digits)]);
    }
    octets[6] = (octets[6] & 0x0f) | 0xf0;
    octets[8] = (octets[8] & 0x3f) | 0xf0;

    std::string uuid_e;
    for (size_t i = 0; i < octets.size(); i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            uuid_e += '-';
        }
        uuid_e += hex_digits[octets[i] >> 4];
        uuid_e += hex_digits[octets[i] & 0x0f];
    }
    return uuid_e;
}

bool mesh_pbc_init_uuid_e(void)
{
    const std::string path = mesh_pbc_uuid_e_path();
    amxd_object_t *network =
        amxd_dm_findf(beerocks::nbapi::Amxrt::getDatamodel(), "%s.Network", TEMPLATES_ROOT_DM);
    if (!network) {
        LOG(ERROR) << "Failed to generate " << path << ": " << TEMPLATES_ROOT_DM
                   << ".Network is not found";
        return false;
    }
    if (!get_param_string(network, MESH_PBC_UUID_E_PARAM).empty()) {
        return true;
    }
    if (!g_database) {
        LOG(ERROR) << "Failed to generate " << path << ": g_database is nullptr";
        return false;
    }

    const sMacAddr &al_mac   = g_database->get_local_bridge_mac();
    const std::string uuid_e = mesh_pbc_make_uuid_e(al_mac);
    if (uuid_e.empty()) {
        LOG(ERROR) << "Failed to generate " << path << ": the Controller AL MAC is " << al_mac
                   << "; the value stays \"\"";
        return false;
    }
    if (!mesh_pbc_write_object(std::string(TEMPLATES_ROOT_DM) + ".Network",
                               [&](amxd_trans_t &transaction) {
                                   amxd_trans_set_value(cstring_t, &transaction,
                                                        MESH_PBC_UUID_E_PARAM, uuid_e.c_str());
                               })) {
        LOG(ERROR) << "Failed to write the generated " << path << "; the value stays \"\"";
        return false;
    }
    LOG(INFO) << path << " set to " << uuid_e << ", generated from the Controller AL MAC "
              << al_mac;
    return true;
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

static void event_mesh_pbc_template_instance_changed(const char *const sig_name,
                                                     const amxc_var_t *const data, void *const priv)
{
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

std::vector<beerocks::nbapi::sEvents> get_mesh_pbc_template_events_list(void)
{
    return {
        {"event_mesh_pbc_template_configuration_changed",
         event_mesh_pbc_template_configuration_changed},
        {"event_mesh_pbc_template_instance_changed", event_mesh_pbc_template_instance_changed},
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
