/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef PRPLMESH_AMX_CLIENT_H
#define PRPLMESH_AMX_CLIENT_H

#include <amxc/amxc.h>
#include <amxp/amxp.h>

#include <amxc/amxc.h>
#include <amxd/amxd_action.h>
#include <amxd/amxd_dm.h>
#include <amxd/amxd_object.h>
#include <amxd/amxd_object_event.h>
#include <amxd/amxd_transaction.h>

#include <amxb/amxb.h>
#include <amxb/amxb_register.h>

#include <amxo/amxo.h>
#include <amxo/amxo_save.h>

#include <easylogging++.h>

#include <iostream>
#include <locale.h>
#include <time.h>

namespace beerocks {
namespace prplmesh_amx {

class AmxResult {
public:
    AmxResult() { amxc_var_init(&m_value); }
    ~AmxResult() { amxc_var_clean(&m_value); }

    AmxResult(const AmxResult &) = delete;
    AmxResult &operator=(const AmxResult &) = delete;

    amxc_var_t &raw() { return m_value; }
    const amxc_var_t &raw() const { return m_value; }

    amxc_var_t *object() { return amxc_var_get_first(GET_ARG(&m_value, "0")); }
    const amxc_htable_t *htable() const
    {
        return amxc_var_constcast(amxc_htable_t, GETI_ARG(&m_value, 0));
    }

private:
    amxc_var_t m_value;
};

class AmxClient {

public:
    AmxClient()                  = default;
    AmxClient(const AmxClient &) = delete;
    AmxClient &operator=(const AmxClient &) = delete;
    ~AmxClient();

    // Connect to an ambiorix.
    bool amx_initialize(const std::string &amxb_backend, const std::string &bus_uri);

    // Get an object from bus using object_path and store the full AMX response in result.
    bool get_object(const std::string &object_path, AmxResult &result, bool &request_timed_out);
    bool get_object(const std::string &object_path, AmxResult &result)
    {
        bool dummy = false;
        return get_object(object_path, result, dummy);
    }

    // Get an htable object from bus using object_path and store the full AMX response in result.
    bool get_htable_object(const std::string &object_path, AmxResult &result);

    /**
     * @returns status from amxb_error.h (AMXB_STATUS_OK on success etc.)
     */
    int set_object(const std::string &path, amxc_var_t *value, amxc_var_t *ret = 0);

private:
    amxb_bus_ctx_t *bus_ctx = nullptr;
};

} // namespace prplmesh_amx
} // namespace beerocks

#endif // PRPLMESH_AMX_CLIENT_H
