/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_qos_utils.h>

namespace beerocks {
namespace qos_management {

bool sDescriptorElementInfo::is_valid() const
{
    return element_type != eDescriptorElementType::Invalid &&
           request_type != eDescriptorRequestType::Invalid;
}

bool is_scs_descriptor_element(const uint8_t *descriptor, size_t descriptor_len)
{
    return descriptor && descriptor_len >= 2 && descriptor[0] == scs_descriptor_element_id;
}

bool is_mscs_descriptor_element(const uint8_t *descriptor, size_t descriptor_len)
{
    return descriptor && descriptor_len >= 3 && descriptor[0] == extended_element_id &&
           descriptor[2] == mscs_descriptor_extension_element_id;
}

bool is_qos_management_descriptor_element(const uint8_t *descriptor, size_t descriptor_len)
{
    return is_scs_descriptor_element(descriptor, descriptor_len) ||
           is_mscs_descriptor_element(descriptor, descriptor_len);
}

eDescriptorRequestType request_type_from_value(uint8_t value)
{
    switch (value) {
    case descriptor_request_type_add:
        return eDescriptorRequestType::Add;
    case descriptor_request_type_remove:
        return eDescriptorRequestType::Remove;
    case descriptor_request_type_change:
        return eDescriptorRequestType::Change;
    default:
        return eDescriptorRequestType::Invalid;
    }
}

sDescriptorElementInfo parse_qos_management_descriptor_element(const uint8_t *descriptor,
                                                               size_t descriptor_len)
{
    sDescriptorElementInfo info;

    if (!descriptor || descriptor_len < 4) {
        return info;
    }

    const auto descriptor_length = static_cast<size_t>(descriptor[1]) + 2;
    if (descriptor_len != descriptor_length) {
        return info;
    }

    if (is_scs_descriptor_element(descriptor, descriptor_len)) {
        info.element_type = eDescriptorElementType::Scs;
    } else if (is_mscs_descriptor_element(descriptor, descriptor_len)) {
        info.element_type = eDescriptorElementType::Mscs;
    } else {
        return info;
    }

    info.raw_request_type = descriptor[descriptor_request_type_offset];
    info.request_type     = request_type_from_value(info.raw_request_type);
    return info;
}

sDescriptorElementInfo
parse_qos_management_descriptor_element(const std::vector<uint8_t> &descriptor_bytes)
{
    return parse_qos_management_descriptor_element(descriptor_bytes.data(),
                                                   descriptor_bytes.size());
}

bool is_valid_qos_management_descriptor_element(const std::vector<uint8_t> &descriptor_bytes)
{
    return parse_qos_management_descriptor_element(descriptor_bytes).is_valid();
}

bool is_remove_qos_management_descriptor(const std::vector<uint8_t> &descriptor_bytes)
{
    return parse_qos_management_descriptor_element(descriptor_bytes).request_type ==
           eDescriptorRequestType::Remove;
}

} // namespace qos_management
} // namespace beerocks
