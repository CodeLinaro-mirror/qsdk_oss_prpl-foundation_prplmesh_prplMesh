/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _BEEROCKS_QOS_UTILS_H_
#define _BEEROCKS_QOS_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace beerocks {
namespace qos_management {

constexpr uint8_t scs_descriptor_element_id            = 185;
constexpr uint8_t extended_element_id                  = 255;
constexpr uint8_t mscs_descriptor_extension_element_id = 88;
constexpr size_t descriptor_request_type_offset        = 3;
constexpr uint8_t descriptor_request_type_add          = 0;
constexpr uint8_t descriptor_request_type_remove       = 1;
constexpr uint8_t descriptor_request_type_change       = 2;

enum class eDescriptorElementType : uint8_t { Invalid, Scs, Mscs };
enum class eDescriptorRequestType : uint8_t { Invalid, Add, Remove, Change };

struct sDescriptorElementInfo {
    eDescriptorElementType element_type = eDescriptorElementType::Invalid;
    eDescriptorRequestType request_type = eDescriptorRequestType::Invalid;
    uint8_t raw_request_type            = 0;

    /**
     * @brief Check whether the parsed descriptor metadata is usable.
     *
     * @return true when both the descriptor element type and request type are valid.
     */
    bool is_valid() const;
};

/**
 * @brief Check whether the descriptor starts with an SCS element header.
 *
 * @param descriptor Descriptor element bytes.
 * @param descriptor_len Descriptor element length in bytes.
 * @return true if the descriptor has an SCS element ID.
 */
bool is_scs_descriptor_element(const uint8_t *descriptor, size_t descriptor_len);

/**
 * @brief Check whether the descriptor starts with an MSCS extension element header.
 *
 * @param descriptor Descriptor element bytes.
 * @param descriptor_len Descriptor element length in bytes.
 * @return true if the descriptor has an MSCS extension element header.
 */
bool is_mscs_descriptor_element(const uint8_t *descriptor, size_t descriptor_len);

/**
 * @brief Check whether the descriptor is an SCS or MSCS QoS management descriptor.
 *
 * @param descriptor Descriptor element bytes.
 * @param descriptor_len Descriptor element length in bytes.
 * @return true if the descriptor starts with a supported SCS/MSCS element header.
 */
bool is_qos_management_descriptor_element(const uint8_t *descriptor, size_t descriptor_len);

/**
 * @brief Convert an IEEE descriptor request type value to an enum.
 *
 * @param value Raw descriptor request type value.
 * @return eDescriptorRequestType Parsed request type, or Invalid for unknown values.
 */
eDescriptorRequestType request_type_from_value(uint8_t value);

/**
 * @brief Parse a complete SCS/MSCS descriptor element.
 *
 * @param descriptor Descriptor element bytes.
 * @param descriptor_len Descriptor element length in bytes.
 * @return sDescriptorElementInfo Parsed descriptor metadata.
 */
sDescriptorElementInfo parse_qos_management_descriptor_element(const uint8_t *descriptor,
                                                               size_t descriptor_len);

/**
 * @brief Parse a complete SCS/MSCS descriptor element byte vector.
 *
 * @param descriptor_bytes Descriptor element bytes.
 * @return sDescriptorElementInfo Parsed descriptor metadata.
 */
sDescriptorElementInfo
parse_qos_management_descriptor_element(const std::vector<uint8_t> &descriptor_bytes);

/**
 * @brief Validate a complete QoS management descriptor element.
 *
 * @param descriptor_bytes Descriptor element bytes.
 * @return true if the descriptor is a supported SCS/MSCS element with a valid request type.
 */
bool is_valid_qos_management_descriptor_element(const std::vector<uint8_t> &descriptor_bytes);

/**
 * @brief Check whether the descriptor carries a remove request.
 *
 * @param descriptor_bytes Descriptor element bytes.
 * @return true if the descriptor is valid and its request type is Remove.
 */
bool is_remove_qos_management_descriptor(const std::vector<uint8_t> &descriptor_bytes);

} // namespace qos_management
} // namespace beerocks

#endif // _BEEROCKS_QOS_UTILS_H_
