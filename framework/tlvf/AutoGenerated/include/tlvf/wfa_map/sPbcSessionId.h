///////////////////////////////////////
// AUTO GENERATED FILE - DO NOT EDIT //
///////////////////////////////////////

/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _TLVF_WFA_MAP_SPBCSESSIONID_H_
#define _TLVF_WFA_MAP_SPBCSESSIONID_H_

#include <cstddef>
#include <stdint.h>
#include <tlvf/swap.h>
#include "tlvf/common/sMacAddr.h"

namespace wfa_map {

typedef struct sPbcSessionId {
    sMacAddr al_mac;
    uint16_t mid;
    void struct_swap(){
        al_mac.struct_swap();
        tlvf_swap(16, reinterpret_cast<uint8_t*>(&mid));
    }
    void struct_init(){
        al_mac.struct_init();
    }
} __attribute__((packed)) sPbcSessionId;


}; // close namespace: wfa_map

#endif //_TLVF/WFA_MAP_SPBCSESSIONID_H_
