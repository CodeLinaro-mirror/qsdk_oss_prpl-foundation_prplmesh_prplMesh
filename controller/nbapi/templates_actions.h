/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef TEMPLATES_ACTIONS_H
#define TEMPLATES_ACTIONS_H

#include "ambiorix_impl.h"
#include "nbapi_utils.h"

#include <list>
#include <vector>

namespace prplmesh {
namespace controller {
namespace actions {

/**
 * Request a templates restage and schedule at most one TEMPLATES_COMMIT_APPLY
 * (coalesces while pending / in progress). No-op while an apply is running so
 * DM writes from that apply cannot immediately re-arm another apply.
 */
void templates_request_apply(void);

void templates_commit_apply_pending(void);

void templates_restage_only(void);

bool is_templates_dm_initialized();

void set_templates_dm_initialized(bool val);

std::vector<beerocks::nbapi::sEvents> get_templates_events_list(void);

enum class eTemplateRsnMode { PSK, SAE, SAE_EXT, TRANSITION, PCM };

bool template_operating_classes_include_6ghz(const std::list<uint8_t> &operating_class);

void template_fill_rsn_security_ies(amxd_object_t *security_template_obj, bool is_6ghz_band,
                                    eTemplateRsnMode mode, std::vector<uint8_t> &out);

} // namespace actions
} // namespace controller
} // namespace prplmesh

#endif // TEMPLATES_ACTIONS_H
