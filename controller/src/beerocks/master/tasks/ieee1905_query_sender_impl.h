/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _IEEE1905_QUERY_SENDER_IMPL_H_
#define _IEEE1905_QUERY_SENDER_IMPL_H_

#include "ieee1905_task.h"

namespace son {

class RealIEEE1905QuerySender : public IEEE1905QuerySender {
public:
    explicit RealIEEE1905QuerySender(db &database);

    bool send_topology_query(const sMacAddr &dest_mac, ieee1905_1::CmduMessageTx &cmdu_tx) override;
    bool send_higher_layer_query(const sMacAddr &dest_mac,
                                 ieee1905_1::CmduMessageTx &cmdu_tx) override;

private:
    db &database;
};

} // namespace son

#endif
