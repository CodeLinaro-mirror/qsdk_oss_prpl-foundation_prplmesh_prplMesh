/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bcl/beerocks_qos_utils.h>

#include <gtest/gtest.h>

namespace {

TEST(BeerocksQosUtilsTest, validate_scs_descriptor_element)
{
    const std::vector<uint8_t> descriptor = {beerocks::qos_management::scs_descriptor_element_id, 2,
                                             0x00, 0x01};
    const auto descriptor_info =
        beerocks::qos_management::parse_qos_management_descriptor_element(descriptor);

    EXPECT_TRUE(
        beerocks::qos_management::is_scs_descriptor_element(descriptor.data(), descriptor.size()));
    EXPECT_FALSE(
        beerocks::qos_management::is_mscs_descriptor_element(descriptor.data(), descriptor.size()));
    EXPECT_EQ(beerocks::qos_management::eDescriptorElementType::Scs, descriptor_info.element_type);
    EXPECT_EQ(beerocks::qos_management::eDescriptorRequestType::Remove,
              descriptor_info.request_type);
    EXPECT_TRUE(beerocks::qos_management::is_valid_qos_management_descriptor_element(descriptor));
    EXPECT_TRUE(beerocks::qos_management::is_remove_qos_management_descriptor(descriptor));
}

TEST(BeerocksQosUtilsTest, validate_mscs_descriptor_element)
{
    const std::vector<uint8_t> descriptor = {
        beerocks::qos_management::extended_element_id, 3,
        beerocks::qos_management::mscs_descriptor_extension_element_id, 0x00, 0x02};
    const auto descriptor_info =
        beerocks::qos_management::parse_qos_management_descriptor_element(descriptor);

    EXPECT_FALSE(
        beerocks::qos_management::is_scs_descriptor_element(descriptor.data(), descriptor.size()));
    EXPECT_TRUE(
        beerocks::qos_management::is_mscs_descriptor_element(descriptor.data(), descriptor.size()));
    EXPECT_EQ(beerocks::qos_management::eDescriptorElementType::Mscs, descriptor_info.element_type);
    EXPECT_EQ(beerocks::qos_management::eDescriptorRequestType::Add, descriptor_info.request_type);
    EXPECT_TRUE(beerocks::qos_management::is_valid_qos_management_descriptor_element(descriptor));
    EXPECT_FALSE(beerocks::qos_management::is_remove_qos_management_descriptor(descriptor));
}

TEST(BeerocksQosUtilsTest, reject_invalid_descriptor_element)
{
    const std::vector<uint8_t> invalid_length_descriptor = {
        beerocks::qos_management::scs_descriptor_element_id, 1, 0x00, 0x00};
    const std::vector<uint8_t> invalid_element_id_descriptor    = {0x00, 2, 0x00, 0x00};
    const std::vector<uint8_t> reserved_request_type_descriptor = {
        beerocks::qos_management::scs_descriptor_element_id, 2, 0x00, 0x03};

    EXPECT_FALSE(beerocks::qos_management::is_valid_qos_management_descriptor_element(
        invalid_length_descriptor));
    EXPECT_FALSE(
        beerocks::qos_management::is_remove_qos_management_descriptor(invalid_length_descriptor));
    EXPECT_FALSE(beerocks::qos_management::is_valid_qos_management_descriptor_element(
        invalid_element_id_descriptor));
    EXPECT_FALSE(beerocks::qos_management::is_remove_qos_management_descriptor(
        invalid_element_id_descriptor));
    EXPECT_FALSE(beerocks::qos_management::is_valid_qos_management_descriptor_element(
        reserved_request_type_descriptor));
}

TEST(BeerocksQosUtilsTest, parse_change_descriptor_element)
{
    const std::vector<uint8_t> descriptor = {beerocks::qos_management::scs_descriptor_element_id, 2,
                                             0x00, 0x02};
    const auto descriptor_info =
        beerocks::qos_management::parse_qos_management_descriptor_element(descriptor);

    EXPECT_EQ(beerocks::qos_management::eDescriptorElementType::Scs, descriptor_info.element_type);
    EXPECT_EQ(beerocks::qos_management::eDescriptorRequestType::Change,
              descriptor_info.request_type);
    EXPECT_TRUE(beerocks::qos_management::is_valid_qos_management_descriptor_element(descriptor));
    EXPECT_FALSE(beerocks::qos_management::is_remove_qos_management_descriptor(descriptor));
}

} // namespace
