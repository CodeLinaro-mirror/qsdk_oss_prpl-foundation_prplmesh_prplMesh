/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef MESH_PBC_TEMPLATE_ACTIONS_H
#define MESH_PBC_TEMPLATE_ACTIONS_H

#include "ambiorix_impl.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace prplmesh {
namespace controller {
namespace actions {

/**
 * @brief Resolve a reference to an instance of a Templates table.
 *
 * Accepts only the canonical instance form, with the native prefix
 * ("X_PRPLWARE-COM_Templates.<table>.<i>") or the public TR-181 prefix
 * ("Device.WiFi.Templates.<table>.<i>"), where <i> is the decimal instance index.
 * Alias, search-path and trailing-dot forms are rejected.
 *
 * @param reference Reference value as written by the operator.
 * @param table_name Name of the Templates table, for example "SSCTemplate".
 * @return The referenced instance, or nullptr if the reference does not resolve
 * to an instance of that table.
 */
amxd_object_t *mesh_pbc_resolve_templates_reference(const std::string &reference,
                                                    const std::string &table_name);

/**
 * @brief Push-button configuration of one staged BSS, from its X_PRPLWARE-COM_MeshPBCTemplate.
 */
struct sMeshPbcBssConfig {
    /**
     * Path of the template instance:
     * "X_PRPLWARE-COM_Templates.X_PRPLWARE-COM_MeshPBCTemplate.<i>".
     */
    std::string template_path;
    uint16_t listen_duration = 0;
    /** UUID-E octets, in the order of the hexadecimal digits of uuid_e_text. */
    std::array<uint8_t, 16> uuid_e{};
    std::string uuid_e_text;
    uint16_t device_password_id = 0;
    /** Device Password octets. Empty when the Push-Button Config TLV carries no password. */
    std::string device_password;
    /** True if device_password_id is not PushButton (4) and WPS.DevicePassword is "". */
    bool device_password_missing = false;
};

/**
 * @brief Result of mesh_pbc_get_bss_config().
 */
enum class eMeshPbcBssConfigResult {
    FOUND,   /**< The BSS gets a Push-Button Config TLV. */
    SKIP,    /**< The BSS is not configured for push-button onboarding. */
    INVALID, /**< The template reference or the template of the BSS has a value that is not
                  valid. */
};

/**
 * @brief Find the push-button configuration of a BSS that is staged from a BSSTemplate.
 *
 * Reads the current data model. The BSS gets a configuration only if all of these are true:
 * - template_ref is not "" (else SKIP).
 * - Templates.Network.Enable is true (else SKIP).
 * - template_ref resolves to an X_PRPLWARE-COM_MeshPBCTemplate instance (else INVALID).
 * - That template has Enable true (else SKIP).
 * - PBCTemplateID is not empty, PushButtonMethod contains "WPS", and WPS.DevicePassword has
 *   at most 32 octets.
 * - Templates.Network.X_PRPLWARE-COM_MeshPBCUUID_E is a canonical UUID and not "". All BSSs
 *   of all templates get this UUID-E.
 *
 * @param[in] template_ref The ssc_mesh_pbc_template_ref of the staged BSS (sBssInfoConf).
 * @param[out] config The configuration, set only when the result is FOUND.
 * @param[out] reason The cause of a result other than FOUND.
 * @return The result of the lookup.
 */
eMeshPbcBssConfigResult mesh_pbc_get_bss_config(const std::string &template_ref,
                                                sMeshPbcBssConfig &config, std::string &reason);

/**
 * @brief Values of an X_PRPLWARE-COM_MeshPBCTemplate instance that a push-button session uses.
 */
struct sMeshPbcSessionTemplate {
    /**
     * Path of the template instance:
     * "X_PRPLWARE-COM_Templates.X_PRPLWARE-COM_MeshPBCTemplate.<i>".
     */
    std::string template_path;
    /** True if PushButtonMethod contains "WPS". */
    bool wps = false;
    /** True if PushButtonMethod contains "DPP". */
    bool dpp                 = false;
    uint16_t listen_duration = 0;
};

/**
 * @brief Result of mesh_pbc_get_session_template().
 */
enum class eMeshPbcSessionTemplateResult {
    VALID,     /**< The template can start a session. */
    NOT_FOUND, /**< The reference does not resolve to a template instance. */
    INVALID,   /**< The template is disabled, or it has a value that is not valid. */
    NOT_READY, /**< Templates.Network.Enable is false: no BSS is staged from the Templates. */
};

/**
 * @brief Resolve a template reference and check the values that a session uses.
 *
 * Reads the current data model. The template can start a session only if all of these are
 * true: Templates.Network.Enable is true (else the result is NOT_READY); Enable is true;
 * PBCTemplateID is not empty; PushButtonMethod contains "WPS" or "DPP"; if PushButtonMethod
 * contains "WPS", Templates.Network.X_PRPLWARE-COM_MeshPBCUUID_E is a canonical UUID and not "".
 * The check does not read the SSCTemplate references: a template that no SSCTemplate
 * references is valid. The BSSs of a session come from the push-button configuration that
 * each radio received last.
 *
 * @param[in] reference Reference to the template, with the prefix "X_PRPLWARE-COM_Templates."
 * or "Device.WiFi.Templates.".
 * @param[out] session_template The values, set when the result is VALID, INVALID or NOT_READY
 * (only the path is set for INVALID and NOT_READY).
 * @param[out] reason The cause of a result other than VALID. It names the parameter.
 * @return The result of the check.
 */
eMeshPbcSessionTemplateResult
mesh_pbc_get_session_template(const std::string &reference,
                              sMeshPbcSessionTemplate &session_template, std::string &reason);

/**
 * @brief Get Templates.Network.X_PRPLWARE-COM_PrimaryMeshPBCTemplateReference.
 *
 * @return The reference, or "" if the parameter is not found.
 */
std::string mesh_pbc_get_primary_template_reference(void);

/**
 * @brief Write the template Status of a session that the Controller created.
 *
 * Sets State "InProgress", PBCSessionID and TimeStamp, and clears Result, FailureReason,
 * EnrolleeMACAddress and BSSID, in one transaction.
 *
 * @param[in] template_path Path of the template instance.
 * @param[in] session_id PBC Session ID in the data-model form.
 * @return false if the template is not found or the write failed.
 */
bool mesh_pbc_set_template_status_started(const std::string &template_path,
                                          const std::string &session_id);

/**
 * @brief Write the template Status at the end of a session.
 *
 * Sets State "Idle", Result and FailureReason, in one transaction. EnrolleeMACAddress and
 * BSSID are set when the result is "Success", else they are cleared.
 *
 * @param[in] template_path Path of the template instance.
 * @param[in] result Value of Status.Result.
 * @param[in] failure_reason Value of Status.FailureReason.
 * @param[in] enrollee_mac Enrollee MAC address, used only for "Success".
 * @param[in] bssid BSSID of the onboarding, used only for "Success".
 * @return false if the template is not found or the write failed.
 */
bool mesh_pbc_set_template_status_ended(const std::string &template_path, const std::string &result,
                                        uint16_t failure_reason, const std::string &enrollee_mac,
                                        const std::string &bssid);

/**
 * @brief Write the template Status of a request that the Controller rejected.
 *
 * Sets Result and FailureReason only, in one transaction. State, TimeStamp and PBCSessionID
 * do not change.
 *
 * @param[in] template_path Path of the template instance.
 * @param[in] result Value of Status.Result.
 * @param[in] failure_reason Value of Status.FailureReason.
 * @return false if the template is not found or the write failed.
 */
bool mesh_pbc_set_template_status_rejected(const std::string &template_path,
                                           const std::string &result, uint16_t failure_reason);

/**
 * @brief Values of the push-button state object of a BSS.
 */
struct sMeshPbcBssStatus {
    /** "Idle", "Requested" or "InProgress". */
    std::string state = "Idle";
    /** PBC Session ID in the data-model form. */
    std::string session_id;
    /** "", "Success", "Error_Not_Ready", "Error_Timeout" or "Error_Other". */
    std::string result;
    uint16_t failure_reason = 0;
    /** Enrollee MAC address, or "" when result is not "Success". */
    std::string enrollee_mac;
};

/**
 * @brief Write the push-button state object of a BSS, with TimeStamp set to the current time.
 *
 * Path: "<bss_dm_path>.X_PRPLWARE-COM_MeshPBC" (Network.Device.{i}.Radio.{j}.BSS.{k}). The six
 * parameters are written in one transaction.
 *
 * @param[in] bss_dm_path Data model path of the BSS instance.
 * @param[in] status The values.
 * @return false if the object is not found or the write failed.
 */
bool mesh_pbc_set_bss_status(const std::string &bss_dm_path, const sMeshPbcBssStatus &status);

std::vector<beerocks::nbapi::sActionsCallback> get_mesh_pbc_template_actions_list(void);

/**
 * @brief Get the event handlers that restage the templates when a push-button template changes.
 *
 * Each change of an X_PRPLWARE-COM_MeshPBCTemplate instance or parameter, except a Status
 * write, requests a templates apply, so that the Agents get an AP-Autoconfiguration Renew and
 * the new values.
 */
std::vector<beerocks::nbapi::sEvents> get_mesh_pbc_template_events_list(void);

} // namespace actions
} // namespace controller
} // namespace prplmesh

#endif // MESH_PBC_TEMPLATE_ACTIONS_H
