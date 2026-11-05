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

std::vector<beerocks::nbapi::sActionsCallback> get_mesh_pbc_template_actions_list(void);

} // namespace actions
} // namespace controller
} // namespace prplmesh

#endif // MESH_PBC_TEMPLATE_ACTIONS_H
